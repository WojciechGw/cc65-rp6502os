/* 

Features: labels, .org, .byte, .word, .equ, <, >, .include, listing LST

TODO call parameters
mass sourcecode.asm -out outfile.bin -base <baseaddress> -run <runaddress>

*/
#include "commons.h"
#include "ext-hass-opcodes.h"

#define APPVER "20260105.0827"
#define APPDIRDEFAULT "USB0:/SHELL/"
#define APP_MSG_TITLE CSI_RESET "\x1b[2;1HOS Shell > Handy ASSembler 65C02S                          version " APPVER
#define APP_MSG_START_ASSEMBLING ANSI_DARK_GRAY "\x1b[4;1HStart compilation ... " ANSI_RESET
#define APP_MSG_START_ENTERCODE ANSI_DARK_GRAY "\x1b[4;1HEnter code or empty line to start compilation ... " ANSI_RESET

/* --- limits --- */
#define MAXLINES    256
#define MAXLEN      50
#define MAXOUT      8192u
#define MAXSYM      64
#define MAXINCDEPTH 4

/* --- bufor wejścia --- */
static int   nlines = 0;

/* --- wyjście domyślne jeżeli brak .org --- */
static uint16_t org = 0x9000;
static uint16_t pc  = 0x9000;

/* wspólne bufory robocze, by ograniczyć lokalne */
static char g_buf[MAXLEN];
static char g_buf2[MAXLEN];
static char g_buf3[MAXLEN];
static char outfilebuffer[80];
static char g_tok[80];
static char g_incpath[256];
static char g_symtmp[48];
static char g_outpath[128] = "out.bin\0";
static char g_listpath[128] = "out.lst\0";
static uint16_t line_pc_before;
static uint16_t line_pc_after;
static char *eq_name, *eq_val;
static uint16_t a;

#define STAT_SUCCESS        0b00000000
#define STAT_PASS1_ERROR    0b00000001
#define STAT_PASS2_ERROR    0b00000010
#define STAT_PRE_INCLUDE    0b00000100

static unsigned int assembly_status;

/* --- magazyny w XRAM (RIA port 1) --- */
#define XRAM_LINES_BASE 0x0000u
#define XRAM_LINES_SIZE 0x3200u
#define XRAM_OUT_BASE   0x3200u
#define XRAM_OUT_SIZE   0x2000u
#define XRAM_LST_BASE   0x5200u
#define XRAM_LST_SIZE   0x50u

#if (MAXLINES * MAXLEN) > XRAM_LINES_SIZE
#error "MAXLINES*MAXLEN exceeds 16KB XRAM source area"
#endif
#if MAXOUT > XRAM_OUT_SIZE
#error "MAXOUT exceeds 8KB XRAM output area"
#endif

// globalne robocze, by nie zużywać lokali
static char *g_s,*g_mn,*g_op,*g_dir,*g_rest;
static char  g_MN[8];
static int16_t g_opcode;
static const opdef_t* g_def;

static int is_branch_mnemonic(const char* mn){
    return strcmp(mn,"BRA")==0 || strcmp(mn,"BCC")==0 || strcmp(mn,"BCS")==0 ||
           strcmp(mn,"BEQ")==0 || strcmp(mn,"BMI")==0 || strcmp(mn,"BNE")==0 ||
           strcmp(mn,"BPL")==0 || strcmp(mn,"BVC")==0 || strcmp(mn,"BVS")==0 ||
           strcmp(mn,"BBR0")==0 || strcmp(mn,"BBR1")==0 || strcmp(mn,"BBR2")==0 || strcmp(mn,"BBR3")==0 ||
           strcmp(mn,"BBR4")==0 || strcmp(mn,"BBR5")==0 || strcmp(mn,"BBR6")==0 || strcmp(mn,"BBR7")==0 ||
           strcmp(mn,"BBS0")==0 || strcmp(mn,"BBS1")==0 || strcmp(mn,"BBS2")==0 || strcmp(mn,"BBS3")==0 ||
           strcmp(mn,"BBS4")==0 || strcmp(mn,"BBS5")==0 || strcmp(mn,"BBS6")==0 || strcmp(mn,"BBS7")==0;
        }

static int is_stack_mnemonic(const char* mn){
    return strcmp(mn,"BRK")==0 || strcmp(mn,"PHA")==0 || strcmp(mn,"PHP")==0 ||
           strcmp(mn,"PHX")==0 || strcmp(mn,"PHY")==0 ||
           strcmp(mn,"PLA")==0 || strcmp(mn,"PLP")==0 ||
           strcmp(mn,"PLX")==0 || strcmp(mn,"PLY")==0 ||
           strcmp(mn,"RTI")==0 || strcmp(mn,"RTS")==0;
}

static int map_mode_to_op(const asm_mode_t m, const char* mn){
    if(m==M_IMP && mn && is_stack_mnemonic(mn)) return M_STACK;
    return (int)m;
}

/* --- symbole --- */
typedef struct {
    char     name[48];
    uint16_t value;
    unsigned defined;
} symbol_t;

static int      nsym = 0;

/* symbol table stored in XRAM to save RAM when linking at $A000 */
#define XRAM_SYM_BASE   0x6000u
#define XRAM_SYM_STRIDE 64u
#define XRAM_SYM_SIZE   ((unsigned)MAXSYM * (unsigned)XRAM_SYM_STRIDE)

static unsigned xram_sym_addr(unsigned idx){
    return (unsigned)(XRAM_SYM_BASE + idx * (unsigned)XRAM_SYM_STRIDE);
}

static void xram_sym_clear_all(void){
    unsigned i;
    RIA.addr1 = XRAM_SYM_BASE;
    RIA.step1 = 1;
    for(i=0;i<XRAM_SYM_SIZE;i++) RIA.rw1 = 0x00;
}

