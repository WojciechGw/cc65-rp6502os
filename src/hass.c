/* 
 * @file      ext-hass.c
 * @author    Wojciech Gwioździk
 * @copyright 2026 (c) Wojciech Gwioździk
 *
 * Handy ASSembler WDC65C02 for Rumbledethumps' Picocomputer 6502
 * 
 */

#include "commons.h"
#include "hass-opcodes.h"

#define APPVER "20260408.1854"

#define APPDIRDEFAULT "MSC0:/"
#define APP_MSG_TITLE CSI_RESET CSI "2;1H" CSI HIGHLIGHT_COLOR " razemOS > " ANSI_RESET " Handy ASSembler WDC65C02S" ANSI_DARK_GRAY CSI "2;60Hversion " APPVER ANSI_RESET
#define APP_MSG_START_ASSEMBLING ANSI_DARK_GRAY CSI "4;1HStart assembling ... " ANSI_RESET
#define APP_MSG_START_ENTERCODE ANSI_DARK_GRAY CSI "4;1HType @HELP for a list of commands, or start coding." ANSI_RESET

/* --- limits --- */
#define MAXLINES    512
#define MAXLEN      50
#define MAXOUT      16384u
#define MAXSYM      128
#define MAXINCDEPTH 4

#define HASS_LAST_SOURCE_CODE_BUFFER_FILE "hass.backup"
#define HASS_DEFAULT_OUT_BIN_FILE "hass-out.bin\0"
#define HASS_DEFAULT_OUT_LST_FILE "hass-out.lst\0"

#define ANSI_RESETNEWLINEx2 ANSI_RESET NEWLINE NEWLINE
#define prn_err(text)  printf(NEWLINE ANSI_RED EXCLAMATION text ANSI_RESETNEWLINEx2)
#define prn_ok(text)   printf(NEWLINE ANSI_GREEN  text ANSI_RESETNEWLINEx2)
#define prn_warn(text) printf(NEWLINE ANSI_YELLOW text ANSI_RESETNEWLINEx2)
#define prn_inf(text)  printf(NEWLINE ANSI_CYAN text ANSI_RESETNEWLINEx2)

void *__fastcall__ argv_mem(size_t size) { return malloc(size); }

/* --- input buffer --- */
static int   nlines = 0;

/* --- default .org address (if .org not defined in source) --- */
static uint16_t org = 0x8000;
static uint16_t pc  = 0x8000;

/* common work buffers */
static char g_buf[MAXLEN];
static char g_buf2[MAXLEN];
static char g_buf3[MAXLEN];
static char outfilebuffer[128];
static char g_tok[80];
static char g_incpath[256];
static char g_symtmp[48];
static char g_outpath[128] = HASS_DEFAULT_OUT_BIN_FILE;
static char g_listpath[128] = HASS_DEFAULT_OUT_LST_FILE;
static uint16_t line_pc_before;
static uint16_t line_pc_after;
static char *eq_name, *eq_val;
static uint16_t a;

#define STAT_SUCCESS        0b00000000
#define STAT_PASS1_ERROR    0b00000001
#define STAT_PASS2_ERROR    0b00000010
#define STAT_PRE_INCLUDE    0b00000100

static unsigned int assembly_status;

static uint32_t g_cycle_count = 0u;   /* accumulated during pass2        */
static uint16_t g_cycle_from  = 0u;   /* first line for @CYCLES counting */
static uint16_t g_cycle_to    = 0xFFFFu; /* last line (0xFFFF = all)     */

/* --- storage in XRAM (RIA port 1) --- */
#define XRAM_LINES_BASE 0x0000u
#define XRAM_LINES_SIZE 0x6400u  /* 512*50 = 25600 bytes */
#define XRAM_OUT_BASE   0x6400u
#define XRAM_OUT_SIZE   0x4000u  /* 16384 bytes */
#define XRAM_LST_BASE   0xA400u
#define XRAM_LST_SIZE   0x80u

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
#define XRAM_SYM_BASE   0xA480u  /* after LST: 0xA400+0x80; 128*64=8192 bytes -> 0xC480 */
#define XRAM_SYM_STRIDE 64u
#define XRAM_SYM_SIZE   ((unsigned)MAXSYM * (unsigned)XRAM_SYM_STRIDE)

static unsigned xram_sym_addr(unsigned idx){
    return (unsigned)(XRAM_SYM_BASE + idx * (unsigned)XRAM_SYM_STRIDE);
}

static void xram1_fill(unsigned addr, uint8_t value, unsigned len); /* forward */

static void xram_sym_clear_all(void){
    /* XRAM zeroing not needed: find_sym() gates on sym_first[] (CPU RAM).
       After nsym=0 + memset, pass1 always writes before any read. */
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
    int       addend;    /* arithmetic offset: label+N or label-N */
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

/*
static void xram1_read_bytes(unsigned addr, uint8_t* dst, unsigned len){
    RIA.addr1 = addr;
    RIA.step1 = 1;
    while(len--) *dst++ = RIA.rw1;
}
*/

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
    uint8_t i;
    uint8_t ch;
    if(!dst) return;
    RIA.addr1 = addr;
    RIA.step1 = 1;
    for(i = 0; i < (uint8_t)MAXLEN; i++){
        ch = RIA.rw1;
        dst[i] = (char)ch;
        if(ch == 0) break;
    }
    dst[MAXLEN-1] = 0;
}

static void xram_out_write_byte(uint16_t off, uint8_t b){
    RIA.addr1 = (unsigned)(XRAM_OUT_BASE + off);
    RIA.rw1 = b;
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
    if(nsym>=MAXSYM){ prn_warn("Too many symbols"); exit(1); }
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
    if(nlines >= MAXLINES){ prn_warn("Too many rows"); exit(1); }
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
    r->is_label=0; r->label[0]=0; r->num=n; r->op=op; r->force_zp=0; r->addend=0;
}

