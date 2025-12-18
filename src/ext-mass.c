/* asm65c02.c — cc65-safe mini assembler 65C02 (C89)
   Features: labels, .org, .byte, .word, .equ, <, >, .include, listing LST
*/
#include <rp6502.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>

/* --- limity --- */
#define MAXLINES    256
#define MAXLEN      40
#define MAXOUT      8192
#define MAXSYM      64
#define MAXINCDEPTH 4

/* --- bufor wejścia --- */
static int   nlines = 0;

/* --- wyjście domyślne jeżeli brak .org --- */
static uint16_t org = 0x9A00;
static uint16_t pc  = 0x9A00;

/* wspólne bufory robocze, by ograniczyć lokalne */
static char g_buf[MAXLEN];
static char g_buf2[MAXLEN];
static char g_buf3[MAXLEN];
static char g_tok[80];
static char g_incpath[256];
static char g_symtmp[48];

/* --- magazyny w XRAM (RIA port 1) --- */
#define XRAM_LINES_BASE 0x0000u
#define XRAM_LINES_SIZE 16384u
#define XRAM_OUT_BASE   0x4000u
#define XRAM_OUT_SIZE   8192u

#if (MAXLINES * MAXLEN) > XRAM_LINES_SIZE
#error "MAXLINES*MAXLEN exceeds 16KB XRAM source area"
#endif
#if MAXOUT > XRAM_OUT_SIZE
#error "MAXOUT exceeds 8KB XRAM output area"
#endif

/* globalne robocze, by nie zużywać lokali */
static char *g_s,*g_mn,*g_op,*g_dir,*g_rest;
static char  g_MN[8];
static int16_t g_opcode;