static void xram_sym_read_name(unsigned idx, char* dst){
    unsigned addr = xram_sym_addr(idx);
    unsigned i;
    if(!dst) return;
    RIA.addr1 = addr;
    RIA.step1 = 1;
    for(i=0;i<48u;i++) dst[i] = (char)RIA.rw1;
    dst[47] = 0;
}

static void xram_sym_write_name(unsigned idx, const char* name){
    unsigned addr = xram_sym_addr(idx);
    unsigned i = 0;
    RIA.addr1 = addr;
    RIA.step1 = 1;
    if(!name) name = "";
    for(i=0;i<48u;i++){
        unsigned char ch = (unsigned char)name[i];
        if(ch == 0) break;
        RIA.rw1 = (uint8_t)ch;
    }
    while(i<48u){
        RIA.rw1 = 0;
        ++i;
    }
}

static uint16_t xram_sym_get_value(unsigned idx){
    unsigned addr = xram_sym_addr(idx) + 48u;
    uint8_t lo, hi;
    RIA.addr1 = addr;
    RIA.step1 = 1;
    lo = RIA.rw1;
    hi = RIA.rw1;
    return (uint16_t)lo | ((uint16_t)hi << 8);
}

static unsigned xram_sym_is_defined(unsigned idx){
    RIA.addr1 = xram_sym_addr(idx) + 50u;
    return (unsigned)RIA.rw1;
}

static void xram_sym_set_value_defined(unsigned idx, uint16_t value, unsigned defined){
    unsigned addr = xram_sym_addr(idx) + 48u;
    RIA.addr1 = addr;
    RIA.step1 = 1;
    RIA.rw1 = (uint8_t)(value & 0xFF);
    RIA.rw1 = (uint8_t)(value >> 8);
    RIA.rw1 = (uint8_t)(defined ? 1u : 0u);
}

static void xram_write_lst_line(const char* line, unsigned len, int fd){
    unsigned i;
    if(fd < 0 || !line || !len) return;
    if(len > XRAM_LST_SIZE) len = XRAM_LST_SIZE;
    RIA.addr0 = XRAM_LST_BASE;
    RIA.step0 = 1;
    for(i = 0; i < XRAM_LST_SIZE; i++) RIA.rw0 = 0x00;
    RIA.addr0 = XRAM_LST_BASE;
    RIA.step0 = 1;
    for(i = 0; i < len; i++) RIA.rw0 = (uint8_t)line[i];
    write_xram(XRAM_LST_BASE, len, fd);
}

/* --- wartości z operatorami < > --- */
typedef enum { V_NORMAL = 0, V_LOW = 1, V_HIGH = 2 } asm_vop_t;

typedef struct {
    unsigned  is_label;
    char      label[48];
    uint16_t  num;
    asm_vop_t op;
    unsigned  force_zp;
} asm_value_t;

/* --- tryby i opkody --- */
static asm_value_t g_val;
static asm_mode_t g_mode;

/* --- narzędzia --- */
static int is_ident_start(int c){ return isalpha(c) || c=='_' || c=='.'; }
static int is_ident_char (int c){ return isalnum(c) || c=='_' || c=='.'; }

static void trim_comment(char* s){
    char* p = s;
    while(*p){
        if(*p==';'){ *p = '\0'; break; }
        ++p;
    }
}
static void rstrip(char* s){
    int L = (int)strlen(s);
    while(L && (s[L-1]=='\r'||s[L-1]=='\n'||s[L-1]==' '||s[L-1]=='\t')){ s[--L]=0; }
}
static void ltrim_ptr(char** ps){ while(**ps==' '||**ps=='\t') (*ps)++; }
static void to_upper_str(char* s){ while(*s){ *s=(char)toupper((unsigned char)*s); ++s; } }
static void strip_utf8_bom(char* s){
    if(!s) return;
    if((unsigned char)s[0]==0xEF && (unsigned char)s[1]==0xBB && (unsigned char)s[2]==0xBF){
        memmove(s, s+3, strlen(s+3)+1);
    }
}

// split_token jest zdefiniowane niżej; prototyp potrzebny dla C89
static int split_token(char* s, char** a, char** b);

static void xram1_read_bytes(unsigned addr, uint8_t* dst, unsigned len){
    RIA.addr1 = addr;
    RIA.step1 = 1;
    while(len--) *dst++ = RIA.rw1;
}

static void xram1_fill(unsigned addr, uint8_t value, unsigned len){
    RIA.addr1 = addr;
    RIA.step1 = 1;
    while(len--) RIA.rw1 = value;
}

static unsigned xram_line_addr(unsigned li){
    return (unsigned)(XRAM_LINES_BASE + (unsigned)li * (unsigned)MAXLEN);
}

static void xram_line_write(unsigned li, const char* text){
    unsigned addr = xram_line_addr(li);
    unsigned i = 0;
    RIA.addr1 = addr;
    RIA.step1 = 1;
    if(!text) text = "";
    for(i=0;i<(unsigned)MAXLEN;i++){
        unsigned char ch = (unsigned char)text[i];
        if(ch == 0){
            break;
        }
        RIA.rw1 = (uint8_t)ch;
    }
    while(i<(unsigned)MAXLEN){
        RIA.rw1 = 0;
        ++i;
    }
}

static void xram_line_read(unsigned li, char* dst){
    unsigned addr = xram_line_addr(li);
    unsigned i;
    if(!dst) return;
    xram1_read_bytes(addr, (uint8_t*)dst, (unsigned)MAXLEN);
    dst[MAXLEN-1] = 0;
    for(i=0;i<(unsigned)(MAXLEN-1);++i){
        if(dst[i]==0) break;
    }
}

static void xram_out_write_byte(uint16_t off, uint8_t b){
    RIA.addr1 = (unsigned)(XRAM_OUT_BASE + off);
    RIA.rw1 = b;
}

static uint8_t xram_out_read_byte(uint16_t off){
    RIA.addr1 = (unsigned)(XRAM_OUT_BASE + off);
    RIA.step1 = 1;
    return RIA.rw1;
}