static void init_val_lab(asm_value_t* r, const char* name, asm_vop_t op){
    size_t L = strlen(name); if(L>47) L=47;
    r->is_label=1; strncpy(r->label,name,L); r->label[L]=0; r->num=0; r->op=op; r->force_zp=0; r->addend=0;
}
static void parse_value_out(const char* tok, asm_value_t* out){
    asm_vop_t op;
    uint16_t v;
    uint16_t av;
    const char* p;
    const char* op_ptr;
    char base_buf[48];
    size_t base_len;
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
    /* look for arithmetic operator after first character (label+N or label-N) */
    op_ptr = NULL;
    p = tok + 1;
    while(*p){ if(*p=='+' || *p=='-'){ op_ptr = p; break; } ++p; }
    if(op_ptr){
        base_len = (size_t)(op_ptr - tok);
        if(base_len > 47) base_len = 47;
        strncpy(base_buf, tok, base_len);
        base_buf[base_len] = 0;
        if(parse_number(base_buf, &v)) init_val_num(out, v, op);
        else init_val_lab(out, base_buf, op);
        av = 0;
        if(parse_number(op_ptr + 1, &av))
            out->addend = (*op_ptr == '-') ? -(int)av : (int)av;
        else
            out->addend = 0;
    } else {
        if(parse_number(tok,&v)){ init_val_num(out, v, op); return; }
        init_val_lab(out, tok, op);
    }
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
    v = (uint16_t)((int)v + a->addend);
    v = apply_vop(v, a->op);
    return v <= 0xFFu;
}
static uint16_t resolve_value(const asm_value_t* a){
    uint16_t base;
    int idx;
    if(!a->is_label) base = a->num;
    else{
        idx = find_sym(a->label);
        if(idx < 0 || !xram_sym_is_defined((unsigned)idx)){ printf(NEWLINE ANSI_RED EXCLAMATION "Unidentified label: %s" ANSI_RESETNEWLINEx2, a->label); base=0; }
        else base = xram_sym_get_value((unsigned)idx);
    }
    base = (uint16_t)((int)base + a->addend);
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
static int read_file_into_lines(const char* path, int depth, int from_line){
    FILE* f;
    char* s; char *dir,*rest; const char* q;
    int line_idx;

    if(depth > MAXINCDEPTH){ prn_err("Too deep .include"); return 0; }
    f = fopen(path,"rb");
    if(!f){ printf(NEWLINE ANSI_RED EXCLAMATION "Can't open: %s" ANSI_RESETNEWLINEx2, path); return 0; }

    line_idx = 0;
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
                        read_file_into_lines(g_incpath, depth+1, 0);
                        continue;
                    }
                }
            }
        }
        if(line_idx >= from_line)
            add_line(g_buf);
        line_idx++;
    }
    fclose(f);
    return 1;
}

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
                    printf(NEWLINE ANSI_RED EXCLAMATION "PASS1 .equ unknown symbol %s" ANSI_RESETNEWLINEx2, g_val.label);
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
                    prn_err("PASS1 label at .org unattended");
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
                    prn_err("PASS1 syntax .ascii: .ascii \"text\"");
                } else {
                    pc = (uint16_t)(pc + (uint16_t)nbytes);
                }

            } else if(strcmp(g_dir,".ASCIZ")==0 || strcmp(g_dir,".ASCIIZ")==0){
                int nbytes = 0;
                if(org==0xFFFF) org=pc;
                if(!parse_ascii_bytes(g_rest, (uint8_t*)g_tok, (int)sizeof(g_tok), &nbytes)){
                    assembly_status |= STAT_PASS1_ERROR;
                    prn_err("PASS1 syntax .asciz: .asciz \"text\"");
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
                            printf(NEWLINE ANSI_RED EXCLAMATION "PASS1 .EQU unknown symbol %s" ANSI_RESETNEWLINEx2, g_val.label);
                        } else {
                            add_or_update_sym(t1, apply_vop(xram_sym_get_value((unsigned)idx), g_val.op), 1);
                        }
                    } else {
                        add_or_update_sym(t1, apply_vop(g_val.num, g_val.op), 1);
                    }
                } else {
                    prn_ok("PASS1 Syntax .EQU: NAME .EQU value");
                }
            }
            continue;
        }

        if(!split_token(g_s,&g_mn,&g_op)) continue;
        strncpy(g_MN,g_mn,7); g_MN[7]=0; to_upper_str(g_MN);
        g_def = find_op(g_MN);
        if(!g_def){
            assembly_status |= STAT_PASS1_ERROR;
            printf(NEWLINE ANSI_RED EXCLAMATION "PASS1 unknown mnemonic at line %d: %s" ANSI_RESETNEWLINEx2, li+1, g_MN); continue; 
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
                printf(NEWLINE ANSI_RED EXCLAMATION "PASS1 unattended mode %s at line %i" ANSI_RESETNEWLINEx2, g_MN, li);
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

    // clean out buffer — only the range [org..pc) computed by pass1
    {
        unsigned out_size = (pc > org) ? (unsigned)(pc - org) : 0u;
        if(out_size > (unsigned)MAXOUT) out_size = (unsigned)MAXOUT;
        if(out_size > 0u) xram1_fill(XRAM_OUT_BASE, 0x00, out_size);
    }
    g_cycle_count = 0u;

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

        // omit constants definitions: "NAME .equ value" — quick dot pre-check
        if(strchr(g_s, '.') && parse_named_equ_line(g_s, &eq_name, &eq_val)) goto list_line;

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
                if((uint16_t)li >= g_cycle_from &&
                   (g_cycle_to == 0xFFFFu || (uint16_t)li <= g_cycle_to))
                    g_cycle_count += op_cycles[(uint8_t)g_opcode];
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

        // build full listing line in one buffer, then one write_xram call
        {
            int pos;
            uint8_t lstb;
            /* prefix: newline + 4-digit PC + space */
            n = sprintf(outfilebuffer, NEWLINE "%04X ", line_pc_before);
            pos = (n > 0) ? n : 0;
            /* machine code columns — set up portal once for sequential reads */
            if(line_pc_after > line_pc_before){
                RIA.addr1 = (unsigned)(XRAM_OUT_BASE + (unsigned)(line_pc_before - org));
                RIA.step1 = 1;
            }
            for(a = line_pc_before; a < (line_pc_before + 10); ++a){
                if(a < line_pc_after){
                    lstb = RIA.rw1;
                    n = sprintf(outfilebuffer + pos, "%02X ", lstb);
                } else {
                    n = sprintf(outfilebuffer + pos, "   ");
                }
                if(n > 0) pos += n;
            }
            /* source line */
            n = sprintf(outfilebuffer + pos, "| %s", g_buf2);
            if(n > 0) pos += n;
            xram_write_lst_line(outfilebuffer, (unsigned)pos, fd);
        }
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
            prn_err("@MAKE Writing error (can't open a file).");
            return;
        }
        if(write_xram((unsigned)XRAM_OUT_BASE, len, fd) < 0){
            prn_err("@MAKE Writing error (write_xram).");
        }else{
            printf(NEWLINE ANSI_GREEN "@MAKE File %s saved." ANSI_RESETNEWLINEx2, g_outpath);
        }
        close(fd);
    } else {
        prn_warn("@MAKE There are nothing to save.");
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
    int from, to, i, page;
    const char *r;
    char pause_buf[4];
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
    page = 0;
    for(i = from; i <= to; i++){
        xram_line_read((unsigned)(i-1), g_buf);
        printf(ANSI_DARK_GRAY "%4d | " ANSI_RESET "%s" NEWLINE, i, g_buf);
        if(++page == 28 && i < to){
            page = 0;
            printf("--- more --- [Enter] continue, [q] quit: ");
            if(!fgets(pause_buf, sizeof(pause_buf), stdin)) break;
            if(pause_buf[0]=='q' || pause_buf[0]=='Q') break;
        }
    }
    if(nlines == 0) prn_warn("@LIST (buffer is empty)");
}

/* --- @EDIT N text --- */
static void cmd_edit(const char *args){
    int n;
    const char *text;
    if(!args || !parse_line_number(args, &n, &text)){
        prn_inf("@EDIT usage: @EDIT N new text"); return;
    }
    if(n < 1 || n > nlines){
        printf(NEWLINE ANSI_RED EXCLAMATION "@EDIT line %d out of range (1..%d)" ANSI_RESETNEWLINEx2, n, nlines); return;
    }
    xram_line_write((unsigned)(n-1), text);
    printf(NEWLINE ANSI_GREEN "@EDIT line %d replaced" ANSI_RESETNEWLINEx2, n);
}

/* --- @DEL N [M] --- */
static void cmd_del(const char *args){
    int from, to, count, i;
    const char *r;
    if(!args || !parse_line_number(args, &from, &r)){
        prn_inf("@DEL usage: @DEL N [M]"); return;
    }
    if(!parse_line_number(r, &to, &r)) to = from;
    if(from > to){ int tmp = from; from = to; to = tmp; }
    if(from < 1 || to > nlines){
        printf(NEWLINE ANSI_RED EXCLAMATION "@DEL range %d..%d out of range (1..%d)" ANSI_RESETNEWLINEx2, from, to, nlines); return;
    }
    count = to - from + 1;
    for(i = from - 1; i < nlines - count; i++){
        xram_line_read((unsigned)(i + count), g_buf);
        xram_line_write((unsigned)i, g_buf);
    }
    for(i = nlines - count; i < nlines; i++) xram_line_write((unsigned)i, "");
    nlines -= count;
    if(count == 1)
        printf(NEWLINE ANSI_GREEN "@DEL line %d deleted, %d lines remain" ANSI_RESETNEWLINEx2, from, nlines);
    else
        printf(NEWLINE ANSI_GREEN "@DEL lines %d..%d deleted (%d lines), %d lines remain" ANSI_RESETNEWLINEx2, from, to, count, nlines);
}

/* --- @INS N text --- */
static void cmd_ins(const char *args){
    int n, i;
    const char *text;
    char text_save[MAXLEN];
    if(!args || !parse_line_number(args, &n, &text)){
        prn_inf("@INS usage: @INS N text"); return;
    }
    if(n < 1 || n > nlines+1){
        printf(NEWLINE ANSI_RED EXCLAMATION "@INS line %d out of range (1..%d)" ANSI_RESETNEWLINEx2, n, nlines+1); return;
    }
    if(nlines >= MAXLINES){
        printf(NEWLINE ANSI_RED EXCLAMATION "@INS buffer full (%d lines)" ANSI_RESETNEWLINEx2, MAXLINES); return;
    }
    strncpy(text_save, text ? text : "", MAXLEN-1);
    text_save[MAXLEN-1] = 0;
    for(i = nlines-1; i >= n-1; i--){
        xram_line_read((unsigned)i, g_buf);
        xram_line_write((unsigned)(i+1), g_buf);
    }
    xram_line_write((unsigned)(n-1), text_save);
    nlines++;
    printf(NEWLINE ANSI_GREEN "@INS inserted at line %d, %d lines total" ANSI_RESETNEWLINEx2, n, nlines);
}

/* --- save entered source lines to a text file --- */
static void save_source_lines(const char *filename) {
    int fd, i;
    unsigned len;
    uint8_t j;

    if(nlines == 0) {
        prn_warn("@SAVE There are nothing to save (buffer is empty)");
    } else {

        if (!filename || !filename[0]) {
            prn_warn("@SAVE missing filename");
            return;
        }

        fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0666);
        if (fd < 0) {
            printf(NEWLINE ANSI_RED EXCLAMATION "@SAVE cannot create %s" ANSI_RESETNEWLINEx2, filename);
            return;
        }
        for (i = 0; i < nlines; i++) {
            xram_line_read((unsigned)i, g_buf);
            len = (unsigned)strlen(g_buf);
            RIA.addr0 = XRAM_LST_BASE;
            RIA.step0 = 1;
            for (j = 0u; j < (uint8_t)len; j++) RIA.rw0 = (uint8_t)g_buf[j];
            RIA.rw0 = (uint8_t)'\n';
            write_xram(XRAM_LST_BASE, len + 1u, fd);
        }
        close(fd);
        printf(NEWLINE ANSI_GREEN "@SAVE %d lines -> %s" ANSI_RESETNEWLINEx2, nlines, filename);
    }
}

/* ================================================================
 * 65C02 SOFTWARE TRACER (@TRACE)
 * Memory: ZP + stack in CPU RAM; code from XRAM_OUT_BASE (read-only)
 * ================================================================ */
static uint8_t  vm_zp[256];
static uint8_t  vm_stk[256];
static uint8_t  vm_a, vm_x, vm_y, vm_sp, vm_p;
static uint16_t vm_pc;
static uint16_t vm_code_start;
static uint16_t vm_code_end;

#define VM_N (vm_p & 0x80u)
#define VM_V (vm_p & 0x40u)
#define VM_D (vm_p & 0x08u)
#define VM_C (vm_p & 0x01u)

/* set/clear flag fl based on cond */
#define VM_SF(fl,cond) do { if(cond) vm_p|=(uint8_t)(fl); else vm_p&=(uint8_t)~(uint8_t)(fl); } while(0)
/* update N and Z flags from byte value v */
#define VM_NZ(v) do { uint8_t _v=(uint8_t)(v); VM_SF(0x80u,_v&0x80u); VM_SF(0x02u,!_v); } while(0)