/* --- symbole --- */
typedef struct {
    char     name[48];
    uint16_t value;
    unsigned defined; /* 0/1 */
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

/* --- wartości z operatorami < > --- */
typedef enum { V_NORMAL = 0, V_LOW = 1, V_HIGH = 2 } asm_vop_t;

typedef struct {
    unsigned  is_label;   /* 0 liczba, 1 etykieta */
    char      label[48];
    uint16_t  num;
    asm_vop_t op;
    unsigned  force_zp;   /* 1 jeśli operand miał prefiks '*' */
} asm_value_t;

/* --- tryby i opkody --- */
typedef enum {
    M_IMP, M_ACC, M_IMM, M_ZP, M_ZPX, M_ZPY, M_ABS, M_ABSX, M_ABSY,
    M_INDX, M_INDY, M_INDZP, M_INDABS, M_REL, M__COUNT
} asm_mode_t;

typedef struct {
    const char* name;
    int16_t op[M__COUNT];
} asm_opdef_t;

static const asm_opdef_t* g_def;
static asm_value_t g_val;
static asm_mode_t g_mode;

#define XXXX -1
static const asm_opdef_t ops[] = {
/*      IMP    ACC   IMM   ZP    ZPX   ZPY   ABS   ABSX  ABSY  INDX  INDY INDZP INDABS REL */
{"ADC", {XXXX, XXXX, 0x69, 0x65, 0x75, XXXX, 0x6D, 0x7D, 0x79, 0x61, 0x71, 0x72, XXXX, XXXX}}, 
{"AND", {XXXX, XXXX, 0x29, 0x25, 0x35, XXXX, 0x2D, 0x3D, 0x39, 0x21, 0x31, 0x32, XXXX, XXXX}}, 
{"ASL", {0x0A, 0x0A, XXXX, 0x06, 0x16, XXXX, 0x0E, 0x1E, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX}}, 
{"BCC", {XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, 0x90}}, 
{"BCS", {XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, 0xB0}}, 
{"BEQ", {XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, 0xF0}}, 
{"BMI", {XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, 0x30}}, 
{"BNE", {XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, 0xD0}}, 
{"BPL", {XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, 0x10}}, 
{"BRA", {XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, 0x80}}, 
{"BRK", {0x00, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX}}, 
{"BVC", {XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, 0x50}}, 
{"BVS", {XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, 0x70}}, 
{"CLC", {0x18, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX}}, 
{"CLD", {0xD8, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX}}, 
{"CLI", {0x58, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX}}, 
{"CLV", {0xB8, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX}}, 
{"CMP", {XXXX, XXXX, 0xC9, 0xC5, 0xD5, XXXX, 0xCD, 0xDD, 0xD9, 0xC1, 0xD1, 0xD2, XXXX, XXXX}}, 
{"CPX", {XXXX, XXXX, 0xE0, 0xE4, XXXX, XXXX, 0xEC, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX}}, 
{"CPY", {XXXX, XXXX, 0xC0, 0xC4, XXXX, XXXX, 0xCC, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX}}, 
{"DEC", {XXXX, XXXX, XXXX, 0xC6, 0xD6, XXXX, 0xCE, 0xDE, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX}}, 
{"EOR", {XXXX, XXXX, 0x49, 0x45, 0x55, XXXX, 0x4D, 0x5D, 0x59, 0x41, 0x51, 0x52, XXXX, XXXX}}, 
{"INC", {XXXX, XXXX, XXXX, 0xE6, 0xF6, XXXX, 0xEE, 0xFE, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX}}, 
{"INX", {0xE8, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX}}, 
{"INY", {0xC8, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX}}, 
{"JMP", {XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, 0x4C, XXXX, XXXX, XXXX, XXXX, XXXX, 0x6C, XXXX}}, 
{"JSR", {XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, 0x20, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX}}, 
{"LDA", {XXXX, XXXX, 0xA9, 0xA5, 0xB5, XXXX, 0xAD, 0xBD, 0xB9, 0xA1, 0xB1, 0xB2, XXXX, XXXX}}, 
{"LDX", {XXXX, XXXX, 0xA2, 0xA6, XXXX, 0xB6, 0xAE, 0xBE, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX}}, 
{"LDY", {XXXX, XXXX, 0xA0, 0xA4, 0xB4, XXXX, 0xAC, 0xBC, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX}}, 
{"LSR", {0x4A, 0x4A, XXXX, 0x46, 0x56, XXXX, 0x4E, 0x5E, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX}}, 
{"NOP", {0xEA, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX}}, 
{"ORA", {XXXX, XXXX, 0x09, 0x05, 0x15, XXXX, 0x0D, 0x1D, 0x19, 0x01, 0x11, 0x12, XXXX, XXXX}}, 
{"PHA", {0x48, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX}}, 
{"PHP", {0x08, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX}}, 
{"PLA", {0x68, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX}}, 
{"PLP", {0x28, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX}}, 
{"ROL", {0x2A, 0x2A, XXXX, 0x26, 0x36, XXXX, 0x2E, 0x3E, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX}}, 
{"ROR", {0x6A, 0x6A, XXXX, 0x66, 0x76, XXXX, 0x6E, 0x7E, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX}}, 
{"RTI", {0x40, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX}}, 
{"RTS", {0x60, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX}}, 
{"SBC", {XXXX, XXXX, 0xE9, 0xE5, 0xF5, XXXX, 0xED, 0xFD, 0xF9, 0xE1, 0xF1, 0xF2, XXXX, XXXX}}, 
{"SEC", {0x38, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX}}, 
{"SED", {0xF8, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX}}, 
{"SEI", {0x78, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX}}, 
{"STA", {XXXX, XXXX, XXXX, 0x85, 0x95, XXXX, 0x8D, 0x9D, 0x99, 0x81, 0x91, 0x92, XXXX, XXXX}}, 
{"STX", {XXXX, XXXX, XXXX, 0x86, XXXX, 0x96, 0x8E, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX}}, 
{"STY", {XXXX, XXXX, XXXX, 0x84, 0x94, XXXX, 0x8C, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX}}, 
{"STZ", {XXXX, XXXX, XXXX, 0x64, 0x74, XXXX, 0x9C, 0x9E, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX}}, 
{"TAX", {0xAA, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX}}, 
{"TAY", {0xA8, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX}}, 
{"TSX", {0xBA, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX}}, 
{"TXA", {0x8A, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX}}, 
{"TXS", {0x9A, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX}}, 
{"TYA", {0x98, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX, XXXX}}, 
{0, {0}}
};

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

/* split_token jest zdefiniowane niżej; prototyp potrzebny dla C89 */
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
    /* pad with NULs and guarantee terminator */
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
    /* safety: always terminate */
    dst[MAXLEN-1] = 0;
    /* (optional) stop early for consumers that assume C string anyway */
    for(i=0;i<(unsigned)(MAXLEN-1);++i){
        if(dst[i]==0) break;
    }
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
    if(*s) return 0; /* nic poza stringiem */
    if(out_len) *out_len = n;
    return 1;
}

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