static int parse_ascii_bytes(const char* s, uint8_t* out, int max_out, int* out_len){
    int n = 0;
    if(out_len) *out_len = 0;
    if(!s) return 0;
    while(*s==' '||*s=='\t') ++s;
    if(*s != '"') return 0;
    ++s;
    while(*s && *s != '"'){
        unsigned char ch = (unsigned char)*s++;
        if(ch == '\\'){
            unsigned char e = (unsigned char)*s++;
            if(!e) return 0;
            if(e=='n') ch = '\n';
            else if(e=='r') ch = '\r';
            else if(e=='t') ch = '\t';
            else if(e=='\\') ch = '\\';
            else if(e=='"') ch = '"';
            else return 0;
        }
        if(n >= max_out) return 0;
        out[n++] = (uint8_t)ch;
    }
    if(*s != '"') return 0;
    ++s;
    while(*s==' '||*s=='\t') ++s;
    if(*s) return 0;
    if(out_len) *out_len = n;
    return 1;
}

static void set_output_path(const char* path){
    size_t len;
    if(!path || !*path) return;
    len = strlen(path);
    if(len >= sizeof(g_outpath)) len = sizeof(g_outpath) - 1;
    memcpy(g_outpath, path, len);
    g_outpath[len] = 0;
}

/*
static void set_listing_path(const char* path){
    size_t len;
    if(!path || !*path) return;
    len = strlen(path);
    if(len >= sizeof(g_listpath)) len = sizeof(g_listpath) - 1;
    memcpy(g_listpath, path, len);
    g_listpath[len] = 0;
}
*/

/* rozpoznaje składnię: NAME .equ value (zwraca wskaźniki do bufora roboczego) */
static int parse_named_equ_line(const char* line, char** name_out, char** value_out){
    char* s;
    char* name;
    char* rest;
    char* dir;
    char* val;

    if(!line) return 0;
    strncpy(g_buf3, line, MAXLEN-1);
    g_buf3[MAXLEN-1] = 0;

    s = g_buf3;
    ltrim_ptr(&s);
    if(!split_token(s, &name, &rest) || !name || !rest) return 0;
    if(!split_token(rest, &dir, &val) || !dir || !val) return 0;
    to_upper_str(dir);
    if(strcmp(dir, ".EQU") != 0) return 0;

    *name_out  = name;
    *value_out = val;
    return 1;
}

static int find_sym(const char* name){
    int i;
    for(i=0;i<nsym;i++){
        xram_sym_read_name((unsigned)i, g_symtmp);
        if(strcmp(g_symtmp,name)==0) return i;
    }
    return -1;
}
static int add_or_update_sym(const char* name, uint16_t value, unsigned defined){
    int i;
    if(nsym>=MAXSYM){ printf("Too many symbols" NEWLINE); exit(1); }
    i = find_sym(name);
    if(i>=0){
        if(defined) xram_sym_set_value_defined((unsigned)i, value, 1);
        return i;
    }
    xram_sym_write_name((unsigned)nsym, name);
    xram_sym_set_value_defined((unsigned)nsym, value, defined);
    nsym++;
    return nsym-1;
}

static void add_line(const char* text){
    if(nlines >= MAXLINES){ printf("Too many rows" NEWLINE); exit(1); }
    xram_line_write((unsigned)nlines, text);
    nlines++;
}

/* --- parser wartości --- */
static int parse_number10(const char* s, uint16_t* v){
    char* endp;
    unsigned long x;
    if(!isdigit((unsigned char)s[0])) return 0;
    x = strtoul(s,&endp,10);
    if(endp==s) return 0;
    *v = (uint16_t)x;
    return 1;
}

static int parse_number(const char* s, uint16_t* v){
    char* endp;
    unsigned long x;
    const char* p = s;
    unsigned value = 0;
    int bits = 0;

    if(!s) return 0;

    if(s[0]=='$'){
        x = strtoul(s+1,&endp,16);
        if(endp == (s+1)) return 0;
        *v = (uint16_t)x;
        return 1;
    }

    while(*p==' ' || *p=='\t') ++p;
    if(*p=='%'){
        ++p;
        // while(*p==' ' || *p=='\t') ++p; /* allow whitespace after % */
        while(*p=='0' || *p=='1'){
            if(bits >= 8) return 0;
            value = (value << 1) | (unsigned)(*p - '0');
            ++bits;
            ++p;
        }
        while(*p==' ' || *p=='\t') ++p;
        if(*p || bits==0) return 0;
        *v = (uint16_t)value;
        return 1;
    }
    return parse_number10(s,v);
}

static void init_val_num(asm_value_t* r, uint16_t n, asm_vop_t op){
    r->is_label=0; r->label[0]=0; r->num=n; r->op=op; r->force_zp=0;
}

static void init_val_lab(asm_value_t* r, const char* name, asm_vop_t op){
    size_t L = strlen(name); if(L>47) L=47;
    r->is_label=1; strncpy(r->label,name,L); r->label[L]=0; r->num=0; r->op=op; r->force_zp=0;
}
static void parse_value_out(const char* tok, asm_value_t* out){
    asm_vop_t op;
    uint16_t v;
    out->force_zp = 0;
    if(!tok || !*tok){ init_val_num(out, 0, V_NORMAL); return; }
    if(*tok=='*'){
        out->force_zp = 1;
        ++tok;
        while(*tok==' '||*tok=='\t') ++tok;
        if(!*tok){ init_val_num(out, 0, V_NORMAL); return; }
    }
    op = V_NORMAL;
    if(tok[0]=='<' || tok[0]=='>'){ op = (tok[0]=='<')?V_LOW:V_HIGH; tok++; }
    if(parse_number(tok,&v)){ init_val_num(out, v, op); return; }
    init_val_lab(out, tok, op);
}
static uint16_t apply_vop(uint16_t v, asm_vop_t op){
    if(op==V_LOW)  return (uint16_t)(v & 0xFF);
    if(op==V_HIGH) return (uint16_t)((v>>8) & 0xFF);
    return v;
}
static int value_fits_zp(const asm_value_t* a){
    uint16_t v;
    int idx;
    if(!a) return 0;
    if(a->is_label){
        idx = find_sym(a->label);
        if(idx < 0 || !xram_sym_is_defined((unsigned)idx)) return 0;
        v = xram_sym_get_value((unsigned)idx);
    }else{
        v = a->num;
    }
    v = apply_vop(v, a->op);
    return v <= 0xFFu;
}
static uint16_t resolve_value(const asm_value_t* a){
    uint16_t base;
    int idx;
    if(!a->is_label) base = a->num;
    else{
        idx = find_sym(a->label);
        if(idx < 0 || !xram_sym_is_defined((unsigned)idx)){ printf("Unidentified label: %s\n", a->label); base=0; }
        else base = xram_sym_get_value((unsigned)idx);
    }
    return apply_vop(base, a->op);
}