static uint8_t vm_read(uint16_t addr){
    if(addr < 0x0100u) return vm_zp[(uint8_t)addr];
    if(addr < 0x0200u) return vm_stk[(uint8_t)addr];
    if(addr >= vm_code_start && addr < vm_code_end){
        RIA.addr1 = (unsigned)(XRAM_OUT_BASE + (unsigned)(addr - vm_code_start));
        return RIA.rw1;
    }
    return 0xFFu;
}
static void vm_write(uint16_t addr, uint8_t val){
    if(addr < 0x0100u) vm_zp[(uint8_t)addr] = val;
    else if(addr < 0x0200u) vm_stk[(uint8_t)addr] = val;
    else if(addr >= vm_code_start && addr < vm_code_end){
        RIA.addr1 = (unsigned)(XRAM_OUT_BASE + (unsigned)(addr - vm_code_start));
        RIA.rw1 = val;
    }
}

/* stack */
static void    vm_push  (uint8_t v)  { vm_stk[vm_sp--] = v; }
static uint8_t vm_pop   (void)       { return vm_stk[++vm_sp]; }
static void    vm_push16(uint16_t v) { vm_push((uint8_t)(v>>8)); vm_push((uint8_t)v); }
static uint16_t vm_pop16(void)       { uint8_t lo=vm_pop(); return (uint16_t)lo|(uint16_t)((uint16_t)vm_pop()<<8); }

/* addressing mode effective address helpers — advance vm_pc */
static uint8_t  vm_imm    (void){ return vm_read(vm_pc++); }
static uint16_t vm_ea_zp  (void){ return (uint16_t)vm_read(vm_pc++); }
static uint16_t vm_ea_zpx (void){ return (uint16_t)(uint8_t)(vm_read(vm_pc++)+vm_x); }
static uint16_t vm_ea_zpy (void){ return (uint16_t)(uint8_t)(vm_read(vm_pc++)+vm_y); }
static uint16_t vm_ea_abs (void){ uint16_t lo=vm_read(vm_pc++); return lo|(uint16_t)((uint16_t)vm_read(vm_pc++)<<8); }
static uint16_t vm_ea_absx(void){ return (uint16_t)(vm_ea_abs()+(uint16_t)vm_x); }
static uint16_t vm_ea_absy(void){ return (uint16_t)(vm_ea_abs()+(uint16_t)vm_y); }
static uint16_t vm_ea_zpindx(void){
    uint8_t zp=(uint8_t)(vm_read(vm_pc++)+vm_x);
    return (uint16_t)vm_zp[zp]|(uint16_t)((uint16_t)vm_zp[(uint8_t)(zp+1u)]<<8);
}
static uint16_t vm_ea_zpindy(void){
    uint8_t zp=vm_read(vm_pc++);
    uint16_t base=(uint16_t)vm_zp[zp]|(uint16_t)((uint16_t)vm_zp[(uint8_t)(zp+1u)]<<8);
    return (uint16_t)(base+(uint16_t)vm_y);
}
static uint16_t vm_ea_zpind(void){
    uint8_t zp=vm_read(vm_pc++);
    return (uint16_t)vm_zp[zp]|(uint16_t)((uint16_t)vm_zp[(uint8_t)(zp+1u)]<<8);
}

/* arithmetic helpers */
static void vm_adc(uint8_t op){
    uint8_t c=VM_C?1u:0u;
    if(VM_D){
        uint8_t lo=(uint8_t)((vm_a&0x0Fu)+(op&0x0Fu)+c);
        uint8_t hi=(uint8_t)((vm_a>>4)+(op>>4));
        if(lo>9u){lo=(uint8_t)(lo-10u);hi++;}
        if(hi>9u){hi=(uint8_t)(hi-10u);vm_p|=0x01u;}else vm_p&=(uint8_t)~0x01u;
        vm_a=(uint8_t)((hi<<4)|(lo&0x0Fu)); VM_NZ(vm_a); vm_p&=(uint8_t)~0x40u;
    } else {
        uint16_t r=(uint16_t)vm_a+(uint16_t)op+(uint16_t)c;
        uint8_t res=(uint8_t)r;
        VM_SF(0x40u,!((vm_a^op)&0x80u)&&((vm_a^res)&0x80u));
        VM_SF(0x01u,r>0xFFu); vm_a=res; VM_NZ(vm_a);
    }
}
static void vm_sbc(uint8_t op){
    if(VM_D){
        uint8_t borrow=VM_C?0u:1u;
        int lo=(int)(vm_a&0x0Fu)-(int)(op&0x0Fu)-(int)borrow;
        int hi=(int)(vm_a>>4)-(int)(op>>4);
        if(lo<0){lo+=10;hi--;}
        if(hi<0){hi+=10;vm_p&=(uint8_t)~0x01u;}else vm_p|=0x01u;
        vm_a=(uint8_t)(((uint8_t)hi<<4)|((uint8_t)lo&0x0Fu)); VM_NZ(vm_a); vm_p&=(uint8_t)~0x40u;
    } else { vm_adc((uint8_t)~op); }
}
static void vm_cmp_op(uint8_t reg, uint8_t op){
    uint16_t r=(uint16_t)reg-(uint16_t)op;
    VM_SF(0x80u,r&0x80u); VM_SF(0x02u,!(uint8_t)r); VM_SF(0x01u,reg>=op);
}
static uint8_t vm_asl(uint8_t v){ VM_SF(0x01u,v&0x80u); v=(uint8_t)(v<<1); VM_NZ(v); return v; }
static uint8_t vm_lsr(uint8_t v){ VM_SF(0x01u,v&0x01u); v>>=1; VM_NZ(v); return v; }
static uint8_t vm_rol(uint8_t v){ uint8_t c=VM_C?1u:0u; VM_SF(0x01u,v&0x80u); v=(uint8_t)((v<<1)|c); VM_NZ(v); return v; }
static uint8_t vm_ror(uint8_t v){ uint8_t c=VM_C?0x80u:0u; VM_SF(0x01u,v&0x01u); v=(uint8_t)((v>>1)|c); VM_NZ(v); return v; }
static uint8_t vm_inc(uint8_t v){ v++; VM_NZ(v); return v; }
static uint8_t vm_dec(uint8_t v){ v--; VM_NZ(v); return v; }
static void    vm_bit(uint8_t v, uint8_t imm){ VM_SF(0x02u,!(v&vm_a)); if(!imm){ VM_SF(0x80u,v&0x80u); VM_SF(0x40u,v&0x40u); } }
static uint8_t vm_trb(uint8_t v){ VM_SF(0x02u,!(v&vm_a)); return (uint8_t)(v&~vm_a); }
static uint8_t vm_tsb(uint8_t v){ VM_SF(0x02u,!(v&vm_a)); return (uint8_t)(v|vm_a); }