static int is_branch_mnemonic(const char* mn){
    return strcmp(mn,"BRA")==0 || strcmp(mn,"BCC")==0 || strcmp(mn,"BCS")==0 ||
           strcmp(mn,"BEQ")==0 || strcmp(mn,"BMI")==0 || strcmp(mn,"BNE")==0 ||
           strcmp(mn,"BPL")==0 || strcmp(mn,"BVC")==0 || strcmp(mn,"BVS")==0 ||
           strcmp(mn,"BBR0")==0 || strcmp(mn,"BBR1")==0 || strcmp(mn,"BBR2")==0 || strcmp(mn,"BBR3")==0 ||
           strcmp(mn,"BBR4")==0 || strcmp(mn,"BBR5")==0 || strcmp(mn,"BBR6")==0 || strcmp(mn,"BBR7")==0 ||
           strcmp(mn,"BBS0")==0 || strcmp(mn,"BBS1")==0 || strcmp(mn,"BBS2")==0 || strcmp(mn,"BBS3")==0 ||
           strcmp(mn,"BBS4")==0 || strcmp(mn,"BBS5")==0 || strcmp(mn,"BBS6")==0 || strcmp(mn,"BBS7")==0;
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
    if(nsym>=MAXSYM){ printf("Too many symbols\n"); exit(1); }
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
    if(nlines >= MAXLINES){ printf("Too many rows\n"); exit(1); }
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
    if(s[0]=='$'){
        x = strtoul(s+1,&endp,16);
        if(endp==(s+1)) return 0;
        *v = (uint16_t)x;
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
static uint16_t resolve_value(const asm_value_t* a){
    uint16_t base;
    int idx;
    if(!a->is_label) base = a->num;
    else{
        idx = find_sym(a->label);
        if(idx<0 || !xram_sym_is_defined((unsigned)idx)){ printf("Unidentified label: %s\n", a->label); base=0; }
        else base = xram_sym_get_value((unsigned)idx);
    }
    return apply_vop(base, a->op);
}

/* --- szukanie opkodów --- */
static const asm_opdef_t* find_op(const char* m){
    const asm_opdef_t* p = ops;
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

        if(q[1]==',' && (q[2]=='Y'||q[2]=='y')){ parse_value_out(inner, val); val->force_zp = 0; return M_INDY; }
        if(len>2 && inner[len-2]==',' && (inner[len-1]=='X'||inner[len-1]=='x')){
            inner[len-2]=0; parse_value_out(inner, val); val->force_zp = 0; return M_INDX;
        }
        parse_value_out(inner, val);
        val->force_zp = (unsigned)force_zp;
        if(force_zp) return M_INDZP;
        if(!val->is_label && val->op==V_NORMAL && val->num<=0xFF) return M_INDZP;
        return M_INDABS;
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
            if(!val->is_label && val->op==V_NORMAL && val->num<=0xFF) return M_ZPX;
            return M_ABSX;
        }
        if(comma[1]=='Y'||comma[1]=='y'){
            val->force_zp = (unsigned)force_zp;
            if(force_zp) return M_ZPY;
            if(!val->is_label && val->op==V_NORMAL && val->num<=0xFF) return M_ZPY;
            return M_ABSY;
        }
        val->force_zp = 0;
        return M_ABS;
    }else{
        parse_value_out(s, val);
        val->force_zp = (unsigned)force_zp;
        if(force_zp) return M_ZP;
        if(!val->is_label && val->op==V_NORMAL && val->num<=0xFF) return M_ZP;
        return M_ABS;
    }
}