static const opdef_t* find_op(const char* m){
    const opdef_t* p = ops;
    while(p->name){
        if(strcmp(p->name,m)==0) return p;
        ++p;
    }
    return 0;
}

/* --- tokenizacja --- */
static int split_token(char* s, char** a, char** b){
    char* p = s;
    ltrim_ptr(&p);
    if(!*p){ *a=*b=0; return 0; }
    *a = p;
    while(*p && !isspace((unsigned char)*p)) ++p;
    if(*p){ *p = '\0'; ++p; }
    ltrim_ptr(&p);
    *b = (*p)?p:0;
    return 1;
}

/* --- operand -> tryb --- */
static asm_mode_t parse_operand_mode(const char* s, asm_value_t* val){
    const char* q;
    int len;
    char inner[80];
    const char* comma;
    int force_zp = 0;

    if(!s || !*s) return M_IMP;
    while(*s==' '||*s=='\t') s++;

    /* prefiks '*' wymusza wybór trybów ZP (ZP/ZPX/ZPY/INDZP) także dla etykiet */
    if(*s=='*'){
        force_zp = 1;
        ++s;
        while(*s==' '||*s=='\t') s++;
    }

    if((s[0]=='A'||s[0]=='a') && !isalnum((unsigned char)s[1])) return M_ACC;

    if(*s=='#'){ parse_value_out(s+1, val); val->force_zp = 0; return M_IMM; }

    if(*s=='('){
        q = strchr(s,')'); if(!q) return M_IMP;
        len = (int)(q-(s+1)); if(len<=0) return M_IMP;
        if(len>79) len=79;
        memcpy(inner,s+1,len); inner[len]=0;

        if(q[1]==',' && (q[2]=='Y'||q[2]=='y')){ parse_value_out(inner, val); val->force_zp = 0; return M_ZPINDY; }
        if(q[1]==',' && (q[2]=='X'||q[2]=='x')){ parse_value_out(inner, val); val->force_zp = 0; return M_ABSINDX; }
        if(len>2 && inner[len-2]==',' && (inner[len-1]=='X'||inner[len-1]=='x')){
            inner[len-2]=0; parse_value_out(inner, val); val->force_zp = 0;
            if(value_fits_zp(val)) return M_ZPINDX;
            return M_ABSINDX;
        }
        parse_value_out(inner, val);
        val->force_zp = (unsigned)force_zp;
        if(force_zp) return M_ZPIND;
        if(value_fits_zp(val)) return M_ZPIND;
        return M_ABSIND;
    }

    comma = strchr(s,',');
    if(comma){
        char base[80];
        int k = (int)(comma-s); if(k>79) k=79;
        memcpy(base,s,k); base[k]=0;
        parse_value_out(base, val);
        if(comma[1]=='X'||comma[1]=='x'){
            val->force_zp = (unsigned)force_zp;
            if(force_zp) return M_ZPX;
            if(value_fits_zp(val)) return M_ZPX;
            return M_ABSX;
        }
        if(comma[1]=='Y'||comma[1]=='y'){
            val->force_zp = (unsigned)force_zp;
            if(force_zp) return M_ZPY;
            if(value_fits_zp(val)) return M_ZPY;
            return M_ABSY;
        }
        val->force_zp = 0;
        return M_ABS;
    }else{
        parse_value_out(s, val);
        val->force_zp = (unsigned)force_zp;
        if(force_zp) return M_ZP;
        if(value_fits_zp(val)) return M_ZP;
        return M_ABS;
    }
}

/* --- I/O plików i .include --- */
static int read_file_into_lines(const char* path, int depth){
    FILE* f;
    char* s; char *dir,*rest; const char* q;

    if(depth > MAXINCDEPTH){ printf("Too deep .include" NEWLINE); return 0; }
    f = fopen(path,"rb");
    if(!f){ printf("Can't open: %s" NEWLINE, path); return 0; }

    while(fgets(g_buf,sizeof(g_buf),f)){
        rstrip(g_buf);
        strip_utf8_bom(g_buf);
        strncpy(g_buf2, g_buf, sizeof(g_buf2)-1); g_buf2[sizeof(g_buf2)-1]=0;
        trim_comment(g_buf2);
        s = g_buf2; ltrim_ptr(&s);
        if(s[0]=='.'){
            if(split_token(s,&dir,&rest)){
                to_upper_str(dir);
                if(strcmp(dir,".INCLUDE")==0 && rest && rest[0]=='"'){
                    q = strrchr(rest,'"');
                    if(q && q>rest){
                        int k = (int)(q-(rest+1)); if(k>255) k=255;
                        memcpy(g_incpath,rest+1,k); g_incpath[k]=0;
                        read_file_into_lines(g_incpath, depth+1);
                        continue;
                    }
                }
            }
        }
        add_line(g_buf);
    }
    fclose(f);
    return 1;
}

/*
static char hex_digit(unsigned char v)
{
    v &= 0x0F;
    return (v < 10) ? (char)('0' + v) : (char)('A' + (v - 10));
}
*/