/* execute one instruction; returns 0=ok 1=halt 2=illegal */
static int vm_step(void){
    uint8_t  opc, zp;
    uint16_t ea;
    int8_t   rel;
    opc=vm_read(vm_pc++);
    switch(opc){
    /* ADC */
    case 0x69: vm_adc(vm_imm()); break;
    case 0x65: vm_adc(vm_read(vm_ea_zp())); break;
    case 0x75: vm_adc(vm_read(vm_ea_zpx())); break;
    case 0x6D: vm_adc(vm_read(vm_ea_abs())); break;
    case 0x7D: vm_adc(vm_read(vm_ea_absx())); break;
    case 0x79: vm_adc(vm_read(vm_ea_absy())); break;
    case 0x61: vm_adc(vm_read(vm_ea_zpindx())); break;
    case 0x71: vm_adc(vm_read(vm_ea_zpindy())); break;
    case 0x72: vm_adc(vm_read(vm_ea_zpind())); break;
    /* AND */
    case 0x29: vm_a&=vm_imm(); VM_NZ(vm_a); break;
    case 0x25: vm_a&=vm_read(vm_ea_zp()); VM_NZ(vm_a); break;
    case 0x35: vm_a&=vm_read(vm_ea_zpx()); VM_NZ(vm_a); break;
    case 0x2D: vm_a&=vm_read(vm_ea_abs()); VM_NZ(vm_a); break;
    case 0x3D: vm_a&=vm_read(vm_ea_absx()); VM_NZ(vm_a); break;
    case 0x39: vm_a&=vm_read(vm_ea_absy()); VM_NZ(vm_a); break;
    case 0x21: vm_a&=vm_read(vm_ea_zpindx()); VM_NZ(vm_a); break;
    case 0x31: vm_a&=vm_read(vm_ea_zpindy()); VM_NZ(vm_a); break;
    case 0x32: vm_a&=vm_read(vm_ea_zpind()); VM_NZ(vm_a); break;
    /* ASL */
    case 0x0A: vm_a=vm_asl(vm_a); break;
    case 0x06: ea=vm_ea_zp();   vm_write(ea,vm_asl(vm_read(ea))); break;
    case 0x16: ea=vm_ea_zpx();  vm_write(ea,vm_asl(vm_read(ea))); break;
    case 0x0E: ea=vm_ea_abs();  vm_write(ea,vm_asl(vm_read(ea))); break;
    case 0x1E: ea=vm_ea_absx(); vm_write(ea,vm_asl(vm_read(ea))); break;
    /* BBR0-7 (3-byte: opcode zp rel) */
    case 0x0F: zp=vm_read(vm_pc++); rel=(int8_t)vm_read(vm_pc++); if(!(vm_zp[zp]&0x01u)) vm_pc=(uint16_t)(vm_pc+rel); break;
    case 0x1F: zp=vm_read(vm_pc++); rel=(int8_t)vm_read(vm_pc++); if(!(vm_zp[zp]&0x02u)) vm_pc=(uint16_t)(vm_pc+rel); break;
    case 0x2F: zp=vm_read(vm_pc++); rel=(int8_t)vm_read(vm_pc++); if(!(vm_zp[zp]&0x04u)) vm_pc=(uint16_t)(vm_pc+rel); break;
    case 0x3F: zp=vm_read(vm_pc++); rel=(int8_t)vm_read(vm_pc++); if(!(vm_zp[zp]&0x08u)) vm_pc=(uint16_t)(vm_pc+rel); break;
    case 0x4F: zp=vm_read(vm_pc++); rel=(int8_t)vm_read(vm_pc++); if(!(vm_zp[zp]&0x10u)) vm_pc=(uint16_t)(vm_pc+rel); break;
    case 0x5F: zp=vm_read(vm_pc++); rel=(int8_t)vm_read(vm_pc++); if(!(vm_zp[zp]&0x20u)) vm_pc=(uint16_t)(vm_pc+rel); break;
    case 0x6F: zp=vm_read(vm_pc++); rel=(int8_t)vm_read(vm_pc++); if(!(vm_zp[zp]&0x40u)) vm_pc=(uint16_t)(vm_pc+rel); break;
    case 0x7F: zp=vm_read(vm_pc++); rel=(int8_t)vm_read(vm_pc++); if(!(vm_zp[zp]&0x80u)) vm_pc=(uint16_t)(vm_pc+rel); break;
    /* BBS0-7 */
    case 0x8F: zp=vm_read(vm_pc++); rel=(int8_t)vm_read(vm_pc++); if(vm_zp[zp]&0x01u) vm_pc=(uint16_t)(vm_pc+rel); break;
    case 0x9F: zp=vm_read(vm_pc++); rel=(int8_t)vm_read(vm_pc++); if(vm_zp[zp]&0x02u) vm_pc=(uint16_t)(vm_pc+rel); break;
    case 0xAF: zp=vm_read(vm_pc++); rel=(int8_t)vm_read(vm_pc++); if(vm_zp[zp]&0x04u) vm_pc=(uint16_t)(vm_pc+rel); break;
    case 0xBF: zp=vm_read(vm_pc++); rel=(int8_t)vm_read(vm_pc++); if(vm_zp[zp]&0x08u) vm_pc=(uint16_t)(vm_pc+rel); break;
    case 0xCF: zp=vm_read(vm_pc++); rel=(int8_t)vm_read(vm_pc++); if(vm_zp[zp]&0x10u) vm_pc=(uint16_t)(vm_pc+rel); break;
    case 0xDF: zp=vm_read(vm_pc++); rel=(int8_t)vm_read(vm_pc++); if(vm_zp[zp]&0x20u) vm_pc=(uint16_t)(vm_pc+rel); break;
    case 0xEF: zp=vm_read(vm_pc++); rel=(int8_t)vm_read(vm_pc++); if(vm_zp[zp]&0x40u) vm_pc=(uint16_t)(vm_pc+rel); break;
    case 0xFF: zp=vm_read(vm_pc++); rel=(int8_t)vm_read(vm_pc++); if(vm_zp[zp]&0x80u) vm_pc=(uint16_t)(vm_pc+rel); break;
    /* branches */
    case 0x90: rel=(int8_t)vm_read(vm_pc++); if(!VM_C) vm_pc=(uint16_t)(vm_pc+rel); break; /* BCC */
    case 0xB0: rel=(int8_t)vm_read(vm_pc++); if(VM_C)  vm_pc=(uint16_t)(vm_pc+rel); break; /* BCS */
    case 0xF0: rel=(int8_t)vm_read(vm_pc++); if(vm_p&0x02u) vm_pc=(uint16_t)(vm_pc+rel); break; /* BEQ */
    case 0x30: rel=(int8_t)vm_read(vm_pc++); if(VM_N)  vm_pc=(uint16_t)(vm_pc+rel); break; /* BMI */
    case 0xD0: rel=(int8_t)vm_read(vm_pc++); if(!(vm_p&0x02u)) vm_pc=(uint16_t)(vm_pc+rel); break; /* BNE */
    case 0x10: rel=(int8_t)vm_read(vm_pc++); if(!VM_N) vm_pc=(uint16_t)(vm_pc+rel); break; /* BPL */
    case 0x80: rel=(int8_t)vm_read(vm_pc++); vm_pc=(uint16_t)(vm_pc+rel); break;            /* BRA */
    case 0x50: rel=(int8_t)vm_read(vm_pc++); if(!VM_V) vm_pc=(uint16_t)(vm_pc+rel); break; /* BVC */
    case 0x70: rel=(int8_t)vm_read(vm_pc++); if(VM_V)  vm_pc=(uint16_t)(vm_pc+rel); break; /* BVS */
    /* BIT */
    case 0x89: vm_bit(vm_imm(),1); break;
    case 0x24: vm_bit(vm_read(vm_ea_zp()),0); break;
    case 0x34: vm_bit(vm_read(vm_ea_zpx()),0); break;
    case 0x2C: vm_bit(vm_read(vm_ea_abs()),0); break;
    case 0x3C: vm_bit(vm_read(vm_ea_absx()),0); break;
    /* BRK */
    case 0x00: return 1;
    /* flags */
    case 0x18: vm_p&=(uint8_t)~0x01u; break; /* CLC */
    case 0xD8: vm_p&=(uint8_t)~0x08u; break; /* CLD */
    case 0x58: vm_p&=(uint8_t)~0x04u; break; /* CLI */
    case 0xB8: vm_p&=(uint8_t)~0x40u; break; /* CLV */
    case 0x38: vm_p|=0x01u; break; /* SEC */
    case 0xF8: vm_p|=0x08u; break; /* SED */
    case 0x78: vm_p|=0x04u; break; /* SEI */
    /* CMP */
    case 0xC9: vm_cmp_op(vm_a,vm_imm()); break;
    case 0xC5: vm_cmp_op(vm_a,vm_read(vm_ea_zp())); break;
    case 0xD5: vm_cmp_op(vm_a,vm_read(vm_ea_zpx())); break;
    case 0xCD: vm_cmp_op(vm_a,vm_read(vm_ea_abs())); break;
    case 0xDD: vm_cmp_op(vm_a,vm_read(vm_ea_absx())); break;
    case 0xD9: vm_cmp_op(vm_a,vm_read(vm_ea_absy())); break;
    case 0xC1: vm_cmp_op(vm_a,vm_read(vm_ea_zpindx())); break;
    case 0xD1: vm_cmp_op(vm_a,vm_read(vm_ea_zpindy())); break;
    case 0xD2: vm_cmp_op(vm_a,vm_read(vm_ea_zpind())); break;
    /* CPX */
    case 0xE0: vm_cmp_op(vm_x,vm_imm()); break;
    case 0xE4: vm_cmp_op(vm_x,vm_read(vm_ea_zp())); break;
    case 0xEC: vm_cmp_op(vm_x,vm_read(vm_ea_abs())); break;
    /* CPY */
    case 0xC0: vm_cmp_op(vm_y,vm_imm()); break;
    case 0xC4: vm_cmp_op(vm_y,vm_read(vm_ea_zp())); break;
    case 0xCC: vm_cmp_op(vm_y,vm_read(vm_ea_abs())); break;
    /* DEC */
    case 0x3A: vm_a=vm_dec(vm_a); break;
    case 0xC6: ea=vm_ea_zp();   vm_write(ea,vm_dec(vm_read(ea))); break;
    case 0xD6: ea=vm_ea_zpx();  vm_write(ea,vm_dec(vm_read(ea))); break;
    case 0xCE: ea=vm_ea_abs();  vm_write(ea,vm_dec(vm_read(ea))); break;
    case 0xDE: ea=vm_ea_absx(); vm_write(ea,vm_dec(vm_read(ea))); break;
    case 0xCA: vm_x=vm_dec(vm_x); break; /* DEX */
    case 0x88: vm_y=vm_dec(vm_y); break; /* DEY */
    /* EOR */
    case 0x49: vm_a^=vm_imm(); VM_NZ(vm_a); break;
    case 0x45: vm_a^=vm_read(vm_ea_zp()); VM_NZ(vm_a); break;
    case 0x55: vm_a^=vm_read(vm_ea_zpx()); VM_NZ(vm_a); break;
    case 0x4D: vm_a^=vm_read(vm_ea_abs()); VM_NZ(vm_a); break;
    case 0x5D: vm_a^=vm_read(vm_ea_absx()); VM_NZ(vm_a); break;
    case 0x59: vm_a^=vm_read(vm_ea_absy()); VM_NZ(vm_a); break;
    case 0x41: vm_a^=vm_read(vm_ea_zpindx()); VM_NZ(vm_a); break;
    case 0x51: vm_a^=vm_read(vm_ea_zpindy()); VM_NZ(vm_a); break;
    case 0x52: vm_a^=vm_read(vm_ea_zpind()); VM_NZ(vm_a); break;
    /* INC */
    case 0x1A: vm_a=vm_inc(vm_a); break;
    case 0xE6: ea=vm_ea_zp();   vm_write(ea,vm_inc(vm_read(ea))); break;
    case 0xF6: ea=vm_ea_zpx();  vm_write(ea,vm_inc(vm_read(ea))); break;
    case 0xEE: ea=vm_ea_abs();  vm_write(ea,vm_inc(vm_read(ea))); break;
    case 0xFE: ea=vm_ea_absx(); vm_write(ea,vm_inc(vm_read(ea))); break;
    case 0xE8: vm_x=vm_inc(vm_x); break; /* INX */
    case 0xC8: vm_y=vm_inc(vm_y); break; /* INY */
    /* JMP */
    case 0x4C: vm_pc=vm_ea_abs(); break;
    case 0x6C: { ea=vm_ea_abs(); vm_pc=(uint16_t)vm_read(ea)|(uint16_t)((uint16_t)vm_read((uint16_t)(ea+1u))<<8); break; }
    case 0x7C: { ea=(uint16_t)(vm_ea_abs()+(uint16_t)vm_x); vm_pc=(uint16_t)vm_read(ea)|(uint16_t)((uint16_t)vm_read((uint16_t)(ea+1u))<<8); break; }
    /* JSR */
    case 0x20: { uint16_t tgt=vm_ea_abs(); vm_push16((uint16_t)(vm_pc-1u)); vm_pc=tgt; break; }
    /* LDA */
    case 0xA9: vm_a=vm_imm(); VM_NZ(vm_a); break;
    case 0xA5: vm_a=vm_read(vm_ea_zp()); VM_NZ(vm_a); break;
    case 0xB5: vm_a=vm_read(vm_ea_zpx()); VM_NZ(vm_a); break;
    case 0xAD: vm_a=vm_read(vm_ea_abs()); VM_NZ(vm_a); break;
    case 0xBD: vm_a=vm_read(vm_ea_absx()); VM_NZ(vm_a); break;
    case 0xB9: vm_a=vm_read(vm_ea_absy()); VM_NZ(vm_a); break;
    case 0xA1: vm_a=vm_read(vm_ea_zpindx()); VM_NZ(vm_a); break;
    case 0xB1: vm_a=vm_read(vm_ea_zpindy()); VM_NZ(vm_a); break;
    case 0xB2: vm_a=vm_read(vm_ea_zpind()); VM_NZ(vm_a); break;
    /* LDX */
    case 0xA2: vm_x=vm_imm(); VM_NZ(vm_x); break;
    case 0xA6: vm_x=vm_read(vm_ea_zp()); VM_NZ(vm_x); break;
    case 0xB6: vm_x=vm_read(vm_ea_zpy()); VM_NZ(vm_x); break;
    case 0xAE: vm_x=vm_read(vm_ea_abs()); VM_NZ(vm_x); break;
    case 0xBE: vm_x=vm_read(vm_ea_absy()); VM_NZ(vm_x); break;
    /* LDY */
    case 0xA0: vm_y=vm_imm(); VM_NZ(vm_y); break;
    case 0xA4: vm_y=vm_read(vm_ea_zp()); VM_NZ(vm_y); break;
    case 0xB4: vm_y=vm_read(vm_ea_zpx()); VM_NZ(vm_y); break;
    case 0xAC: vm_y=vm_read(vm_ea_abs()); VM_NZ(vm_y); break;
    case 0xBC: vm_y=vm_read(vm_ea_absx()); VM_NZ(vm_y); break;
    /* LSR */
    case 0x4A: vm_a=vm_lsr(vm_a); break;
    case 0x46: ea=vm_ea_zp();   vm_write(ea,vm_lsr(vm_read(ea))); break;
    case 0x56: ea=vm_ea_zpx();  vm_write(ea,vm_lsr(vm_read(ea))); break;
    case 0x4E: ea=vm_ea_abs();  vm_write(ea,vm_lsr(vm_read(ea))); break;
    case 0x5E: ea=vm_ea_absx(); vm_write(ea,vm_lsr(vm_read(ea))); break;
    /* NOP */
    case 0xEA: break;
    /* ORA */
    case 0x09: vm_a|=vm_imm(); VM_NZ(vm_a); break;
    case 0x05: vm_a|=vm_read(vm_ea_zp()); VM_NZ(vm_a); break;
    case 0x15: vm_a|=vm_read(vm_ea_zpx()); VM_NZ(vm_a); break;
    case 0x0D: vm_a|=vm_read(vm_ea_abs()); VM_NZ(vm_a); break;
    case 0x1D: vm_a|=vm_read(vm_ea_absx()); VM_NZ(vm_a); break;
    case 0x19: vm_a|=vm_read(vm_ea_absy()); VM_NZ(vm_a); break;
    case 0x01: vm_a|=vm_read(vm_ea_zpindx()); VM_NZ(vm_a); break;
    case 0x11: vm_a|=vm_read(vm_ea_zpindy()); VM_NZ(vm_a); break;
    case 0x12: vm_a|=vm_read(vm_ea_zpind()); VM_NZ(vm_a); break;
    /* stack */
    case 0x48: vm_push(vm_a); break;
    case 0x08: vm_push((uint8_t)(vm_p|0x10u)); break;
    case 0xDA: vm_push(vm_x); break;
    case 0x5A: vm_push(vm_y); break;
    case 0x68: vm_a=vm_pop(); VM_NZ(vm_a); break;
    case 0x28: vm_p=(uint8_t)((vm_pop()&~0x10u)|0x20u); break;
    case 0xFA: vm_x=vm_pop(); VM_NZ(vm_x); break;
    case 0x7A: vm_y=vm_pop(); VM_NZ(vm_y); break;
    /* RMB0-7 */
    case 0x07: vm_zp[vm_read(vm_pc++)]&=(uint8_t)~0x01u; break;
    case 0x17: vm_zp[vm_read(vm_pc++)]&=(uint8_t)~0x02u; break;
    case 0x27: vm_zp[vm_read(vm_pc++)]&=(uint8_t)~0x04u; break;
    case 0x37: vm_zp[vm_read(vm_pc++)]&=(uint8_t)~0x08u; break;
    case 0x47: vm_zp[vm_read(vm_pc++)]&=(uint8_t)~0x10u; break;
    case 0x57: vm_zp[vm_read(vm_pc++)]&=(uint8_t)~0x20u; break;
    case 0x67: vm_zp[vm_read(vm_pc++)]&=(uint8_t)~0x40u; break;
    case 0x77: vm_zp[vm_read(vm_pc++)]&=(uint8_t)~0x80u; break;
    /* SMB0-7 */
    case 0x87: vm_zp[vm_read(vm_pc++)]|=0x01u; break;
    case 0x97: vm_zp[vm_read(vm_pc++)]|=0x02u; break;
    case 0xA7: vm_zp[vm_read(vm_pc++)]|=0x04u; break;
    case 0xB7: vm_zp[vm_read(vm_pc++)]|=0x08u; break;
    case 0xC7: vm_zp[vm_read(vm_pc++)]|=0x10u; break;
    case 0xD7: vm_zp[vm_read(vm_pc++)]|=0x20u; break;
    case 0xE7: vm_zp[vm_read(vm_pc++)]|=0x40u; break;
    case 0xF7: vm_zp[vm_read(vm_pc++)]|=0x80u; break;
    /* ROL */
    case 0x2A: vm_a=vm_rol(vm_a); break;
    case 0x26: ea=vm_ea_zp();   vm_write(ea,vm_rol(vm_read(ea))); break;
    case 0x36: ea=vm_ea_zpx();  vm_write(ea,vm_rol(vm_read(ea))); break;
    case 0x2E: ea=vm_ea_abs();  vm_write(ea,vm_rol(vm_read(ea))); break;
    case 0x3E: ea=vm_ea_absx(); vm_write(ea,vm_rol(vm_read(ea))); break;
    /* ROR */
    case 0x6A: vm_a=vm_ror(vm_a); break;
    case 0x66: ea=vm_ea_zp();   vm_write(ea,vm_ror(vm_read(ea))); break;
    case 0x76: ea=vm_ea_zpx();  vm_write(ea,vm_ror(vm_read(ea))); break;
    case 0x6E: ea=vm_ea_abs();  vm_write(ea,vm_ror(vm_read(ea))); break;
    case 0x7E: ea=vm_ea_absx(); vm_write(ea,vm_ror(vm_read(ea))); break;
    /* RTI / RTS */
    case 0x40: vm_p=(uint8_t)((vm_pop()&~0x10u)|0x20u); vm_pc=vm_pop16(); break;
    case 0x60: vm_pc=(uint16_t)(vm_pop16()+1u); break;
    /* SBC */
    case 0xE9: vm_sbc(vm_imm()); break;
    case 0xE5: vm_sbc(vm_read(vm_ea_zp())); break;
    case 0xF5: vm_sbc(vm_read(vm_ea_zpx())); break;
    case 0xED: vm_sbc(vm_read(vm_ea_abs())); break;
    case 0xFD: vm_sbc(vm_read(vm_ea_absx())); break;
    case 0xF9: vm_sbc(vm_read(vm_ea_absy())); break;
    case 0xE1: vm_sbc(vm_read(vm_ea_zpindx())); break;
    case 0xF1: vm_sbc(vm_read(vm_ea_zpindy())); break;
    case 0xF2: vm_sbc(vm_read(vm_ea_zpind())); break;
    /* STA */
    case 0x85: vm_write(vm_ea_zp(),    vm_a); break;
    case 0x95: vm_write(vm_ea_zpx(),   vm_a); break;
    case 0x8D: vm_write(vm_ea_abs(),   vm_a); break;
    case 0x9D: vm_write(vm_ea_absx(),  vm_a); break;
    case 0x99: vm_write(vm_ea_absy(),  vm_a); break;
    case 0x81: vm_write(vm_ea_zpindx(),vm_a); break;
    case 0x91: vm_write(vm_ea_zpindy(),vm_a); break;
    case 0x92: vm_write(vm_ea_zpind(), vm_a); break;
    /* STX */
    case 0x86: vm_write(vm_ea_zp(),  vm_x); break;
    case 0x96: vm_write(vm_ea_zpy(), vm_x); break;
    case 0x8E: vm_write(vm_ea_abs(), vm_x); break;
    /* STY */
    case 0x84: vm_write(vm_ea_zp(),  vm_y); break;
    case 0x94: vm_write(vm_ea_zpx(), vm_y); break;
    case 0x8C: vm_write(vm_ea_abs(), vm_y); break;
    /* STZ */
    case 0x64: vm_write(vm_ea_zp(),   0u); break;
    case 0x74: vm_write(vm_ea_zpx(),  0u); break;
    case 0x9C: vm_write(vm_ea_abs(),  0u); break;
    case 0x9E: vm_write(vm_ea_absx(), 0u); break;
    /* STP / WAI — halt */
    case 0xDB: return 1;
    case 0xCB: return 1;
    /* transfers */
    case 0xAA: vm_x=vm_a; VM_NZ(vm_x); break; /* TAX */
    case 0xA8: vm_y=vm_a; VM_NZ(vm_y); break; /* TAY */
    case 0xBA: vm_x=vm_sp; VM_NZ(vm_x); break;/* TSX */
    case 0x8A: vm_a=vm_x; VM_NZ(vm_a); break; /* TXA */
    case 0x9A: vm_sp=vm_x; break;              /* TXS */
    case 0x98: vm_a=vm_y; VM_NZ(vm_a); break; /* TYA */
    /* TRB / TSB */
    case 0x14: ea=vm_ea_zp();  vm_write(ea,vm_trb(vm_read(ea))); break;
    case 0x1C: ea=vm_ea_abs(); vm_write(ea,vm_trb(vm_read(ea))); break;
    case 0x04: ea=vm_ea_zp();  vm_write(ea,vm_tsb(vm_read(ea))); break;
    case 0x0C: ea=vm_ea_abs(); vm_write(ea,vm_tsb(vm_read(ea))); break;
    default: return 2; /* illegal */
    }
    return 0;
}