/* --- I/O plików i .include --- */
static int read_file_into_lines(const char* path, int depth){
    FILE* f;
    char* s; char *dir,*rest; const char* q;

    if(depth>MAXINCDEPTH){ printf("Too deep .include\n"); return 0; }
    f = fopen(path,"rb");
    if(!f){ printf("Can't open: %s\n", path); return 0; }

    while(fgets(g_buf,sizeof(g_buf),f)){
        rstrip(g_buf);
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

/* --- PASS1 --- */
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
                    if(idx<0 || !xram_sym_is_defined((unsigned)idx)){ printf("PASS1: .equ unknown symbol %s\n", g_val.label); }
                    else add_or_update_sym(eq_name, apply_vop(xram_sym_get_value((unsigned)idx), g_val.op), 1);
                }else{
                    add_or_update_sym(eq_name, apply_vop(g_val.num, g_val.op), 1);
                }
                continue;
            }

        if(*g_s=='.'){
            if(!split_token(g_s,&g_dir,&g_rest)) continue;
            to_upper_str(g_dir);

            if(strcmp(g_dir,".ORG")==0){
                parse_value_out(g_rest, &g_val);
                if(g_val.is_label){ printf("Label at .org unattended\n"); continue; }
                pc = g_val.num; if(org==0xFFFF) org=pc;

            }else if(strcmp(g_dir,".BYTE")==0){
                char* pr = g_rest;
                if(org==0xFFFF) org=pc;
                while(pr && *pr){
                    int i=0;
                    while(*pr==' '||*pr=='\t') ++pr;
                    while(*pr && *pr!=',' && i<79){ g_tok[i++]=*pr++; }
                    g_tok[i]=0; if(*pr==',') ++pr;
                    if(g_tok[0]) pc += 1;
                }

            }else if(strcmp(g_dir,".WORD")==0){
                char* pr = g_rest;
                if(org==0xFFFF) org=pc;
                while(pr && *pr){
                    int i=0;
                    while(*pr==' '||*pr=='\t') ++pr;
                    while(*pr && *pr!=',' && i<79){ g_tok[i++]=*pr++; }
                    g_tok[i]=0; if(*pr==',') ++pr;
                    if(g_tok[0]) pc += 2;
                }

            }else if(strcmp(g_dir,".ASCII")==0){
                int nbytes = 0;
                if(org==0xFFFF) org=pc;
                if(!parse_ascii_bytes(g_rest, (uint8_t*)g_tok, (int)sizeof(g_tok), &nbytes)){
                    printf("Syntax .ascii: .ascii \"text\"\n");
                }else{
                    pc = (uint16_t)(pc + (uint16_t)nbytes);
                }

            }else if(strcmp(g_dir,".ASCIZ")==0 || strcmp(g_dir,".ASCIIZ")==0){
                int nbytes = 0;
                if(org==0xFFFF) org=pc;
                if(!parse_ascii_bytes(g_rest, (uint8_t*)g_tok, (int)sizeof(g_tok), &nbytes)){
                    printf("Syntax .asciz: .asciz \"text\"\n");
                }else{
                    pc = (uint16_t)(pc + (uint16_t)nbytes + 1u);
                }

            }else if(strcmp(g_dir,".EQU")==0){
                char *t1,*t2;
                if(split_token(g_rest,&t1,&t2) && t1 && t2){
                    parse_value_out(t2, &g_val);
                    if(g_val.is_label){
                        int idx = find_sym(g_val.label);
                        if(idx<0 || !xram_sym_is_defined((unsigned)idx)){ printf("PASS1: .equ unknown symbol %s\n", g_val.label); }
                        else add_or_update_sym(t1, apply_vop(xram_sym_get_value((unsigned)idx), g_val.op), 1);
                    }else{
                        add_or_update_sym(t1, apply_vop(g_val.num, g_val.op), 1);
                    }
                }else{
                    printf("Syntax .equ: .equ NAME value\n");
                }
            }
            continue;
        }

        if(!split_token(g_s,&g_mn,&g_op)) continue;
        strncpy(g_MN,g_mn,7); g_MN[7]=0; to_upper_str(g_MN);
        g_def = find_op(g_MN);
        if(!g_def){ printf("PASS1: unknown mnemonic at line %d: %s\n", li+1, g_MN); continue; }

        parse_value_out("", &g_val); /* init */
        g_mode = parse_operand_mode(g_op,&g_val);
        if(is_branch_mnemonic(g_MN)){
            parse_value_out(g_op, &g_val);
            g_mode = M_REL;
        }
        if(g_def->op[g_mode]<0){ printf("PASS1: unattended mode %s\n", g_MN); continue; }

        if(org==0xFFFF) org=pc;
        if(g_mode==M_IMP || g_mode==M_ACC) pc+=1;
        else if(g_mode==M_IMM || g_mode==M_ZP || g_mode==M_ZPX || g_mode==M_ZPY || g_mode==M_INDX || g_mode==M_INDY || g_mode==M_INDZP || g_mode==M_REL) pc+=2;
        else pc+=3;
    }
}

