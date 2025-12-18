#include <stdint.h>
// #define OPCODES

#ifdef OPCODES
typedef enum {
  M_ABS, M_ABSINDX, M_ABSX, M_ABSY, M_ABSIND, 
  M_ACC, M_IMM, M_IMP, M_PCREL, M_STACK, 
  M_ZP, M_ZPINDX, M_ZPX, M_ZPY, M_ZPIND, M_ZPINDY,
  M__COUNT
} asm_mode_t;

typedef struct { uint8_t mode; uint8_t opcode; } opvar_t;
typedef struct { const char* name; const opvar_t* vars; uint8_t count; } opdef_t;

static const opvar_t vars_ADC[] = {{M_ABS,0x6D},{M_ABSX,0x7D},{M_ABSY,0x79},{M_IMM,0x69},{M_ZP,0x65},{M_ZPINDX,0x61},{M_ZPX,0x75},{M_ZPIND,0x72},{M_ZPINDY,0x71},};
static const opvar_t vars_AND[] = {{M_ABS,0x2D},{M_ABSX,0x3D},{M_ABSY,0x39},{M_IMM,0x29},{M_ZP,0x25},{M_ZPINDX,0x21},{M_ZPX,0x35},{M_ZPIND,0x32},{M_ZPINDY,0x31},};
static const opvar_t vars_ASL[] = {{M_ABS,0x0E},{M_ABSX,0x1E},{M_ACC,0x0A},{M_ZP,0x06},{M_ZPX,0x16},};
static const opvar_t vars_BBR0[] = {{M_PCREL,0x0F},};
static const opvar_t vars_BBR1[] = {{M_PCREL,0x1F},};
static const opvar_t vars_BBR2[] = {{M_PCREL,0x2F},};
static const opvar_t vars_BBR3[] = {{M_PCREL,0x3F},};
static const opvar_t vars_BBR4[] = {{M_PCREL,0x4F},};
static const opvar_t vars_BBR5[] = {{M_PCREL,0x5F},};
static const opvar_t vars_BBR6[] = {{M_PCREL,0x6F},};
static const opvar_t vars_BBR7[] = {{M_PCREL,0x7F},};
static const opvar_t vars_BBS0[] = {{M_PCREL,0x8F},};
static const opvar_t vars_BBS1[] = {{M_PCREL,0x9F},};
static const opvar_t vars_BBS2[] = {{M_PCREL,0xAF},};
static const opvar_t vars_BBS3[] = {{M_PCREL,0xBF},};
static const opvar_t vars_BBS4[] = {{M_PCREL,0xCF},};
static const opvar_t vars_BBS5[] = {{M_PCREL,0xDF},};
static const opvar_t vars_BBS6[] = {{M_PCREL,0xEF},};
static const opvar_t vars_BBS7[] = {{M_PCREL,0xFF},};
static const opvar_t vars_BCC[] = {{M_PCREL,0x90},};
static const opvar_t vars_BCS[] = {{M_PCREL,0xB0},};
static const opvar_t vars_BEQ[] = {{M_PCREL,0xF0},};
static const opvar_t vars_BIT[] = {{M_ABS,0x2C},{M_ABSX,0x3C},{M_IMM,0x89},{M_ZP,0x24},{M_ZPX,0x34},};
static const opvar_t vars_BMI[] = {{M_PCREL,0x30},};
static const opvar_t vars_BNE[] = {{M_PCREL,0xD0},};
static const opvar_t vars_BPL[] = {{M_PCREL,0x10},};
static const opvar_t vars_BRA[] = {{M_PCREL,0x80},};
static const opvar_t vars_BRK[] = {{M_STACK,0x00},};
static const opvar_t vars_BVC[] = {{M_PCREL,0x50},};
static const opvar_t vars_BVS[] = {{M_PCREL,0x70},};
static const opvar_t vars_CLC[] = {{M_IMP,0x18},};
static const opvar_t vars_CLD[] = {{M_IMP,0xD8},};
static const opvar_t vars_CLI[] = {{M_IMP,0x58},};
static const opvar_t vars_CLV[] = {{M_IMP,0xB8},};
static const opvar_t vars_CMP[] = {{M_ABS,0xCD},{M_ABSX,0xDD},{M_ABSY,0xD9},{M_IMM,0xC9},{M_ZP,0xC5},{M_ZPINDX,0xC1},{M_ZPX,0xD5},{M_ZPIND,0xD2},{M_ZPINDY,0xD1},};
static const opvar_t vars_CPX[] = {{M_ABS,0xEC},{M_IMM,0xE0},{M_ZP,0xE4},};
static const opvar_t vars_CPY[] = {{M_ABS,0xCC},{M_IMM,0xC0},{M_ZP,0xC4},};
static const opvar_t vars_DEC[] = {{M_ABS,0xCE},{M_ABSX,0xDE},{M_ACC,0x3A},{M_ZP,0xC6},{M_ZPX,0xD6},};
static const opvar_t vars_DEX[] = {{M_IMP,0xCA},};
static const opvar_t vars_DEY[] = {{M_IMP,0x88},};
static const opvar_t vars_EOR[] = {{M_ABS,0x4D},{M_ABSX,0x5D},{M_ABSY,0x59},{M_IMM,0x49},{M_ZP,0x45},{M_ZPINDX,0x41},{M_ZPX,0x55},{M_ZPIND,0x52},{M_ZPINDY,0x51},};
static const opvar_t vars_INC[] = {{M_ABS,0xEE},{M_ABSX,0xFE},{M_ACC,0x1A},{M_ZP,0xE6},{M_ZPX,0xF6},};
static const opvar_t vars_INX[] = {{M_IMP,0xE8},};
static const opvar_t vars_INY[] = {{M_IMP,0xC8},};
static const opvar_t vars_JMP[] = {{M_ABS,0x4C},{M_ABSINDX,0x7C},{M_ABSIND,0x6C},};
static const opvar_t vars_JSR[] = {{M_ABS,0x20},};
static const opvar_t vars_LDA[] = {{M_ABS,0xAD},{M_ABSX,0xBD},{M_ABSY,0xB9},{M_IMM,0xA9},{M_ZP,0xA5},{M_ZPINDX,0xA1},{M_ZPX,0xB5},{M_ZPIND,0xB2},{M_ZPINDY,0xB1},};
static const opvar_t vars_LDX[] = {{M_ABS,0xAE},{M_ABSY,0xBE},{M_IMM,0xA2},{M_ZP,0xA6},{M_ZPY,0xB6},};
static const opvar_t vars_LDY[] = {{M_ABS,0xAC},{M_ABSX,0xBC},{M_IMM,0xA0},{M_ZP,0xA4},{M_ZPX,0xB4},};
static const opvar_t vars_LSR[] = {{M_ABS,0x4E},{M_ABSX,0x5E},{M_ACC,0x4A},{M_ZP,0x46},{M_ZPX,0x56},};
static const opvar_t vars_NOP[] = {{M_IMP,0xEA},};
static const opvar_t vars_ORA[] = {{M_ABS,0x0D},{M_ABSX,0x1D},{M_ABSY,0x19},{M_IMM,0x09},{M_ZP,0x05},{M_ZPINDX,0x01},{M_ZPX,0x15},{M_ZPIND,0x12},{M_ZPINDY,0x11},};
static const opvar_t vars_PHA[] = {{M_STACK,0x48},};
static const opvar_t vars_PHP[] = {{M_STACK,0x08},};
static const opvar_t vars_PHX[] = {{M_STACK,0xDA},};
static const opvar_t vars_PHY[] = {{M_STACK,0x5A},};
static const opvar_t vars_PLA[] = {{M_STACK,0x68},};
static const opvar_t vars_PLP[] = {{M_STACK,0x28},};
static const opvar_t vars_PLX[] = {{M_STACK,0xFA},};
static const opvar_t vars_PLY[] = {{M_STACK,0x7A},};
static const opvar_t vars_RMB0[] = {{M_ZP,0x07},};
static const opvar_t vars_RMB1[] = {{M_ZP,0x17},};
static const opvar_t vars_RMB2[] = {{M_ZP,0x27},};
static const opvar_t vars_RMB3[] = {{M_ZP,0x37},};
static const opvar_t vars_RMB4[] = {{M_ZP,0x47},};
static const opvar_t vars_RMB5[] = {{M_ZP,0x57},};
static const opvar_t vars_RMB6[] = {{M_ZP,0x67},};
static const opvar_t vars_RMB7[] = {{M_ZP,0x77},};
static const opvar_t vars_ROL[] = {{M_ABS,0x2E},{M_ABSX,0x3E},{M_ACC,0x2A},{M_ZP,0x26},{M_ZPX,0x36},};
static const opvar_t vars_ROR[] = {{M_ABS,0x6E},{M_ABSX,0x7E},{M_ACC,0x6A},{M_ZP,0x66},{M_ZPX,0x76},};
static const opvar_t vars_RTI[] = {{M_STACK,0x40},};
static const opvar_t vars_RTS[] = {{M_STACK,0x60},};
static const opvar_t vars_SBC[] = {{M_ABS,0xED},{M_ABSX,0xFD},{M_ABSY,0xF9},{M_IMM,0xE9},{M_ZP,0xE5},{M_ZPINDX,0xE1},{M_ZPX,0xF5},{M_ZPIND,0xF2},{M_ZPINDY,0xF1},};
static const opvar_t vars_SEC[] = {{M_IMP,0x38},};
static const opvar_t vars_SED[] = {{M_IMP,0xF8},};
static const opvar_t vars_SEI[] = {{M_IMP,0x78},};
static const opvar_t vars_SMB0[] = {{M_ZP,0x87},};
static const opvar_t vars_SMB1[] = {{M_ZP,0x97},};
static const opvar_t vars_SMB2[] = {{M_ZP,0xA7},};
static const opvar_t vars_SMB3[] = {{M_ZP,0xB7},};
static const opvar_t vars_SMB4[] = {{M_ZP,0xC7},};
static const opvar_t vars_SMB5[] = {{M_ZP,0xD7},};
static const opvar_t vars_SMB6[] = {{M_ZP,0xE7},};
static const opvar_t vars_SMB7[] = {{M_ZP,0xF7},};
static const opvar_t vars_STA[] = {{M_ABS,0x8D},{M_ABSX,0x9D},{M_ABSY,0x99},{M_ZP,0x85},{M_ZPINDX,0x81},{M_ZPX,0x95},{M_ZPIND,0x92},{M_ZPINDY,0x91},};
static const opvar_t vars_STP[] = {{M_IMP,0xDB},};
static const opvar_t vars_STX[] = {{M_ABS,0x8E},{M_ZP,0x86},{M_ZPY,0x96},};
static const opvar_t vars_STY[] = {{M_ABS,0x8C},{M_ZP,0x84},{M_ZPX,0x94},};
static const opvar_t vars_STZ[] = {{M_ABS,0x9C},{M_ABSX,0x9E},{M_ZP,0x64},{M_ZPX,0x74},};
static const opvar_t vars_TAX[] = {{M_IMP,0xAA},};
static const opvar_t vars_TAY[] = {{M_IMP,0xA8},};
static const opvar_t vars_TRB[] = {{M_ABS,0x1C},{M_ZP,0x14},};
static const opvar_t vars_TSB[] = {{M_ABS,0x0C},{M_ZP,0x04},};
static const opvar_t vars_TSX[] = {{M_IMP,0xBA},};
static const opvar_t vars_TXA[] = {{M_IMP,0x8A},};
static const opvar_t vars_TXS[] = {{M_IMP,0x9A},};
static const opvar_t vars_TYA[] = {{M_IMP,0x98},};
static const opvar_t vars_WAI[] = {{M_IMP,0xCB},};