/* disassemble one instruction at addr without modifying vm_pc */
static void vm_disasm_at(uint16_t addr){
    uint8_t opc, b1, b2;
    uint16_t w;
    int8_t  rel;
    int i, j;
    const char *mn = "???";
    uint8_t mode = M_IMP;
    opc=vm_read(addr); b1=vm_read((uint16_t)(addr+1u)); b2=vm_read((uint16_t)(addr+2u));
    for(i=0; ops[i].name[0]; i++){
        for(j=0; j<ops[i].count; j++){
            if(ops[i].vars[j].opcode==opc){ mn=ops[i].name; mode=ops[i].vars[j].mode; goto disasm_done; }
        }
    }
disasm_done:
    w=(uint16_t)b1|(uint16_t)((uint16_t)b2<<8);
    rel=(int8_t)b1;
    switch(mode){
    case M_IMP:
    case M_STACK:  printf("%-4s              ", mn); break;
    case M_ACC:    printf("%-4s A             ", mn); break;
    case M_IMM:    printf("%-4s #$%02X          ", mn, b1); break;
    case M_ZP:     printf("%-4s $%02X           ", mn, b1); break;
    case M_ZPX:    printf("%-4s $%02X,X         ", mn, b1); break;
    case M_ZPY:    printf("%-4s $%02X,Y         ", mn, b1); break;
    case M_ABS:    printf("%-4s $%04X         ", mn, w); break;
    case M_ABSX:   printf("%-4s $%04X,X       ", mn, w); break;
    case M_ABSY:   printf("%-4s $%04X,Y       ", mn, w); break;
    case M_ABSIND: printf("%-4s ($%04X)       ", mn, w); break;
    case M_ABSINDX:printf("%-4s ($%04X,X)     ", mn, w); break;
    case M_ZPIND:  printf("%-4s ($%02X)         ", mn, b1); break;
    case M_ZPINDX: printf("%-4s ($%02X,X)       ", mn, b1); break;
    case M_ZPINDY: printf("%-4s ($%02X),Y       ", mn, b1); break;
    case M_PCREL:
        if((opc&0x0Fu)==0x0Fu) /* BBR/BBS: 3-byte */
            printf("%-4s $%02X,$%04X     ", mn, b1, (uint16_t)(addr+3u+(int8_t)b2));
        else
            printf("%-4s $%04X         ", mn, (uint16_t)(addr+2u+rel));
        break;
    default: printf("%-4s ???           ", mn); break;
    }
}