/* --- PASS2 + listing --- */
static void emit_at(uint8_t b, uint16_t at){
    uint16_t off;
    if(at < org){
        printf("PASS2: write before ORG at $%04X\n", at);
        return;
    }
    off = (uint16_t)(at - org);
    if(off >= MAXOUT){
        printf("PASS2: output overflow at $%04X\n", at);
        return;
    }
    xram_out_write_byte(off, b);
}

static void pass2(void){
    int li;

    /* XRAM nie musi być wyzerowany, a .org może zostawiać dziury — czyść bufor wyjścia */
    xram1_fill(XRAM_OUT_BASE, 0x00, (unsigned)MAXOUT);

    pc = org;
    for(li = 0; li < nlines; ++li){
        uint16_t line_pc_before = pc;
        char *eq_name, *eq_val;

        xram_line_read((unsigned)li, g_buf2);
        strncpy(g_buf, g_buf2, MAXLEN-1); g_buf[MAXLEN-1]=0;
        trim_comment(g_buf);
        g_s = g_buf; ltrim_ptr(&g_s);
        if(!*g_s) continue;

        if(is_ident_start((unsigned char)*g_s)){
            char* p = g_s;
            while(is_ident_char((unsigned char)*p)) ++p;
            if(*p==':'){
                g_s=p+1; ltrim_ptr(&g_s);
                if(!*g_s) continue;
            }
        }

        /* pomiń definicje stałych: NAME .equ value */
        if(parse_named_equ_line(g_s, &eq_name, &eq_val)) goto list_line;

        if(*g_s=='.'){
            if(split_token(g_s,&g_dir,&g_rest)){
                to_upper_str(g_dir);
                if(strcmp(g_dir,".ORG")==0){
                    parse_value_out(g_rest, &g_val); pc=resolve_value(&g_val);
                }else if(strcmp(g_dir,".BYTE")==0){
                    char* pr = g_rest;
                    while(pr && *pr){
                        int i=0;
                        while(*pr==' '||*pr=='\t') ++pr;
                        while(*pr && *pr!=',' && i<79){ g_tok[i++]=*pr++; }
                        g_tok[i]=0; if(*pr==',') ++pr;
                        if(g_tok[0]){
                            uint16_t v16;
                            parse_value_out(g_tok, &g_val);
                            v16 = resolve_value(&g_val);
                            if(g_val.force_zp && v16 > 0xFF){
                                printf("PASS2: warning: forced byte truncates $%04X at line %d\n", v16, li+1);
                            }else if(!g_val.force_zp && g_val.op==V_NORMAL && v16 > 0xFF){
                                printf("PASS2: warning: .byte truncates $%04X at line %d (use < or >)\n", v16, li+1);
                            }
                            emit_at((uint8_t)v16, pc++);
                        }
                    }
                }else if(strcmp(g_dir,".WORD")==0){
                    char* pr = g_rest;
                    while(pr && *pr){
                        int i=0;
                        while(*pr==' '||*pr=='\t') ++pr;
                        while(*pr && *pr!=',' && i<79){ g_tok[i++]=*pr++; }
                        g_tok[i]=0; if(*pr==',') ++pr;
                        if(g_tok[0]){
                            uint16_t v16;
                            parse_value_out(g_tok, &g_val);
                            v16 = resolve_value(&g_val);
                            emit_at((uint8_t)(v16&0xFF), pc++);
                            emit_at((uint8_t)(v16>>8),   pc++);
                        }
                    }
                }else if(strcmp(g_dir,".ASCII")==0){
                    uint8_t bytes[80];
                    int nbytes = 0;
                    int i;
                    if(!parse_ascii_bytes(g_rest, bytes, (int)sizeof(bytes), &nbytes)){
                        printf("Syntax .ascii: .ascii \"text\"\n");
                    }else{
                        for(i=0;i<nbytes;i++) emit_at(bytes[i], pc++);
                    }
                }else if(strcmp(g_dir,".ASCIZ")==0 || strcmp(g_dir,".ASCIIZ")==0){
                    uint8_t bytes[80];
                    int nbytes = 0;
                    int i;
                    if(!parse_ascii_bytes(g_rest, bytes, (int)sizeof(bytes), &nbytes)){
                        printf("Syntax .asciz: .asciz \"text\"\n");
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

            parse_value_out("", &g_val); /* init */
            g_mode   = parse_operand_mode(g_op,&g_val);
            if(is_branch_mnemonic(g_MN)){
                parse_value_out(g_op, &g_val);
                g_mode = M_REL;
            }
            g_opcode = g_def->op[g_mode];
            if(g_opcode>=0){
                emit_at((uint8_t)g_opcode, pc++);
                if(g_mode==M_IMP || g_mode==M_ACC){
                }else if(g_mode==M_IMM || g_mode==M_ZP || g_mode==M_ZPX || g_mode==M_ZPY || g_mode==M_INDX || g_mode==M_INDY || g_mode==M_INDZP){
                    uint16_t v16 = resolve_value(&g_val);
                    if(g_val.force_zp && (g_mode==M_ZP || g_mode==M_ZPX || g_mode==M_ZPY || g_mode==M_INDZP) && v16 > 0xFF){
                        printf("PASS2: warning: forced ZP truncates $%04X at line %d\n", v16, li+1);
                    }
                    emit_at((uint8_t)v16, pc++);
                }else if(g_mode==M_ABS || g_mode==M_ABSX || g_mode==M_ABSY || g_mode==M_INDABS){
                    uint16_t v16 = resolve_value(&g_val);
                    emit_at((uint8_t)(v16&0xFF), pc++);
                    emit_at((uint8_t)(v16>>8),   pc++);
                }else if(g_mode==M_REL){
                    uint16_t target = resolve_value(&g_val);
                    int16_t off = (int16_t)target - (int16_t)(pc + 1);
                    if(off < -128 || off > 127){
                        printf("PASS2: branch out of range at line %d\n", li+1);
                        off = 0;
                    }
                    emit_at((uint8_t)(off & 0xFF), pc++);
                }
            }
        }
list_line:
        if(1) continue;
    }
}

/* --- zapis BIN --- */
static void save_bin(void){
    unsigned len;
    len = (pc > org) ? (unsigned)(pc - org) : 0;
    printf("ORG=$%04X, size=%u bytes\r\n", org, len);
    if(len > 0){
        int fd = open("out.bin", O_WRONLY | O_CREAT | O_TRUNC, 0666);
        if(fd < 0){
            printf("Writing error (open out.bin)\r\n");
            return;
        }
        /* dane już są w XRAM -> write_xram nie potrzebuje kopiowania do RAM */
        if(write_xram((unsigned)XRAM_OUT_BASE, len, fd) < 0){
            printf("Writing error (write_xram)\r\n");
        }else{
            printf("\r\nSave in out.bin\r\n");
        }
        close(fd);
    } else {
        printf("\r\nTher are nothing to save.\r\n");
    }
}

/* --- main: stdin + .include --- */
int main(int argc, char **argv){
    char *s,*dir,*rest; const char* q;
//   int i;
    int loaded_from_file = 0;

    /*
    printf("\r\n--------------\r\nargc=%d\r\n", argc);
    for(i = 0; i < argc; i++) {
        printf("argv[%d]=\"%s\"\r\n", i, argv[i]);
    }
    */

    printf("Mini ASSembler for 62C02S\r\n");
    printf("Paste the code. Empty line start compilation.\r\n");

    nsym = 0;
    nlines = 0;
    org = 0x9A00;
    pc  = 0x9A00;
    xram_sym_clear_all();

    /* jeśli podano argv[0], spróbuj wczytać plik */
    if(argc > 0 && argv[0] && argv[0][0]){
        FILE* src = fopen(argv[0], "rb");
        if(!src){
            printf("Can't open source file %s\r\n", argv[0]);
        }else{
            printf("Open source file %s\r\n", argv[0]);
            while(nlines < MAXLINES && fgets(g_buf,sizeof(g_buf),src)){
                rstrip(g_buf);
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

    /* jeśli nie wczytano z pliku, wczytuj z klawiatury jak dotąd */
    if(!loaded_from_file){
        while(nlines < MAXLINES){
            if(!fgets(g_buf,sizeof(g_buf),stdin)) break;
            if(g_buf[0]=='\n'||g_buf[0]==0) break;
            rstrip(g_buf);

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
    pass2();
    save_bin();
    printf("\r\n");
    return 0;

}