// Zwraca wskaźnik do przygotowanego bufora (outfilebuffer).
// Opcjonalnie zwraca długość (bez '\0') przez out_len.
// Jeśli bufor był za mały, ucina wynik.
/*
static char* build_outfilebuffer(unsigned int* out_len, uint16_t line_pc_before, uint16_t line_pc_after)
{
    unsigned int a; 
    unsigned int out_off;
    unsigned char byte;
    unsigned int pos;
    unsigned int max;

    pos = 0;
    max = (unsigned int)sizeof(outfilebuffer);

    for (a = line_pc_before; a < line_pc_after; ++a)
    {
        // 3 znaki na bajt: "HH " + 1 na końcowe '\0'
        if (pos + 3u + 1u > max) {
            break;
        }

        out_off = (unsigned int)(a - org);
        byte = (unsigned char)xram_out_read_byte(out_off);

        outfilebuffer[pos++] = hex_digit((unsigned char)(byte >> 4));
        outfilebuffer[pos++] = hex_digit(byte);
        outfilebuffer[pos++] = ' ';
    }

    // usuń końcową spację (opcjonalnie)
    if (pos > 0 && outfilebuffer[pos - 1] == ' ') {
        --pos;
    }

    outfilebuffer[pos] = '\0';

    if (out_len != 0) {
        *out_len = pos;
    }
    return outfilebuffer;
}
*/

// --- PASS1 ---
static void pass1(void){
    int li;
    org = 0xFFFF; pc = 0x0000;

    for(li=0; li<nlines; ++li){
        char *p; char label[48]; int L;
        char *eq_name, *eq_val;

        xram_line_read((unsigned)li, g_buf);
        trim_comment(g_buf);
        g_s = g_buf; ltrim_ptr(&g_s); if(!*g_s) continue;

        /* etykieta */
        if(is_ident_start((unsigned char)*g_s)){
            p = g_s; L=0;
            while(is_ident_char((unsigned char)*p) && L<47){ label[L++]=*p++; }
            label[L]=0;
            if(*p==':'){
                if(org==0xFFFF) org=pc;
                add_or_update_sym(label, pc, 1);
                g_s=p+1; ltrim_ptr(&g_s);
                if(!*g_s) continue;
            }
        }

        /* stała w stylu: NAME .equ value */
            if(parse_named_equ_line(g_s, &eq_name, &eq_val)){
                parse_value_out(eq_val, &g_val);
                if(g_val.is_label){
                    int idx = find_sym(g_val.label);
                    if(idx<0 || !xram_sym_is_defined((unsigned)idx)){
                        assembly_status |= STAT_PASS1_ERROR;
                        printf("PASS1: ERROR .equ unknown symbol %s" NEWLINE, g_val.label);
                    } else { 
                        add_or_update_sym(eq_name, apply_vop(xram_sym_get_value((unsigned)idx), g_val.op), 1);
                    }
                } else {
                    add_or_update_sym(eq_name, apply_vop(g_val.num, g_val.op), 1);
                }
                continue;
            }

        if(*g_s=='.'){
            if(!split_token(g_s,&g_dir,&g_rest)) continue;
            to_upper_str(g_dir);

            if(strcmp(g_dir,".ORG")==0){
                parse_value_out(g_rest, &g_val);
                if(g_val.is_label){ 
                    assembly_status |= STAT_PASS1_ERROR;
                    printf("PASS1: ERROR Label at .org unattended" NEWLINE);
                    continue;
                }
                pc = g_val.num; if(org==0xFFFF) org=pc;

            } else if(strcmp(g_dir,".BYTE")==0){
                char* pr = g_rest;
                if(org==0xFFFF) org=pc;
                while(pr && *pr){
                    int i=0;
                    while(*pr==' '||*pr=='\t') ++pr;
                    while(*pr && *pr!=',' && i<79){ g_tok[i++]=*pr++; }
                    g_tok[i]=0; if(*pr==',') ++pr;
                    if(g_tok[0]) pc += 1;
                }

            } else if(strcmp(g_dir,".WORD")==0){
                char* pr = g_rest;
                if(org==0xFFFF) org=pc;
                while(pr && *pr){
                    int i=0;
                    while(*pr==' '||*pr=='\t') ++pr;
                    while(*pr && *pr!=',' && i<79){ g_tok[i++]=*pr++; }
                    g_tok[i]=0; if(*pr==',') ++pr;
                    if(g_tok[0]) pc += 2;
                }

            } else if(strcmp(g_dir,".ASCII")==0){
                int nbytes = 0;
                if(org==0xFFFF) org=pc;
                if(!parse_ascii_bytes(g_rest, (uint8_t*)g_tok, (int)sizeof(g_tok), &nbytes)){
                    assembly_status |= STAT_PASS1_ERROR;
                    printf("PASS1: ERROR syntax .ascii: .ascii \"text\"" NEWLINE);
                } else {
                    pc = (uint16_t)(pc + (uint16_t)nbytes);
                }

            } else if(strcmp(g_dir,".ASCIZ")==0 || strcmp(g_dir,".ASCIIZ")==0){
                int nbytes = 0;
                if(org==0xFFFF) org=pc;
                if(!parse_ascii_bytes(g_rest, (uint8_t*)g_tok, (int)sizeof(g_tok), &nbytes)){
                    assembly_status |= STAT_PASS1_ERROR;
                    printf("PASS1: ERROR syntax .asciz: .asciz \"text\"" NEWLINE);
                } else {
                    pc = (uint16_t)(pc + (uint16_t)nbytes + 1u);
                }

            }else if(strcmp(g_dir,".EQU")==0){
                char *t1,*t2;
                if(split_token(g_rest,&t1,&t2) && t1 && t2){
                    parse_value_out(t2, &g_val);
                    if(g_val.is_label){
                        int idx = find_sym(g_val.label);
                        if(idx<0 || !xram_sym_is_defined((unsigned)idx)){
                            assembly_status |= STAT_PASS1_ERROR;
                            printf("PASS1: ERROR .EQU unknown symbol %s" NEWLINE, g_val.label);
                        } else {
                            add_or_update_sym(t1, apply_vop(xram_sym_get_value((unsigned)idx), g_val.op), 1);
                        }
                    } else {
                        add_or_update_sym(t1, apply_vop(g_val.num, g_val.op), 1);
                    }
                } else {
                    printf("PASS1: INFO Syntax .EQU: NAME .EQU value" NEWLINE);
                }
            }
            continue;
        }

        if(!split_token(g_s,&g_mn,&g_op)) continue;
        strncpy(g_MN,g_mn,7); g_MN[7]=0; to_upper_str(g_MN);
        g_def = find_op(g_MN);
        if(!g_def){
            assembly_status |= STAT_PASS1_ERROR;
            printf("PASS1: ERROR unknown mnemonic at line %d: %s" NEWLINE, li+1, g_MN); continue; 
        }

        parse_value_out("", &g_val); /* init */
        g_mode = parse_operand_mode(g_op,&g_val);
        if(is_branch_mnemonic(g_MN)){
            parse_value_out(g_op, &g_val);
            g_mode = M_PCREL;
        }
        {
            int opt_mode = map_mode_to_op(g_mode, g_MN);
            int opc = -1;
            int cnt = g_def->count;
            int i;
            for(i=0;i<cnt;i++){
                if(g_def->vars[i].mode == opt_mode){ opc = g_def->vars[i].opcode; break; }
            }
            if(opc < 0){ 
                assembly_status |= STAT_PASS1_ERROR;
                printf("PASS1: ERROR unattended mode %s at line %i" NEWLINE, g_MN, li);
                continue; 
            }
        }
        /* długości instrukcji liczone jak wcześniej po lokalnym g_mode */

        if(org==0xFFFF) org=pc;
        if(g_mode==M_IMP || g_mode==M_ACC) pc+=1;
        else if(g_mode==M_IMM || g_mode==M_ZP || g_mode==M_ZPX || g_mode==M_ZPY || g_mode==M_ZPINDX || g_mode==M_ZPINDY || g_mode==M_ZPIND || g_mode==M_PCREL) pc+=2;
        else pc+=3;
    }
}