/* return penalty cycles for current instruction (call BEFORE vm_step) */
static uint8_t vm_penalty_cycles(void){
    uint8_t opc, b1, b2, zp;
    uint16_t base, ea;
    int8_t rel;
    uint16_t next_pc;
    int taken;
    opc = vm_read(vm_pc);
    switch(opc){
    /* branch: +1 taken, +1 more if page cross */
    case 0x90: case 0xB0: case 0xF0: case 0x30:
    case 0xD0: case 0x10: case 0x80: case 0x50: case 0x70:
        taken = 0;
        switch(opc){
        case 0x90: taken = !VM_C; break;
        case 0xB0: taken =  VM_C ? 1 : 0; break;
        case 0xF0: taken = (vm_p&0x02u)?1:0; break;
        case 0x30: taken =  VM_N ? 1 : 0; break;
        case 0xD0: taken = (vm_p&0x02u)?0:1; break;
        case 0x10: taken =  VM_N ? 0 : 1; break;
        case 0x80: taken = 1; break;
        case 0x50: taken =  VM_V ? 0 : 1; break;
        case 0x70: taken =  VM_V ? 1 : 0; break;
        }
        if(!taken) return 0u;
        rel = (int8_t)vm_read((uint16_t)(vm_pc+1u));
        next_pc = (uint16_t)(vm_pc + 2u);
        ea = (uint16_t)(next_pc + (int16_t)rel);
        return (uint8_t)(1u + (((next_pc & 0xFF00u) != (ea & 0xFF00u)) ? 1u : 0u));
    /* abs,X read ops: page crossing penalty */
    case 0x7D: case 0x3D: case 0xDD: case 0x5D:
    case 0xBD: case 0xBC: case 0x1D: case 0xFD: case 0x3C:
        b1 = vm_read((uint16_t)(vm_pc+1u));
        b2 = vm_read((uint16_t)(vm_pc+2u));
        base = (uint16_t)b1 | (uint16_t)((uint16_t)b2<<8);
        ea   = (uint16_t)(base + (uint16_t)vm_x);
        return (uint8_t)(((base & 0xFF00u) != (ea & 0xFF00u)) ? 1u : 0u);
    /* abs,Y read ops: page crossing penalty */
    case 0x79: case 0x39: case 0xD9: case 0x59:
    case 0xB9: case 0xBE: case 0x19: case 0xF9:
        b1 = vm_read((uint16_t)(vm_pc+1u));
        b2 = vm_read((uint16_t)(vm_pc+2u));
        base = (uint16_t)b1 | (uint16_t)((uint16_t)b2<<8);
        ea   = (uint16_t)(base + (uint16_t)vm_y);
        return (uint8_t)(((base & 0xFF00u) != (ea & 0xFF00u)) ? 1u : 0u);
    /* (zp),Y read ops: page crossing penalty */
    case 0x71: case 0x31: case 0xD1: case 0x51:
    case 0xB1: case 0x11: case 0xF1:
        zp = vm_read((uint16_t)(vm_pc+1u));
        base = (uint16_t)vm_zp[zp] | (uint16_t)((uint16_t)vm_zp[(uint8_t)(zp+1u)]<<8);
        ea   = (uint16_t)(base + (uint16_t)vm_y);
        return (uint8_t)(((base & 0xFF00u) != (ea & 0xFF00u)) ? 1u : 0u);
    default: return 0u;
    }
}

static void trace_print_run_result(int status, unsigned long steps, unsigned long cycles){
    printf(NEWLINE "steps: %lu  cycles: %lu" NEWLINE, steps, cycles);
    printf("A:%02X X:%02X Y:%02X SP:%02X  %c%c%c%c%c%c" NEWLINE NEWLINE,
        vm_a, vm_x, vm_y, vm_sp,
        (vm_p&0x80u)?'N':'.', (vm_p&0x40u)?'V':'.',
        (vm_p&0x08u)?'D':'.', (vm_p&0x04u)?'I':'.',
        (vm_p&0x02u)?'Z':'.', (vm_p&0x01u)?'C':'.');
    if(status==1)
        printf(ANSI_GREEN "@TRACE: halted" ANSI_RESETNEWLINEx2);
    else if(steps>=10000ul && !status)
        printf(ANSI_YELLOW "@TRACE: step limit 10000 reached" ANSI_RESETNEWLINEx2);
    else
        printf(ANSI_RED "@TRACE: illegal opcode at $%04X" ANSI_RESETNEWLINEx2, (unsigned)vm_pc);
}

