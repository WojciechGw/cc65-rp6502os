/* 

Features: labels, .org, .byte, .word, .equ, <, >, .include, listing LST

TODO call parameters
mass sourcecode.asm -out outfile.bin -base <baseaddress> -run <runaddress>

*/
#include "commons.h"
#include "ext-hass-opcodes.h"

#define APPVER "20260317.1000"
#define APPDIRDEFAULT "MSC0:/"
#define APP_MSG_TITLE CSI_RESET "\x1b[2;1H\x1b" HIGHLIGHT_COLOR " OS Shell > " ANSI_RESET " Handy ASSembler WDC65C02S" ANSI_DARK_GRAY "\x1b[2;60Hversion " APPVER ANSI_RESET
#define APP_MSG_START_ASSEMBLING ANSI_DARK_GRAY "\x1b[4;1HStart compilation ... " ANSI_RESET
#define APP_MSG_START_ENTERCODE ANSI_DARK_GRAY "\x1b[4;1HEnter code. Empty line start code compilation ... " ANSI_RESET

/* --- limits --- */
#define MAXLINES    512
#define MAXLEN      50
#define MAXOUT      16384u
#define MAXSYM      128
#define MAXINCDEPTH 4

/* --- input buffer --- */
static int   nlines = 0;

/* --- default .org address (if .org not defined in source) --- */
static uint16_t org = 0x8000;
static uint16_t pc  = 0x8000;

/* common work buffers */
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

/* --- storage in XRAM (RIA port 1) --- */
#define XRAM_LINES_BASE 0x0000u
#define XRAM_LINES_SIZE 0x6400u  /* 512*50 = 25600 bytes */
#define XRAM_OUT_BASE   0x6400u
#define XRAM_OUT_SIZE   0x4000u  /* 16384 bytes */
#define XRAM_LST_BASE   0xA400u
#define XRAM_LST_SIZE   0x50u

#if (MAXLINES * MAXLEN) > XRAM_LINES_SIZE
#error "MAXLINES*MAXLEN exceeds XRAM source area"
#endif
#if MAXOUT > XRAM_OUT_SIZE
#error "MAXOUT exceeds XRAM output area"
#endif

// global variables
static char *g_s,*g_mn,*g_op,*g_dir,*g_rest;
static char  g_MN[8];
static int16_t g_opcode;
static const opdef_t* g_def;

/* Branch mnemonics have M_PCREL as sole mode; stack mnemonics have M_STACK.
   Use the opdef to detect them — no strcmp needed. */
#define opdef_is_branch(def) ((def)->vars[0].mode == M_PCREL)
#define opdef_is_stack(def)  ((def)->vars[0].mode == M_STACK)

static int map_mode_to_op(const asm_mode_t m, const opdef_t* def){
    if(m==M_IMP && def && opdef_is_stack(def)) return M_STACK;
    return (int)m;
}

/* --- symbols --- */
typedef struct {
    char     name[48];
    uint16_t value;
    unsigned defined;
} symbol_t;

static int      nsym = 0;
static char     sym_first[MAXSYM]; /* first-char cache for fast find_sym */

/* symbol table stored in XRAM */
#define XRAM_SYM_BASE   0xA450u  /* after LST: 0xA400+0x50; 128*64=8192 bytes -> 0xC450 */
#define XRAM_SYM_STRIDE 64u
#define XRAM_SYM_SIZE   ((unsigned)MAXSYM * (unsigned)XRAM_SYM_STRIDE)

static unsigned xram_sym_addr(unsigned idx){
    return (unsigned)(XRAM_SYM_BASE + idx * (unsigned)XRAM_SYM_STRIDE);
}

static void xram1_fill(unsigned addr, uint8_t value, unsigned len); /* forward */

static void xram_sym_clear_all(void){
    xram1_fill(XRAM_SYM_BASE, 0x00, XRAM_SYM_SIZE);
    memset(sym_first, 0, sizeof(sym_first));
}

static void xram_sym_read_name(unsigned idx, char* dst){
    unsigned addr = xram_sym_addr(idx);
    uint8_t i;
    if(!dst) return;
    RIA.addr1 = addr;
    RIA.step1 = 1;
    for(i=0;i<48u;i++) dst[i] = (char)RIA.rw1;
    dst[47] = 0;
}