// --- PASS2 ---
static void emit_at(uint8_t b, uint16_t at){
    uint16_t off;
    if(at < org){
        assembly_status |= STAT_PASS2_ERROR;
        printf("PASS2: ERROR write before .ORG at $%04X" NEWLINE, at);
        return;
    }
    off = (uint16_t)(at - org);
    if(off >= MAXOUT){
        assembly_status |= STAT_PASS2_ERROR;
        printf("PASS2: ERROR output overflow at $%04X" NEWLINE, at);
        return;
    }
    xram_out_write_byte(off, b);
}

static void pass2(void){
    int li,i,opt_mode;
    int fd,n;

    fd = open(g_listpath, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if(fd < 0){
        printf("Writing error (open %s)" NEWLINE, g_listpath);
        return;
    }

    // clean out buffer
    xram1_fill(XRAM_OUT_BASE, 0x00, (unsigned)MAXOUT);

    pc = org;
    for(li = 0; li < nlines; ++li){
        line_pc_before = pc;

        xram_line_read((unsigned)li, g_buf2);
        strncpy(g_buf, g_buf2, MAXLEN-1); g_buf[MAXLEN-1]=0;
        trim_comment(g_buf);
        g_s = g_buf; ltrim_ptr(&g_s);
        if(!*g_s){
            // list remarks and empty lines
            n = sprintf(outfilebuffer, li == 0 ? "                                   | %s" : NEWLINE "                                   | %s", g_buf2);
            if(n < 0) continue;
            if(n >= (int)sizeof(outfilebuffer)) n = (int)sizeof(outfilebuffer) - 1;
            xram_write_lst_line(outfilebuffer, (unsigned)n, fd);
            continue;
        } 

        if(is_ident_start((unsigned char)*g_s)){
            char* p = g_s;
            while(is_ident_char((unsigned char)*p)) ++p;
            if(*p==':'){
                g_s = p + 1; ltrim_ptr(&g_s);
                if(!*g_s){
                    n = sprintf(outfilebuffer, NEWLINE "                                   | %s", g_buf2);
                    if(n < 0) continue;
                    if(n >= (int)sizeof(outfilebuffer)) n = (int)sizeof(outfilebuffer) - 1;
                    xram_write_lst_line(outfilebuffer, (unsigned)n, fd);
                    continue;
                } 
            }
        }

        // pomiń definicje stałych: NAME .equ value
        if(parse_named_equ_line(g_s, &eq_name, &eq_val)) goto list_line;

        if(*g_s=='.'){
            if(split_token(g_s,&g_dir,&g_rest)){
                to_upper_str(g_dir);
                if(strcmp(g_dir,".ORG") == 0){
                    parse_value_out(g_rest, &g_val); pc=resolve_value(&g_val);
                }else if(strcmp(g_dir,".BYTE") == 0){
                    char* pr = g_rest;
                    while(pr && *pr){
                        int i=0;
                        while(*pr==' '||*pr=='\t') ++pr;
                        while(*pr && *pr!=',' && i < 79){ g_tok[i++]=*pr++; }
                        g_tok[i]=0; if(*pr==',') ++pr;
                        if(g_tok[0]){
                            uint16_t v16;
                            parse_value_out(g_tok, &g_val);
                            v16 = resolve_value(&g_val);
                            if(g_val.force_zp && v16 > 0xFF){
                                assembly_status |= STAT_PASS2_ERROR;
                                printf("PASS2: ERROR forced byte truncates $%04X at line %d" NEWLINE, v16, li+1);
                            }else if(!g_val.force_zp && g_val.op == V_NORMAL && v16 > 0xFF){
                                assembly_status |= STAT_PASS2_ERROR;
                                printf("PASS2: ERROR .byte truncates $%04X at line %d (use < or >)" NEWLINE, v16, li+1);
                            }
                            emit_at((uint8_t)v16, pc++);
                        }
                    }
                }else if(strcmp(g_dir,".WORD")==0){
                    char* pr = g_rest;
                    while(pr && *pr){
                        int i=0;
                        while(*pr==' ' || *pr=='\t') ++pr;
                        while(*pr && *pr != ',' && i < 79){ g_tok[i++]=*pr++; }
                        g_tok[i] = 0; if(*pr==',') ++pr;
                        if(g_tok[0]){
                            uint16_t v16;
                            parse_value_out(g_tok, &g_val);
                            v16 = resolve_value(&g_val);
                            emit_at((uint8_t)(v16 & 0xFF), pc++);
                            emit_at((uint8_t)(v16 >> 8),   pc++);
                        }
                    }
                }else if(strcmp(g_dir, ".ASCII")==0){
                    uint8_t bytes[80];
                    int nbytes = 0;
                    int i;
                    if(!parse_ascii_bytes(g_rest, bytes, (int)sizeof(bytes), &nbytes)){
                        assembly_status |= STAT_PASS2_ERROR;
                        printf("PASS2: ERROR Syntax .ascii: .ascii \"text\"" NEWLINE);
                    }else{
                        for(i=0;i<nbytes;i++) emit_at(bytes[i], pc++);
                    }
                }else if(strcmp(g_dir,".ASCIZ")==0 || strcmp(g_dir,".ASCIIZ")==0){
                    uint8_t bytes[80];
                    int nbytes = 0;
                    int i;
                    if(!parse_ascii_bytes(g_rest, bytes, (int)sizeof(bytes), &nbytes)){
                        assembly_status |= STAT_PASS2_ERROR;
                        printf("PASS2: ERROR Syntax .asciz: .asciz \"text\"" NEWLINE);
                    }else{
                        for(i=0;i<nbytes;i++) emit_at(bytes[i], pc++);
                        emit_at(0x00, pc++);
                    }
                }
            }
        } else {
            
            if(!split_token(g_s,&g_mn,&g_op)) goto list_line;

            strncpy(g_MN,g_mn,7); g_MN[7]=0; to_upper_str(g_MN);
            g_def = find_op(g_MN);
            if(!g_def) goto list_line;

            parse_value_out("", &g_val); // init
            g_mode   = parse_operand_mode(g_op,&g_val);
            if(is_branch_mnemonic(g_MN)){
                parse_value_out(g_op, &g_val);
                g_mode = M_PCREL;
            }

            opt_mode = map_mode_to_op(g_mode, g_MN);
            g_opcode = -1;
            for(i=0;i< g_def->count; i++){
                if(g_def->vars[i].mode == opt_mode){
                    g_opcode = g_def->vars[i].opcode;
                    break;
                }
            }
            if(g_opcode < 0) goto list_line; // no opcode in this mode

            if(g_opcode>=0){
                emit_at((uint8_t)g_opcode, pc++);
                if(g_mode==M_IMP || g_mode==M_ACC){
                }else if(g_mode==M_IMM || g_mode==M_ZP || g_mode==M_ZPX || g_mode==M_ZPY || g_mode==M_ZPINDX || g_mode==M_ZPINDY || g_mode==M_ZPIND){
                    uint16_t v16 = resolve_value(&g_val);
                    if(g_val.force_zp && (g_mode==M_ZP || g_mode==M_ZPX || g_mode==M_ZPY || g_mode==M_ZPIND) && v16 > 0xFF){
                        assembly_status |= STAT_PASS2_ERROR;
                        printf("PASS2: ERROR forced ZP truncates $%04X at line %d" NEWLINE, v16, li + 1);
                    }
                    emit_at((uint8_t)v16, pc++);
                }else if(g_mode==M_ABS || g_mode==M_ABSX || g_mode==M_ABSY || g_mode==M_ABSIND){
                    uint16_t v16 = resolve_value(&g_val);
                    emit_at((uint8_t)(v16 & 0xFF), pc++);
                    emit_at((uint8_t)(v16 >> 8),   pc++);
                }else if(g_mode==M_PCREL){
                    uint16_t target = resolve_value(&g_val);
                    int16_t off = (int16_t)target - (int16_t)(pc + 1);
                    if(off < -128 || off > 127){
                        assembly_status |= STAT_PASS2_ERROR;
                        printf("PASS2: ERROR branch out of range at line %d" NEWLINE, li + 1);
                        off = 0;
                    }
                    emit_at((uint8_t)(off & 0xFF), pc++);
                }
            }
        }
list_line:
        line_pc_after = pc;

        // list pc
        n = sprintf(outfilebuffer, NEWLINE "%04X ", line_pc_before);
        if(n < 0) continue;
        if(n >= (int)sizeof(outfilebuffer)) n = (int)sizeof(outfilebuffer) - 1;
        xram_write_lst_line(outfilebuffer, (unsigned)n, fd);

        // list machine codes
        for(a = line_pc_before; a < (line_pc_before + 10); ++a)
        {
            if(a < line_pc_after){
                uint16_t out_off = (uint16_t)(a - org);
                uint8_t byte = xram_out_read_byte(out_off);
                n = sprintf(outfilebuffer, "%02X \0", byte);
            } else {
                n = sprintf(outfilebuffer, "%s\0", "   ");
            }
            if(n < 0) continue;
            if(n >= (int)sizeof(outfilebuffer)) n = (int)sizeof(outfilebuffer) - 1;
            xram_write_lst_line(outfilebuffer, (unsigned)n, fd);
        }
        // p = build_outfilebuffer(&len, line_pc_before, line_pc_after);
        // xram_write_lst_line(p, (unsigned)len, fd);

        /*
        pad = 36 - 3 * (line_pc_after - line_pc_before);
        if(pad < 0) pad = 0;
        while(pad--) {
            n = sprintf(outfilebuffer, "%s", " ");
            if(n < 0) continue;
            if(n >= (int)sizeof(outfilebuffer)) n = (int)sizeof(outfilebuffer) - 1;
            xram_write_lst_line(outfilebuffer, (unsigned)n, fd);
        }
        */

        // list source line for current pc
        n = sprintf(outfilebuffer, "| %s", g_buf2);
        if(n < 0) continue;
        if(n >= (int)sizeof(outfilebuffer)) n = (int)sizeof(outfilebuffer) - 1;
        xram_write_lst_line(outfilebuffer, (unsigned)n, fd);
    }
    close(fd);
}

/* --- zapis BIN --- */
static void save_bin(void){
    unsigned len;
    len = (pc > org) ? (unsigned)(pc - org) : 0;
    printf("ORG=$%04X, size=%u bytes" NEWLINE, org, len);
    if(len > 0 && !assembly_status){
        int fd = open(g_outpath, O_WRONLY | O_CREAT | O_TRUNC, 0666);
        if(fd < 0){
            printf("Writing error (open %s)" NEWLINE, g_outpath);
            return;
        }
        /* dane już są w XRAM -> write_xram nie potrzebuje kopiowania do RAM */
        if(write_xram((unsigned)XRAM_OUT_BASE, len, fd) < 0){
            printf("Writing error (write_xram)" NEWLINE);
        }else{
            printf(NEWLINE "Save in %s" NEWLINE, g_outpath);
        }
        close(fd);
    } else {
        printf(NEWLINE "There are nothing to save." NEWLINE);
    }
}

/* --- main: stdin + .include --- */
int main(int argc, char **argv){
    char *s,*dir,*rest; const char* q;
    int i;
    int loaded_from_file = 0;
    const char* input_path = 0;

    printf(CSI_RESET APP_MSG_TITLE);

    /*
    printf("List of parameters" NEWLINE "--------------" NEWLINE "argc=%d" NEWLINE, argc);
    for(i = 0; i < argc; i++) {
        printf("argv[%d]=\"%s\"" NEWLINE, i, argv[i]);
    }
    */
   
    for(i = 0; i < argc; i++){
        if(strcmp(argv[i], "-o") == 0){
            if((i + 1) < argc && argv[i + 1] && argv[i + 1][0]){
                set_output_path(argv[i + 1]);
                ++i;
            }else{
                printf("Missing output filename after -o" NEWLINE);
            }
            continue;
        }
        if(!input_path && argv[i] && argv[i][0]) input_path = argv[i];
    }

    nsym = 0;
    nlines = 0;
    org = 0x9000;
    pc  = 0x9000;
    xram_sym_clear_all();

    assembly_status = STAT_SUCCESS;

    if(input_path){
        FILE* src = fopen(input_path, "rb");
        if(!src){
            printf("Can't open source file %s" NEWLINE, input_path);
        }else{
            printf(APP_MSG_START_ASSEMBLING NEWLINE);
            printf("Open source file %s" NEWLINE, input_path);
            while(nlines < MAXLINES && fgets(g_buf,sizeof(g_buf),src)){
                rstrip(g_buf);
                strip_utf8_bom(g_buf);
                strncpy(g_buf2, g_buf, sizeof(g_buf2)-1); g_buf2[sizeof(g_buf2)-1]=0;
                trim_comment(g_buf2);
                s = g_buf2; ltrim_ptr(&s);
                if(s[0]=='.' && split_token(s,&dir,&rest)){
                    to_upper_str(dir);
                    if(strcmp(dir,".INCLUDE")==0 && rest && rest[0]=='"'){
                        q = strrchr(rest,'"');
                        if(q && q>rest){
                            int k = (int)(q-(rest+1)); if(k>255) k=255;
                            memcpy(g_incpath,rest+1,k); g_incpath[k]=0;
                            read_file_into_lines(g_incpath, 1);
                            continue;
                        }
                    }
                }
                add_line(g_buf);
            }
            fclose(src);
            loaded_from_file = 1;
        }
    }

    // manually entering code
    if(!loaded_from_file){
        printf(APP_MSG_START_ENTERCODE NEWLINE);
        while(nlines < MAXLINES){
            if(!fgets(g_buf,sizeof(g_buf),stdin)) break;
            if(g_buf[0]=='\n'||g_buf[0]==0) break;
            rstrip(g_buf);
            strip_utf8_bom(g_buf);

            strncpy(g_buf2, g_buf, sizeof(g_buf2)-1); g_buf2[sizeof(g_buf2)-1]=0;
            trim_comment(g_buf2);
            s = g_buf2; ltrim_ptr(&s);
            if(s[0]=='.' && split_token(s,&dir,&rest)){
                to_upper_str(dir);
                if(strcmp(dir,".INCLUDE")==0 && rest && rest[0]=='\"'){
                    q = strrchr(rest,'\"');
                    if(q && q>rest){
                        int k = (int)(q-(rest+1)); if(k>255) k=255;
                        memcpy(g_incpath,rest+1,k); g_incpath[k]=0;
                        read_file_into_lines(g_incpath, 1);
                        continue;
                    }
                }
            }
            add_line(g_buf);
        }
    }

    pass1();
    if(assembly_status != STAT_SUCCESS){
        printf(ANSI_RED "PASS1: ERRORS !" ANSI_RESET NEWLINE);
    } else {
        printf(ANSI_GREEN "PASS1: SUCCESS" ANSI_RESET NEWLINE);
    }
    pass2();
    if(assembly_status != STAT_SUCCESS){
        printf(ANSI_RED "PASS2: ERRORS !" ANSI_RESET NEWLINE);
    } else {
        printf(ANSI_GREEN "PASS2: SUCCESS" ANSI_RESET NEWLINE);
    }
    if(assembly_status == STAT_SUCCESS) save_bin();
    printf(NEWLINE);
    return 0;

}