static void cmd_trace(const char *args){
    char tbuf[8];
    int  status;
    unsigned long steps;
    unsigned long cycles;
    uint8_t row, col;
    int run_mode;

    if(nlines==0){ prn_warn("@TRACE nothing to assemble"); return; }

    run_mode = (args && (args[0]=='r' || args[0]=='R'));

    assembly_status=STAT_SUCCESS;
    nsym=0; xram_sym_clear_all();
    org=0x9000; pc=0x9000;
    pass1();
    if(assembly_status!=STAT_SUCCESS){ prn_err("@TRACE PASS1 error"); return; }
    {
        unsigned out_size=(pc>org)?(unsigned)(pc-org):0u;
        if(out_size>(unsigned)MAXOUT) out_size=(unsigned)MAXOUT;
        if(out_size>0u) xram1_fill(XRAM_OUT_BASE,0x00,out_size);
    }
    pass2();
    if(assembly_status!=STAT_SUCCESS){ prn_err("@TRACE PASS2 error"); return; }

    vm_code_start=org;
    vm_code_end=pc;
    memset(vm_zp, 0, sizeof(vm_zp));
    memset(vm_stk,0, sizeof(vm_stk));
    vm_a=0x00u; vm_x=0x00u; vm_y=0x00u;
    vm_sp=0xFFu; vm_p=0x20u;
    vm_pc=vm_code_start;

    if(run_mode){
        cycles=0ul;
        for(steps=0ul; steps<10000ul; ){
            cycles += op_cycles[vm_read(vm_pc)] + vm_penalty_cycles();
            status=vm_step(); steps++;
            if(status) break;
        }
        trace_print_run_result(status, steps, cycles);
        return;
    }

    printf(NEWLINE ANSI_DARK_GRAY "active keys [ENTER:step R:run till STP/BRK Z:Zero Page Q:end of tracing]" ANSI_RESETNEWLINEx2);

    for(;;){
        printf("PC:$%04X\t", (unsigned)vm_pc);
        vm_disasm_at(vm_pc);
        printf("\t%c%c%c%c%c%c A:%02X X:%02X Y:%02X SP:%02X > ",
            (vm_p&0x80u)?'N':'.', (vm_p&0x40u)?'V':'.',
            (vm_p&0x08u)?'D':'.', (vm_p&0x04u)?'I':'.',
            (vm_p&0x02u)?'Z':'.', (vm_p&0x01u)?'C':'.',
            vm_a, vm_x, vm_y, vm_sp
        );

        if(!fgets(tbuf, sizeof(tbuf), stdin)) break;

        if(tbuf[0]=='q'||tbuf[0]=='Q'){
            prn_inf("End of tracing");
            break;
        }

        if(tbuf[0]=='z'||tbuf[0]=='Z'){
            printf(NEWLINE "Zero Page :" NEWLINE NEWLINE);
            for(row=0u; row<16u; row++){
                printf("$%02X:", (unsigned)(row*16u));
                for(col=0u; col<16u; col++) printf(" %02X", vm_zp[row*16u+col]);
                printf(NEWLINE);
            }
            printf(NEWLINE);
            continue;
        }

        if(tbuf[0]=='r'||tbuf[0]=='R'){
            cycles=0ul;
            for(steps=0ul; steps<10000ul; ){
                cycles += op_cycles[vm_read(vm_pc)];
                status=vm_step(); steps++;
                if(status) break;
            }
            trace_print_run_result(status, steps, cycles);
            break;
        }

        status=vm_step();
        if(status==1){ printf(ANSI_GREEN "@TRACE: halted (BRK/STP)" ANSI_RESETNEWLINEx2); break; }
        if(status==2){ printf(ANSI_RED "@TRACE: illegal opcode $%02X" ANSI_RESETNEWLINEx2, (unsigned)vm_read((uint16_t)(vm_pc-1u))); break; }
    }
}

