/* asm65c02.c — cc65-safe mini assembler 65C02 (C89)
   Features: labels, .org, .byte, .word, .equ, < >, .include, listing LST
*/
#include <rp6502.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdint.h>

/* --- limity --- */
#define MAXLINES    128
#define MAXLEN      80
#define MAXOUT      8192
#define MAXSYM      50
#define MAXINCDEPTH 8

/* --- bufor wejścia --- */
static char  lines[MAXLINES][MAXLEN];
static int   nlines = 0;

/* --- wyjście --- */
static uint8_t  outbuf[MAXOUT];
static uint16_t org = 0xC000;
static uint16_t pc  = 0xC000;

/* wspólne bufory robocze, by ograniczyć lokalne */
static char g_buf[MAXLEN];
static char g_buf2[MAXLEN];
static char g_tok[80];
static char g_incpath[256];

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

static symbol_t symtab[MAXSYM];
static int      nsym = 0;

/* --- wartości z operatorami < > --- */
typedef enum { V_NORMAL = 0, V_LOW = 1, V_HIGH = 2 } asm_vop_t;

typedef struct {
    unsigned  is_label;   /* 0 liczba, 1 etykieta */
    char      label[48];
    uint16_t  num;
    asm_vop_t op;
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

#define XX -1
static const asm_opdef_t ops[] = {
/*      IMP ACC IMM  ZP   ZPX  ZPY  ABS  ABSX ABSY INDX INDY INDZP INDABS REL */
{"LDA",{XX, XX,0xA9,0xA5,0xB5,XX, 0xAD,0xBD,0xB9,0xA1,0xB1,0xB2, XX,    XX}},
{"LDX",{XX, XX,0xA2,0xA6,XX, 0xB6,0xAE,0xBE,XX,  XX,  XX,  XX,    XX,    XX}},
{"LDY",{XX, XX,0xA0,0xA4,0xB4,XX, 0xAC,0xBC,XX,  XX,  XX,  XX,    XX,    XX}},
{"STA",{XX, XX,XX,  0x85,0x95,XX, 0x8D,0x9D,0x99,0x81,0x91,0x92,  XX,    XX}},
{"STX",{XX, XX,XX,  0x86,XX, 0x96,0x8E,XX,  XX,  XX,  XX,  XX,    XX,    XX}},
{"STY",{XX, XX,XX,  0x84,0x94,XX, 0x8C,XX,  XX,  XX,  XX,  XX,    XX,    XX}},
{"STZ",{XX, XX,XX,  0x64,0x74,XX, 0x9C,0x9E,XX,  XX,  XX,  XX,    XX,    XX}},
{"ADC",{XX, XX,0x69,0x65,0x75,XX, 0x6D,0x7D,0x79,0x61,0x71,0x72,  XX,    XX}},
{"SBC",{XX, XX,0xE9,0xE5,0xF5,XX, 0xED,0xFD,0xF9,0xE1,0xF1,0xF2,  XX,    XX}},
{"AND",{XX, XX,0x29,0x25,0x35,XX, 0x2D,0x3D,0x39,0x21,0x31,0x32,  XX,    XX}},
{"ORA",{XX, XX,0x09,0x05,0x15,XX, 0x0D,0x1D,0x19,0x01,0x11,0x12,  XX,    XX}},
{"EOR",{XX, XX,0x49,0x45,0x55,XX, 0x4D,0x5D,0x59,0x41,0x51,0x52,  XX,    XX}},
{"CMP",{XX, XX,0xC9,0xC5,0xD5,XX, 0xCD,0xDD,0xD9,0xC1,0xD1,0xD2,  XX,    XX}},
{"CPX",{XX, XX,0xE0,0xE4,XX, XX,  0xEC,XX, XX,  XX,  XX,  XX,     XX,    XX}},
{"CPY",{XX, XX,0xC0,0xC4,XX, XX,  0xCC,XX, XX,  XX,  XX,  XX,     XX,    XX}},
{"INC",{XX, XX,XX,  0xE6,0xF6,XX, 0xEE,0xFE,XX,  XX,  XX,  XX,     XX,    XX}},
{"DEC",{XX, XX,XX,  0xC6,0xD6,XX, 0xCE,0xDE,XX,  XX,  XX,  XX,     XX,    XX}},
{"ASL",{0x0A,0x0A,XX,0x06,0x16,XX, 0x0E,0x1E,XX,  XX,  XX,  XX,     XX,    XX}},
{"LSR",{0x4A,0x4A,XX,0x46,0x56,XX, 0x4E,0x5E,XX,  XX,  XX,  XX,     XX,    XX}},
{"ROL",{0x2A,0x2A,XX,0x26,0x36,XX, 0x2E,0x3E,XX,  XX,  XX,  XX,     XX,    XX}},
{"ROR",{0x6A,0x6A,XX,0x66,0x76,XX, 0x6E,0x7E,XX,  XX,  XX,  XX,     XX,    XX}},
{"JMP",{XX, XX,XX, XX, XX, XX, 0x4C,XX, XX,  XX,  XX,  XX,     0x6C,  XX}},
{"JSR",{XX, XX,XX, XX, XX, XX, 0x20,XX, XX,  XX,  XX,  XX,     XX,    XX}},
{"RTS",{0x60,XX,XX, XX, XX, XX, XX, XX, XX,  XX,  XX,  XX,     XX,    XX}},
{"RTI",{0x40,XX,XX, XX, XX, XX, XX, XX, XX,  XX,  XX,  XX,     XX,    XX}},
{"PHA",{0x48,XX,XX, XX, XX, XX, XX, XX, XX,  XX,  XX,  XX,     XX,    XX}},
{"PLA",{0x68,XX,XX, XX, XX, XX, XX, XX, XX,  XX,  XX,  XX,     XX,    XX}},
{"PHP",{0x08,XX,XX, XX, XX, XX, XX, XX, XX,  XX,  XX,  XX,     XX,    XX}},
{"PLP",{0x28,XX,XX, XX, XX, XX, XX, XX, XX,  XX,  XX,  XX,     XX,    XX}},
{"TAX",{0xAA,XX,XX, XX, XX, XX, XX, XX, XX,  XX,  XX,  XX,     XX,    XX}},
{"TXA",{0x8A,XX,XX, XX, XX, XX, XX, XX, XX,  XX,  XX,  XX,     XX,    XX}},
{"TAY",{0xA8,XX,XX, XX, XX, XX, XX, XX, XX,  XX,  XX,  XX,     XX,    XX}},
{"TYA",{0x98,XX,XX, XX, XX, XX, XX, XX, XX,  XX,  XX,  XX,     XX,    XX}},
{"TSX",{0xBA,XX,XX, XX, XX, XX, XX, XX, XX,  XX,  XX,  XX,     XX,    XX}},
{"TXS",{0x9A,XX,XX, XX, XX, XX, XX, XX, XX,  XX,  XX,  XX,     XX,    XX}},
{"CLC",{0x18,XX,XX, XX, XX, XX, XX, XX, XX,  XX,  XX,  XX,     XX,    XX}},
{"SEC",{0x38,XX,XX, XX, XX, XX, XX, XX, XX,  XX,  XX,  XX,     XX,    XX}},
{"CLI",{0x58,XX,XX, XX, XX, XX, XX, XX, XX,  XX,  XX,  XX,     XX,    XX}},
{"SEI",{0x78,XX,XX, XX, XX, XX, XX, XX, XX,  XX,  XX,  XX,     XX,    XX}},
{"CLD",{0xD8,XX,XX, XX, XX, XX, XX, XX, XX,  XX,  XX,  XX,     XX,    XX}},
{"SED",{0xF8,XX,XX, XX, XX, XX, XX, XX, XX,  XX,  XX,  XX,     XX,    XX}},
{"CLV",{0xB8,XX,XX, XX, XX, XX, XX, XX, XX,  XX,  XX,  XX,     XX,    XX}},
{"NOP",{0xEA,XX,XX, XX, XX, XX, XX, XX, XX,  XX,  XX,  XX,     XX,    XX}},
{"BRK",{0x00,XX,XX, XX, XX, XX, XX, XX, XX,  XX,  XX,  XX,     XX,    XX}},
{"BRA",{XX, XX,XX, XX, XX, XX, XX, XX, XX,  XX,  XX,  XX,     XX,   0x80}},
{"BCC",{XX, XX,XX, XX, XX, XX, XX, XX, XX,  XX,  XX,  XX,     XX,   0x90}},
{"BCS",{XX, XX,XX, XX, XX, XX, XX, XX, XX,  XX,  XX,  XX,     XX,   0xB0}},
{"BEQ",{XX, XX,XX, XX, XX, XX, XX, XX, XX,  XX,  XX,  XX,     XX,   0xF0}},
{"BMI",{XX, XX,XX, XX, XX, XX, XX, XX, XX,  XX,  XX,  XX,     XX,   0x30}},
{"BNE",{XX, XX,XX, XX, XX, XX, XX, XX, XX,  XX,  XX,  XX,     XX,   0xD0}},
{"BPL",{XX, XX,XX, XX, XX, XX, XX, XX, XX,  XX,  XX,  XX,     XX,   0x10}},
{"BVC",{XX, XX,XX, XX, XX, XX, XX, XX, XX,  XX,  XX,  XX,     XX,   0x50}},
{"BVS",{XX, XX,XX, XX, XX, XX, XX, XX, XX,  XX,  XX,  XX,     XX,   0x70}},
{0,{0}}
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

static int is_branch_mnemonic(const char* mn){
    return strcmp(mn,"BRA")==0 || strcmp(mn,"BCC")==0 || strcmp(mn,"BCS")==0 ||
           strcmp(mn,"BEQ")==0 || strcmp(mn,"BMI")==0 || strcmp(mn,"BNE")==0 ||
           strcmp(mn,"BPL")==0 || strcmp(mn,"BVC")==0 || strcmp(mn,"BVS")==0;
}

static int find_sym(const char* name){
    int i;
    for(i=0;i<nsym;i++){ if(strcmp(symtab[i].name,name)==0) return i; }
    return -1;
}
static int add_or_update_sym(const char* name, uint16_t value, unsigned defined){
    int i;
    if(nsym>=MAXSYM){ printf("Too many symbols\n"); exit(1); }
    i = find_sym(name);
    if(i>=0){
        if(defined){ symtab[i].value=value; symtab[i].defined=1; }
        return i;
    }
    strncpy(symtab[nsym].name,name,sizeof(symtab[nsym].name)-1);
    symtab[nsym].name[sizeof(symtab[nsym].name)-1]=0;
    symtab[nsym].value=value;
    symtab[nsym].defined=defined;
    nsym++;
    return nsym-1;
}

static void add_line(const char* text){
    if(nlines>=MAXLINES){ printf("Too many rows\n"); exit(1); }
    strncpy(lines[nlines], text, MAXLEN-1);
    lines[nlines][MAXLEN-1]=0;
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
    r->is_label=0; r->label[0]=0; r->num=n; r->op=op;
}
static void init_val_lab(asm_value_t* r, const char* name, asm_vop_t op){
    size_t L = strlen(name); if(L>47) L=47;
    r->is_label=1; strncpy(r->label,name,L); r->label[L]=0; r->num=0; r->op=op;
}
static void parse_value_out(const char* tok, asm_value_t* out){
    asm_vop_t op;
    uint16_t v;
    if(!tok || !*tok){ init_val_num(out, 0, V_NORMAL); return; }
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
        if(idx<0 || !symtab[idx].defined){ printf("Unidentified label: %s\n", a->label); base=0; }
        else base = symtab[idx].value;
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

    if(!s || !*s) return M_IMP;
    while(*s==' '||*s=='\t') s++;

    if((s[0]=='A'||s[0]=='a') && !isalnum((unsigned char)s[1])) return M_ACC;

    if(*s=='#'){ parse_value_out(s+1, val); return M_IMM; }

    if(*s=='('){
        q = strchr(s,')'); if(!q) return M_IMP;
        len = (int)(q-(s+1)); if(len<=0) return M_IMP;
        if(len>79) len=79;
        memcpy(inner,s+1,len); inner[len]=0;

        if(q[1]==',' && (q[2]=='Y'||q[2]=='y')){ parse_value_out(inner, val); return M_INDY; }
        if(len>2 && inner[len-2]==',' && (inner[len-1]=='X'||inner[len-1]=='x')){
            inner[len-2]=0; parse_value_out(inner, val); return M_INDX;
        }
        parse_value_out(inner, val);
        if(!val->is_label && val->op==V_NORMAL && val->num<=0xFF) return M_INDZP;
        return M_INDABS;
    }

    comma = strchr(s,',');
    if(comma){
        char base[80];
        int k = (int)(comma-s); if(k>79) k=79;
        memcpy(base,s,k); base[k]=0;
        parse_value_out(base, val);
        if(comma[1]=='X'||comma[1]=='x'){ if(!val->is_label && val->op==V_NORMAL && val->num<=0xFF) return M_ZPX; return M_ABSX; }
        if(comma[1]=='Y'||comma[1]=='y'){ if(!val->is_label && val->op==V_NORMAL && val->num<=0xFF) return M_ZPY; return M_ABSY; }
        return M_ABS;
    }else{
        parse_value_out(s, val);
        if(!val->is_label && val->op==V_NORMAL && val->num<=0xFF) return M_ZP; else return M_ABS;
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

        strncpy(g_buf, lines[li], MAXLEN-1); g_buf[MAXLEN-1]=0;
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

            }else if(strcmp(g_dir,".EQU")==0){
                char *t,*t1,*t2;
                strncpy(g_buf2, lines[li], sizeof(g_buf2)-1); g_buf2[sizeof(g_buf2)-1]=0;
                trim_comment(g_buf2); t=g_buf2; ltrim_ptr(&t);
                if(split_token(t,&t1,&t2) && t1 && t2){
                    parse_value_out(g_rest, &g_val);
                    if(g_val.is_label){
                        int idx = find_sym(g_val.label);
                        if(idx<0 || !symtab[idx].defined){ printf("PASS1: .equ unknown symbol %s\n", g_val.label); }
                        else add_or_update_sym(t1, apply_vop(symtab[idx].value, g_val.op), 1);
                    }else{
                        add_or_update_sym(t1, apply_vop(g_val.num, g_val.op), 1);
                    }
                }else{
                    printf("Syntax .equ: NAME .equ value\n");
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
    outbuf[off] = b;
}

static void pass2_and_list(void){
    FILE* lst;
    int li;

    lst = fopen("out.lst","wb");
    if(!lst) printf("Can't open out.lst\n");

    pc = org;
    for(li=0; li<nlines; ++li){
        uint16_t line_pc_before = pc;

        strncpy(g_buf2, lines[li], sizeof(g_buf2)-1); g_buf2[sizeof(g_buf2)-1]=0;
        strncpy(g_buf,  lines[li], MAXLEN-1); g_buf[MAXLEN-1]=0;
        trim_comment(g_buf);
        g_s = g_buf; ltrim_ptr(&g_s);
        if(!*g_s){
            if(lst) fprintf(lst,"          | %s\n", g_buf2);
            continue;
        }

        if(is_ident_start((unsigned char)*g_s)){
            char* p = g_s;
            while(is_ident_char((unsigned char)*p)) ++p;
            if(*p==':'){
                g_s=p+1; ltrim_ptr(&g_s);
                if(!*g_s){
                    if(lst) fprintf(lst,"%04X          | %s\n", line_pc_before, g_buf2);
                    continue;
                }
            }
        }

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
                            parse_value_out(g_tok, &g_val);
                            emit_at((uint8_t)resolve_value(&g_val), pc++);
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
                }
            }
        }else{
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
                    emit_at((uint8_t)resolve_value(&g_val), pc++);
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
        if(lst){
            uint16_t line_pc_after = pc;
            uint16_t a;
            int pad;
            fprintf(lst,"%04X ", line_pc_before);
            for(a=line_pc_before; a<line_pc_after; ++a) fprintf(lst,"%02X ", outbuf[a-org]);
            pad = 11 - 3*(line_pc_after-line_pc_before); if(pad<0) pad=0;
            while(pad--) fputc(' ', lst);
            fprintf(lst,"| %s\n", g_buf2);
        }
    }

    if(lst) fclose(lst);
}

/* --- zapis BIN --- */
static void save_bin(void){
    unsigned len;
    FILE* f;
    len = (pc>org)? (unsigned)(pc-org) : 0;
    printf("ORG=$%04X, size=%u bytes\n", org, len);
    f = fopen("out.bin","wb"); if(!f){ printf("Writing error\n"); return; }
    fwrite(outbuf,1,len,f); fclose(f);
    printf("Save in out.bin and out.lst\n");
}

/* --- main: stdin + .include --- */
int main(int argc, char **argv){
    
    char *s,*dir,*rest; 
    const char* q;
    int i;

    printf("\r\n--------------\r\nargc=%d\r\n", argc);
    for(i = 0; i < argc; i++) {
        printf("argv[%d]=\"%s\"\r\n", i, argv[i]);
    }

    printf("Mini assembler 65C02 (cc65-safe v2)\n");
    printf("Paste the code. Empty line start compilation.\n");

    while(nlines<MAXLINES){
        if(!fgets(g_buf,sizeof(g_buf),stdin)) break;
        if(g_buf[0]=='\n'||g_buf[0]==0) break;
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

    pass1();
    pass2_and_list();
    save_bin();
    return 0;
}