static const opdef_t ops[] = {
    {"ADC", vars_ADC, sizeof(vars_ADC) },
    {"AND", vars_AND, sizeof(vars_AND) },
    {"ASL", vars_ASL, sizeof(vars_ASL) },
    {"BBR0", vars_BBR0, sizeof(vars_BBR0) },
    {"BBR1", vars_BBR1, sizeof(vars_BBR1) },
    {"BBR2", vars_BBR2, sizeof(vars_BBR2) },
    {"BBR3", vars_BBR3, sizeof(vars_BBR3) },
    {"BBR4", vars_BBR4, sizeof(vars_BBR4) },
    {"BBR5", vars_BBR5, sizeof(vars_BBR5) },
    {"BBR6", vars_BBR6, sizeof(vars_BBR6) },
    {"BBR7", vars_BBR7, sizeof(vars_BBR7) },
    {"BBS0", vars_BBS0, sizeof(vars_BBS0) },
    {"BBS1", vars_BBS1, sizeof(vars_BBS1) },
    {"BBS2", vars_BBS2, sizeof(vars_BBS2) },
    {"BBS3", vars_BBS3, sizeof(vars_BBS3) },
    {"BBS4", vars_BBS4, sizeof(vars_BBS4) },
    {"BBS5", vars_BBS5, sizeof(vars_BBS5) },
    {"BBS6", vars_BBS6, sizeof(vars_BBS6) },
    {"BBS7", vars_BBS7, sizeof(vars_BBS7) },
    {"BCC", vars_BCC, sizeof(vars_BCC) },
    {"BCS", vars_BCS, sizeof(vars_BCS) },
    {"BEQ", vars_BEQ, sizeof(vars_BEQ) },
    {"BIT", vars_BIT, sizeof(vars_BIT) },
    {"BMI", vars_BMI, sizeof(vars_BMI) },
    {"BNE", vars_BNE, sizeof(vars_BNE) },
    {"BPL", vars_BPL, sizeof(vars_BPL) },
    {"BRA", vars_BRA, sizeof(vars_BRA) },
    {"BRK", vars_BRK, sizeof(vars_BRK) },
    {"BVC", vars_BVC, sizeof(vars_BVC) },
    {"BVS", vars_BVS, sizeof(vars_BVS) },
    {"CLC", vars_CLC, sizeof(vars_CLC) },
    {"CLD", vars_CLD, sizeof(vars_CLD) },
    {"CLI", vars_CLI, sizeof(vars_CLI) },
    {"CLV", vars_CLV, sizeof(vars_CLV) },
    {"CMP", vars_CMP, sizeof(vars_CMP) },
    {"CPX", vars_CPX, sizeof(vars_CPX) },
    {"CPY", vars_CPY, sizeof(vars_CPY) },
    {"DEC", vars_DEC, sizeof(vars_DEC) },
    {"DEX", vars_DEX, sizeof(vars_DEX) },
    {"DEY", vars_DEY, sizeof(vars_DEY) },
    {"EOR", vars_EOR, sizeof(vars_EOR) },
    {"INC", vars_INC, sizeof(vars_INC) },
    {"INX", vars_INX, sizeof(vars_INX) },
    {"INY", vars_INY, sizeof(vars_INY) },
    {"JMP", vars_JMP, sizeof(vars_JMP) },
    {"JSR", vars_JSR, sizeof(vars_JSR) },
    {"LDA", vars_LDA, sizeof(vars_LDA) },
    {"LDX", vars_LDX, sizeof(vars_LDX) },
    {"LDY", vars_LDY, sizeof(vars_LDY) },
    {"LSR", vars_LSR, sizeof(vars_LSR) },
    {"NOP", vars_NOP, sizeof(vars_NOP) },
    {"ORA", vars_ORA, sizeof(vars_ORA) },
    {"PHA", vars_PHA, sizeof(vars_PHA) },
    {"PHP", vars_PHP, sizeof(vars_PHP) },
    {"PHX", vars_PHX, sizeof(vars_PHX) },
    {"PHY", vars_PHY, sizeof(vars_PHY) },
    {"PLA", vars_PLA, sizeof(vars_PLA) },
    {"PLP", vars_PLP, sizeof(vars_PLP) },
    {"PLX", vars_PLX, sizeof(vars_PLX) },
    {"PLY", vars_PLY, sizeof(vars_PLY) },
    {"RMB0", vars_RMB0, sizeof(vars_RMB0) },
    {"RMB1", vars_RMB1, sizeof(vars_RMB1) },
    {"RMB2", vars_RMB2, sizeof(vars_RMB2) },
    {"RMB3", vars_RMB3, sizeof(vars_RMB3) },
    {"RMB4", vars_RMB4, sizeof(vars_RMB4) },
    {"RMB5", vars_RMB5, sizeof(vars_RMB5) },
    {"RMB6", vars_RMB6, sizeof(vars_RMB6) },
    {"RMB7", vars_RMB7, sizeof(vars_RMB7) },
    {"ROL", vars_ROL, sizeof(vars_ROL) },
    {"ROR", vars_ROR, sizeof(vars_ROR) },
    {"RTI", vars_RTI, sizeof(vars_RTI) },
    {"RTS", vars_RTS, sizeof(vars_RTS) },
    {"SBC", vars_SBC, sizeof(vars_SBC) },
    {"SEC", vars_SEC, sizeof(vars_SEC) },
    {"SED", vars_SED, sizeof(vars_SED) },
    {"SEI", vars_SEI, sizeof(vars_SEI) },
    {"SMB0", vars_SMB0, sizeof(vars_SMB0) },
    {"SMB1", vars_SMB1, sizeof(vars_SMB1) },
    {"SMB2", vars_SMB2, sizeof(vars_SMB2) },
    {"SMB3", vars_SMB3, sizeof(vars_SMB3) },
    {"SMB4", vars_SMB4, sizeof(vars_SMB4) },
    {"SMB5", vars_SMB5, sizeof(vars_SMB5) },
    {"SMB6", vars_SMB6, sizeof(vars_SMB6) },
    {"SMB7", vars_SMB7, sizeof(vars_SMB7) },
    {"STA", vars_STA, sizeof(vars_STA) },
    {"STP", vars_STP, sizeof(vars_STP) },
    {"STX", vars_STX, sizeof(vars_STX) },
    {"STY", vars_STY, sizeof(vars_STY) },
    {"STZ", vars_STZ, sizeof(vars_STZ) },
    {"TAX", vars_TAX, sizeof(vars_TAX) },
    {"TAY", vars_TAY, sizeof(vars_TAY) },
    {"TRB", vars_TRB, sizeof(vars_TRB) },
    {"TSB", vars_TSB, sizeof(vars_TSB) },
    {"TSX", vars_TSX, sizeof(vars_TSX) },
    {"TXA", vars_TXA, sizeof(vars_TXA) },
    {"TXS", vars_TXS, sizeof(vars_TXS) },
    {"TYA", vars_TYA, sizeof(vars_TYA) },
    {"WAI", vars_WAI, sizeof(vars_WAI) },
    { 0, 0, 0 }
};
#endif

#ifndef OPCODES
typedef enum {
    M_IMP, M_ACC, M_IMM, M_ZP, M_ZPX, M_ZPY, M_ABS, M_ABSX, M_ABSY,
    M_ZPINDX, M_ZPINDY, M_ZPIND, M_INDABS, M_PCREL, M__COUNT
} asm_mode_t;

typedef struct { const char* name; int16_t op[M__COUNT]; } asm_opdef_t;
static const asm_opdef_t* g_def;
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
#endif