/* --- main: stdin + .include --- */
int main(int argc, char *argv[]){

    char *s,*dir,*rest; const char* q;
    int i;
    int loaded_from_file = 0;
    int interactive_mode = 0;
    const char* input_path = 0;

    printf(CSI_RESET APP_MSG_TITLE NEWLINE NEWLINE NEWLINE NEWLINE);

#ifdef DEBUG
    printf("argc = %d\n", argc);
    for (i = 0; i < argc; i++)
        printf("argv[%d] = %s\n", i, argv[i]);
#endif

    for(i = 1; i < argc; i++){
        if(strcmp(argv[i], "-i") == 0){ interactive_mode = 1; continue; }
        if(strcmp(argv[i], "-o") == 0){
            if(i+1 < argc && argv[i+1] && argv[i+1][0]){
                ++i;
                set_output_path(argv[i]);
            } else {
                printf(NEWLINE NEWLINE EXCLAMATION "Missing output filename after -o" NEWLINE NEWLINE);
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
            printf(NEWLINE NEWLINE ANSI_RED EXCLAMATION "Can't open source file %s" NEWLINE NEWLINE, input_path);
        }else{
            printf(APP_MSG_START_ASSEMBLING NEWLINE);
            printf(NEWLINE NEWLINE ANSI_GREEN "Open source file %s" NEWLINE NEWLINE, input_path);
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
                            read_file_into_lines(g_incpath, 1, 0);
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
        if(loaded_from_file) {
            printf(APP_MSG_START_ENTERCODE NEWLINE "Loaded %d lines from %s. Entering interactive mode ..." NEWLINE, nlines, input_path);
            cmd_list(NULL);
        } else {
            printf(APP_MSG_START_ENTERCODE NEWLINE NEWLINE);
        }
        while(nlines < MAXLINES){
            printf("?\033[1D");
            if(!fgets(g_buf,sizeof(g_buf),stdin)) break;
            rstrip(g_buf);
            strip_utf8_bom(g_buf);

            strncpy(g_buf2, g_buf, sizeof(g_buf2)-1); g_buf2[sizeof(g_buf2)-1]=0;
            trim_comment(g_buf2);
            s = g_buf2; ltrim_ptr(&s);
            if(s[0]=='@' && split_token(s,&dir,&rest)){
                to_upper_str(dir);
                if(strcmp(dir,"@HELP")==0){
                    printf(NEWLINE "List of commands:" NEWLINE
                                   "---------------------------" NEWLINE
                        "@HELP               - show this list of commands" NEWLINE
                        "@SAVE [path]file    - save buffer to file" NEWLINE
                        "@LOAD [path]file    - clear buffer and load a file" NEWLINE
                        "@APPEND file [line] - append file to buffer from line" NEWLINE
                        "@NEW                - clear the buffer" NEWLINE
                        "@LIST [from [to]]   - display buffer lines" NEWLINE
                        "@EDIT N text        - replace line N" NEWLINE
                        "@DEL N [M]          - delete line N or range N..M" NEWLINE
                        "@INS N text         - insert text in line before N" NEWLINE
                        "@MAKE [filename]    - assemble the code and save the binary" NEWLINE
                        "@TRACE [R]          - step through code; R = run immediately" NEWLINE
                        "@CYCLES [from [to]] - count CPU cycles" NEWLINE
                        "@SYMBOLS            - list assembled symbols" NEWLINE
                        "@MANUAL [en|pl] [N] - show manual; N = jump to chapter N" NEWLINE
                        "@CD [path]          - change directory (no arg = show current)" NEWLINE
                        "@DIR [path]         - list directory (size and name)" NEWLINE
                        "@EXIT               - save to " HASS_LAST_SOURCE_CODE_BUFFER_FILE " and exit" NEWLINE NEWLINE
                    );
                    continue;
                }
                if(strcmp(dir,"@SAVE")==0){
                    save_source_lines(rest ? rest : "");
                    continue;
                }
                if(strcmp(dir,"@LOAD")==0){
                    if(rest && rest[0]){
                        char load_fname[MAXLEN];
                        strncpy(load_fname, rest, MAXLEN-1);
                        load_fname[MAXLEN-1] = 0;
                        nlines = 0; nsym = 0; xram_sym_clear_all();
                        read_file_into_lines(load_fname, 0, 0);
                        printf(NEWLINE ANSI_GREEN "@LOAD %d lines loaded from %s" ANSI_RESETNEWLINEx2, nlines, load_fname);
                    } else {
                        prn_inf("@LOAD usage: @LOAD filename");
                    }
                    continue;
                }
                if(strcmp(dir,"@APPEND")==0){
                    if(rest && rest[0]){
                        char append_buf[MAXLEN];
                        char *sp;
                        int  insert_pos, before, file_count, i;
                        FILE *af;
                        strncpy(append_buf, rest, MAXLEN-1);
                        append_buf[MAXLEN-1] = 0;
                        sp = append_buf;
                        while(*sp && *sp != ' ') sp++;
                        insert_pos = -1;  /* -1 = append at end */
                        if(*sp){
                            *sp = 0; sp++;
                            while(*sp == ' ') sp++;
                            if(*sp){
                                insert_pos = atoi(sp) - 1; /* 1-based -> 0-based */
                                if(insert_pos < 0) insert_pos = 0;
                                if(insert_pos > nlines) insert_pos = nlines;
                            }
                        }
                        before = nlines;
                        if(insert_pos < 0 || insert_pos >= nlines){
                            /* append at end of buffer */
                            read_file_into_lines(append_buf, 0, 0);
                        } else {
                            /* insert: count file lines first */
                            af = fopen(append_buf, "rb");
                            if(!af){
                                printf(NEWLINE ANSI_RED EXCLAMATION "@APPEND can't open %s" ANSI_RESETNEWLINEx2, append_buf);
                                continue;
                            }
                            file_count = 0;
                            while(fgets(g_buf, sizeof(g_buf), af)) file_count++;
                            fclose(af);
                            if(nlines + file_count > MAXLINES){
                                prn_err("@APPEND buffer full");
                                continue;
                            }
                            /* shift existing lines from insert_pos downward */
                            for(i = nlines - 1; i >= insert_pos; i--){
                                xram_line_read((unsigned)i, g_buf);
                                xram_line_write((unsigned)(i + file_count), g_buf);
                            }
                            /* write file lines starting at insert_pos */
                            af = fopen(append_buf, "rb");
                            i = insert_pos;
                            while(fgets(g_buf, sizeof(g_buf), af)){
                                rstrip(g_buf);
                                strip_utf8_bom(g_buf);
                                xram_line_write((unsigned)i, g_buf);
                                i++;
                            }
                            fclose(af);
                            nlines += file_count;
                        }
                        printf(ANSI_GREEN "@APPEND %d lines added from %s" ANSI_RESETNEWLINEx2, nlines - before, append_buf);
                    } else {
                        prn_inf("@APPEND usage: @APPEND filename [startline]");
                    }
                    continue;
                }
                if(strcmp(dir,"@SYMBOLS")==0){
                    if(nsym == 0){
                        prn_warn("@SYMBOLS no symbols (run @MAKE first)");
                    } else {
                        int si;
                        for(si = 0; si < nsym; si++){
                            xram_sym_read_name((unsigned)si, g_buf);
                            printf("  %-16s = $%04X" NEWLINE,
                                   g_buf, (unsigned)xram_sym_get_value((unsigned)si));
                        }
                        printf("@SYMBOLS: %d symbol(s)" NEWLINE NEWLINE, nsym);
                    }
                    continue;
                }
                if(strcmp(dir,"@NEW")==0){
                    nlines = 0;
                    nsym = 0;
                    xram_sym_clear_all();
                    prn_ok("@NEW buffer cleared");
                    continue;
                }
                if(strcmp(dir,"@LIST")==0){
                    cmd_list(rest);
                    continue;
                }
                if(strcmp(dir,"@EDIT")==0){
                    char *raw = g_buf; ltrim_ptr(&raw);
                    while(*raw && !isspace((unsigned char)*raw)) raw++;
                    ltrim_ptr(&raw);
                    cmd_edit(raw);
                    continue;
                }
                if(strcmp(dir,"@DEL")==0){
                    cmd_del(rest);
                    continue;
                }
                if(strcmp(dir,"@INS")==0){
                    char *raw = g_buf; ltrim_ptr(&raw);
                    while(*raw && !isspace((unsigned char)*raw)) raw++;
                    ltrim_ptr(&raw);
                    cmd_ins(raw);
                    continue;
                }
                if(strcmp(dir,"@EXIT")==0){
                    if(nlines > 0) save_source_lines(HASS_LAST_SOURCE_CODE_BUFFER_FILE);
                    printf(NEWLINE);
                    return 0;
                }
                if(strcmp(dir,"@MAKE")==0){
                    if(rest && rest[0]) set_output_path(rest);
                    if(nlines == 0){
                        prn_warn("@MAKE: nothing to assemble");
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
                if(strcmp(dir,"@TRACE")==0){
                    cmd_trace(rest);
                    continue;
                }
                if(strcmp(dir,"@CYCLES")==0){
                    char *tok2;
                    g_cycle_from = 0u;
                    g_cycle_to   = 0xFFFFu;
                    if(rest && rest[0]){
                        g_cycle_from = (uint16_t)atoi(rest);
                        tok2 = rest;
                        while(*tok2 && *tok2!=' ') tok2++;
                        while(*tok2==' ') tok2++;
                        if(*tok2) g_cycle_to = (uint16_t)atoi(tok2);
                        else      g_cycle_to = g_cycle_from;
                    }
                    if(nlines == 0){
                        prn_warn("@CYCLES: nothing to count");
                    } else {
                        assembly_status = STAT_SUCCESS;
                        nsym = 0; xram_sym_clear_all();
                        org = 0x9000; pc = 0x9000;
                        pass1();
                        if(assembly_status == STAT_SUCCESS){
                            pass2();
                            if(assembly_status == STAT_SUCCESS){
                                if(g_cycle_to == 0xFFFFu)
                                    printf("@CYCLES: %lu cycles (all %d lines)" NEWLINE,
                                           (unsigned long)g_cycle_count, nlines);
                                else
                                    printf("@CYCLES: %lu cycles (lines %u-%u)" NEWLINE,
                                           (unsigned long)g_cycle_count,
                                           (unsigned)g_cycle_from, (unsigned)g_cycle_to);
                            } else {
                                prn_err("@CYCLES: PASS2 error");
                            }
                        } else {
                            prn_err("@CYCLES: PASS1 error");
                        }
                    }
                    continue;
                }
                if(strcmp(dir,"@MANUAL")==0){
                    static char mpath[32];
                    static char mbuf[82];  /* 80 chars + \n + \0 */
                    static char tok1[8];
                    static char chapprefix[8];
                    FILE *mf;
                    int page = 0;
                    int chapter = 0;
                    const char *lang = "en";
                    const char *p_rest = (rest && rest[0]) ? rest : "";
                    const char *p;
                    int ti;
                    /* parse: @MANUAL [en|pl] [N]  or  @MANUAL [N] [en|pl] */
                    p = p_rest; ti = 0;
                    while(*p && *p!=' ' && *p!='\t' && ti<7) tok1[ti++] = *p++;
                    tok1[ti] = 0;
                    while(*p==' ' || *p=='\t') p++;
                    if(tok1[0]>='1' && tok1[0]<='9'){
                        chapter = atoi(tok1);
                        if(*p) lang = p;
                    } else if(tok1[0]){
                        lang = tok1;
                        if(*p>='1' && *p<='9') chapter = atoi(p);
                    }
                    sprintf(mpath, "ROM:manual[%.2s].txt", lang);
                    mf = fopen(mpath, "rb");
                    if(!mf){
                        printf(ANSI_RED "@MANUAL: cannot open %s" ANSI_RESETNEWLINEx2, mpath);
                    } else {
                        if(chapter > 0){
                            long sep_pos = 0;
                            long line_start = 0;
                            sprintf(chapprefix, "%d.", chapter);
                            line_start = ftell(mf);
                            while(fgets(mbuf, sizeof(mbuf), mf)){
                                rstrip(mbuf);
                                if(mbuf[0]=='-' && mbuf[1]=='-') sep_pos = line_start;
                                if(strncmp(mbuf, chapprefix, strlen(chapprefix))==0){
                                    if(sep_pos) fseek(mf, sep_pos, SEEK_SET);
                                    break;
                                }
                                line_start = ftell(mf);
                            }
                        }
                        while(fgets(mbuf, sizeof(mbuf), mf)){
                            rstrip(mbuf);
                            mbuf[80] = 0;
                            printf("%s" NEWLINE, mbuf);
                            if(++page == 28){
                                page = 0;
                                printf("--- more --- [Enter] continue, [q] quit: ");
                                if(!fgets(g_buf2, sizeof(g_buf2), stdin)) break;
                                if(g_buf2[0]=='q' || g_buf2[0]=='Q') break;
                            }
                        }
                        fclose(mf);
                        printf(NEWLINE);
                    }
                    continue;
                }
                if(strcmp(dir,"@CD")==0){
                    if(rest && rest[0]){
                        if(chdir(rest) != 0)
                            printf(ANSI_RED "@CD: cannot change to %s" ANSI_RESETNEWLINEx2, rest);
                        else
                            printf(ANSI_GREEN "@CD: %s" ANSI_RESETNEWLINEx2, rest);
                    } else {
                        static char cwd[64];
                        if(f_getcwd(cwd, sizeof(cwd)) == 0)
                            printf("%s" NEWLINE NEWLINE, cwd);
                    }
                    continue;
                }
                if(strcmp(dir,"@DIR")==0){
                    const char *dpath = (rest && rest[0]) ? rest : ".";
                    int dd;
                    static f_stat_t de;
                    int count = 0;
                    /* pass 1: directories */
                    dd = f_opendir(dpath);
                    if(dd < 0){
                        prn_err("@DIR: cannot open directory");
                    } else {
                        while(f_readdir(&de, dd) == 0 && de.fname[0]){
                            if(!(de.fattrib & 0x10)) continue;
                            printf("     <DIR>  %s" NEWLINE, de.fname);
                            count++;
                        }
                        /* pass 2: files */
                        dd = f_opendir(dpath);
                        if(dd >= 0){
                            while(f_readdir(&de, dd) == 0 && de.fname[0]){
                                if(de.fattrib & 0x10) continue;
                                printf("%10lu  %s" NEWLINE, de.fsize, de.fname);
                                count++;
                            }
                        }
                        printf(NEWLINE "%d item(s)" NEWLINE NEWLINE, count);
                    }
                    continue;
                }
                printf(ANSI_RED "Error: %s unknown. Check your typing." ANSI_RESETNEWLINEx2, dir);
                continue;
            }
            if(s[0]=='.' && split_token(s,&dir,&rest)){
                to_upper_str(dir);
                if(strcmp(dir,".INCLUDE")==0 && rest && rest[0]=='\"'){
                    q = strrchr(rest,'\"');
                    if(q && q>rest){
                        int k = (int)(q-(rest+1)); if(k>255) k=255;
                        memcpy(g_incpath,rest+1,k); g_incpath[k]=0;
                        read_file_into_lines(g_incpath, 1, 0);
                        continue;
                    }
                }
            }
            add_line(g_buf);
        }
    }

    if(nlines == 0)
    {
        prn_warn("nothing to do ...");
        return -1;
    } else {
        printf(ANSI_WHITE "number of entered source code lines: ");
        printf("%d", nlines);
        printf(ANSI_RESETNEWLINEx2);
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