static void xram_sym_write_name(unsigned idx, const char* name){
    unsigned addr = xram_sym_addr(idx);
    uint8_t i = 0;
    RIA.addr1 = addr;
    RIA.step1 = 1;
    if(!name) name = "";
    sym_first[idx] = name[0]; /* update first-char cache */
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
    uint8_t i;
    if(fd < 0 || !line || !len) return;
    if(len > XRAM_LST_SIZE) len = XRAM_LST_SIZE;
    RIA.addr0 = XRAM_LST_BASE;
    RIA.step0 = 1;
    for(i = 0; i < (uint8_t)len; i++) RIA.rw0 = (uint8_t)line[i];
    write_xram(XRAM_LST_BASE, len, fd);
}

/* --- values with < > --- */
typedef enum { V_NORMAL = 0, V_LOW = 1, V_HIGH = 2 } asm_vop_t;

typedef struct {
    unsigned  is_label;
    char      label[48];
    uint16_t  num;
    asm_vop_t op;
    unsigned  force_zp;
} asm_value_t;

/* --- modes i opcodes --- */
static asm_value_t g_val;
static asm_mode_t g_mode;

/* --- tools --- */
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

// split_token defined below; prototype for C89
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
    uint8_t i = 0;
    RIA.addr1 = addr;
    RIA.step1 = 1;
    if(!text) text = "";
    for(i=0;i<(uint8_t)MAXLEN;i++){
        unsigned char ch = (unsigned char)text[i];
        if(ch == 0) break;
        RIA.rw1 = (uint8_t)ch;
    }
    while(i<(uint8_t)MAXLEN){
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

/*
static uint8_t xram_out_read_byte(uint16_t off){
    RIA.addr1 = (unsigned)(XRAM_OUT_BASE + off);
    RIA.step1 = 1;
    return RIA.rw1;
}
*/

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

/* recognize syntax: NAME .equ value (return pointers to work buffer) */
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
    char c0 = name[0];
    for(i=0;i<nsym;i++){
        if(sym_first[i] != c0) continue; /* fast reject via first-char cache */
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

/* --- values' parser --- */
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
    int lo = 0;
    int hi = (int)(sizeof(ops)/sizeof(ops[0])) - 2; /* -2: skip null sentinel */
    while(lo <= hi){
        int mid = (lo + hi) >> 1;
        int c = strcmp(ops[mid].name, m);
        if(c == 0) return &ops[mid];
        if(c < 0) lo = mid + 1; else hi = mid - 1;
    }
    return 0;
}

/* --- tokenization --- */
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

/* --- operand -> mode --- */
static asm_mode_t parse_operand_mode(const char* s, asm_value_t* val){
    const char* q;
    int len;
    char inner[80];
    const char* comma;
    int force_zp = 0;

    if(!s || !*s) return M_IMP;
    while(*s==' '||*s=='\t') s++;

    /* prefix '*' force modes ZP (ZP/ZPX/ZPY/INDZP) including labels */
    if(*s=='*'){
        force_zp = 1;
        ++s;
        while(*s==' '||*s=='\t') s++;
    }

    // INC A - ACCumulator mode
    if((s[0]=='A') && !isalnum((unsigned char)s[1])) return M_ACC;

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

/* --- I/O for files --- */
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

/*
// Returns a pointer to the prepared buffer (outfilebuffer).
// Optionally returns the length (excluding the ‘\0’ terminator) via out_len.
// If the buffer was too small, truncates the result.
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

// --- assembly PASS1 ---
static void pass1(void){
    int li;
    org = 0xFFFF; pc = 0x0000;

    for(li=0; li<nlines; ++li){
        char *p; char label[48]; int L;
        char *eq_name, *eq_val;

        xram_line_read((unsigned)li, g_buf);
        trim_comment(g_buf);
        g_s = g_buf; ltrim_ptr(&g_s); if(!*g_s) continue;

        /* label: "mylabel:" */
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

        /* constant: "NAME .equ value" */
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

        if(opdef_is_branch(g_def)){
            parse_value_out(g_op, &g_val);
            g_mode = M_PCREL;
        } else {
            g_mode = parse_operand_mode(g_op,&g_val);
        }
        {
            int opt_mode = map_mode_to_op(g_mode, g_def);
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

        if(org==0xFFFF) org=pc;
        if(g_mode==M_IMP || g_mode==M_ACC) pc+=1;
        else if(g_mode==M_IMM || g_mode==M_ZP || g_mode==M_ZPX || g_mode==M_ZPY || g_mode==M_ZPINDX || g_mode==M_ZPINDY || g_mode==M_ZPIND || g_mode==M_PCREL) pc+=2;
        else pc+=3;
    }
}

// --- assembly PASS2 ---
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

static void pass2(void){ // also write listing to .lst file
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

        // omit constants definitions: "NAME .equ value"
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

            if(opdef_is_branch(g_def)){
                parse_value_out(g_op, &g_val);
                g_mode = M_PCREL;
            } else {
                g_mode = parse_operand_mode(g_op,&g_val);
            }

            opt_mode = map_mode_to_op(g_mode, g_def);
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

        // list machine codes — set up portal once for sequential reads
        if(line_pc_after > line_pc_before){
            RIA.addr1 = (unsigned)(XRAM_OUT_BASE + (unsigned)(line_pc_before - org));
            RIA.step1 = 1;
        }
        for(a = line_pc_before; a < (line_pc_before + 10); ++a)
        {
            if(a < line_pc_after){
                uint8_t byte = RIA.rw1; /* sequential read; portal already set up */
                n = sprintf(outfilebuffer, "%02X ", byte);
            } else {
                n = sprintf(outfilebuffer, "%s", "   ");
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

/* --- out machine code to .BIN --- */
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

/* --- parse leading integer from string, return pointer to remainder --- */
static int parse_line_number(const char *s, int *out_n, const char **out_rest){
    int n = 0;
    if(!s || !isdigit((unsigned char)*s)) return 0;
    while(isdigit((unsigned char)*s)){ n = n*10 + (*s - '0'); ++s; }
    while(*s==' '||*s=='\t') ++s;
    *out_n    = n;
    *out_rest = s;
    return 1;
}

/* --- @LIST [from [to]] --- */
static void cmd_list(const char *args){
    int from, to, i;
    const char *r;
    from = 1; to = nlines;
    if(args && parse_line_number(args, &from, &r)){
        if(parse_line_number(r, &to, &r)){
            /* range given */
        } else {
            to = from;   /* single line */
        }
    }
    if(from < 1) from = 1;
    if(to > nlines) to = nlines;
    for(i = from; i <= to; i++){
        xram_line_read((unsigned)(i-1), g_buf);
        printf("%4d | %s" NEWLINE, i, g_buf);
    }
    if(nlines == 0) printf("(buffer empty)" NEWLINE);
}

/* --- @EDIT N text --- */
static void cmd_edit(const char *args){
    int n;
    const char *text;
    if(!args || !parse_line_number(args, &n, &text)){
        printf("@EDIT: usage: @EDIT N new text" NEWLINE); return;
    }
    if(n < 1 || n > nlines){
        printf("@EDIT: line %d out of range (1..%d)" NEWLINE, n, nlines); return;
    }
    xram_line_write((unsigned)(n-1), text);
    printf("@EDIT: line %d replaced" NEWLINE, n);
}

/* --- @DEL N --- */
static void cmd_del(const char *args){
    int n, i;
    const char *r;
    if(!args || !parse_line_number(args, &n, &r)){
        printf("@DEL: usage: @DEL N" NEWLINE); return;
    }
    if(n < 1 || n > nlines){
        printf("@DEL: line %d out of range (1..%d)" NEWLINE, n, nlines); return;
    }
    for(i = n-1; i < nlines-1; i++){
        xram_line_read((unsigned)(i+1), g_buf);
        xram_line_write((unsigned)i, g_buf);
    }
    xram_line_write((unsigned)(nlines-1), "");
    nlines--;
    printf("@DEL: line %d deleted, %d lines remain" NEWLINE, n, nlines);
}

/* --- @INS N text --- */
static void cmd_ins(const char *args){
    int n, i;
    const char *text;
    if(!args || !parse_line_number(args, &n, &text)){
        printf("@INS: usage: @INS N text" NEWLINE); return;
    }
    if(n < 1 || n > nlines+1){
        printf("@INS: line %d out of range (1..%d)" NEWLINE, n, nlines+1); return;
    }
    if(nlines >= MAXLINES){
        printf("@INS: buffer full (%d lines)" NEWLINE, MAXLINES); return;
    }
    for(i = nlines-1; i >= n-1; i--){
        xram_line_read((unsigned)i, g_buf);
        xram_line_write((unsigned)(i+1), g_buf);
    }
    xram_line_write((unsigned)(n-1), text);
    nlines++;
    printf("@INS: inserted at line %d, %d lines total" NEWLINE, n, nlines);
}

/* --- save entered source lines to a text file --- */
static void save_source_lines(const char *filename) {
    int fd, i;
    size_t len;
    char nl;

    if (!filename || !filename[0]) {
        printf("@SAVE: missing filename" NEWLINE);
        return;
    }
    fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) {
        printf("@SAVE: cannot create %s" NEWLINE, filename);
        return;
    }
    nl = '\n';
    for (i = 0; i < nlines; i++) {
        xram_line_read((unsigned)i, g_buf);
        len = strlen(g_buf);
        if (len > 0) write(fd, g_buf, (unsigned)len);
        write(fd, &nl, 1u);
    }
    close(fd);
    printf("@SAVE: %d lines -> %s" NEWLINE, nlines, filename);
}

/* --- main: stdin + .include --- */
int main(int argc, char **argv){
    char *s,*dir,*rest; const char* q;
    int i;
    int loaded_from_file = 0;
    int interactive_mode = 0;
    const char* input_path = 0;

    printf(CSI_RESET APP_MSG_TITLE);

    /*
    printf("List of parameters" NEWLINE "--------------" NEWLINE "argc=%d" NEWLINE, argc);
    for(i = 0; i < argc; i++) {
        printf("argv[%d]=\"%s\"" NEWLINE, i, argv[i]);
    }
    */
   
    for(i = 0; i < argc; i++){
        if(strcmp(argv[i], "-i") == 0){ interactive_mode = 1; continue; }
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

    // manual entry of the code
    if(!loaded_from_file || interactive_mode){
        if(loaded_from_file)
            printf(APP_MSG_START_ENTERCODE NEWLINE "Loaded %d lines from %s. Entering interactive mode ..." NEWLINE NEWLINE, nlines, input_path);
        else
            printf(APP_MSG_START_ENTERCODE NEWLINE NEWLINE);
        while(nlines < MAXLINES){
            if(!fgets(g_buf,sizeof(g_buf),stdin)) break;
            rstrip(g_buf);
            strip_utf8_bom(g_buf);

            strncpy(g_buf2, g_buf, sizeof(g_buf2)-1); g_buf2[sizeof(g_buf2)-1]=0;
            trim_comment(g_buf2);
            s = g_buf2; ltrim_ptr(&s);
            if(s[0]=='@' && split_token(s,&dir,&rest)){
                to_upper_str(dir);
                if(strcmp(dir,"@SAVE")==0){
                    save_source_lines(rest ? rest : "");
                    continue;
                }
                if(strcmp(dir,"@LOAD")==0){
                    if(rest && rest[0]){
                        int before = nlines;
                        read_file_into_lines(rest, 0);
                        printf("@LOAD: %d lines loaded from %s" NEWLINE, nlines - before, rest);
                    } else {
                        printf("@LOAD: missing filename" NEWLINE);
                    }
                    continue;
                }
                if(strcmp(dir,"@LIST")==0){
                    cmd_list(rest);
                    continue;
                }
                if(strcmp(dir,"@EDIT")==0){
                    cmd_edit(rest);
                    continue;
                }
                if(strcmp(dir,"@DEL")==0){
                    cmd_del(rest);
                    continue;
                }
                if(strcmp(dir,"@INS")==0){
                    cmd_ins(rest);
                    continue;
                }
                if(strcmp(dir,"@EXIT")==0){
                    save_source_lines("last.hass");
                    break;
                }
                if(strcmp(dir,"@MAKE")==0){
                    if(rest && rest[0]) set_output_path(rest);
                    if(nlines == 0){
                        printf("@MAKE: nothing to compile" NEWLINE);
                    } else {
                        assembly_status = STAT_SUCCESS;
                        nsym = 0; xram_sym_clear_all();
                        org = 0x9000; pc = 0x9000;
                        pass1();
                        if(assembly_status != STAT_SUCCESS)
                            printf(ANSI_RED "PASS1: ERRORS !" ANSI_RESET NEWLINE);
                        else
                            printf(ANSI_GREEN "PASS1: SUCCESS" ANSI_RESET NEWLINE);
                        pass2();
                        if(assembly_status != STAT_SUCCESS)
                            printf(ANSI_RED "PASS2: ERRORS !" ANSI_RESET NEWLINE);
                        else
                            printf(ANSI_GREEN "PASS2: SUCCESS" ANSI_RESET NEWLINE);
                        if(assembly_status == STAT_SUCCESS) save_bin();
                    }
                    continue;
                }
            }
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

    if(nlines == 0)
    {
        printf(ANSI_RED "nothing to do ... bye, bye!" ANSI_RESET NEWLINE NEWLINE);
        return -1;
    } else {
        printf(ANSI_WHITE "number of entered source code lines: ");
        printf("%d", nlines);
        printf(ANSI_RESET NEWLINE NEWLINE);
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
}
