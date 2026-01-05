typedef enum {
  M_ABS, M_ABSINDX, M_ABSX, M_ABSY, M_ABSIND, 
  M_ACC, M_IMM, M_IMP, M_PCREL, M_STACK, 
  M_ZP, M_ZPINDX, M_ZPX, M_ZPY, M_ZPIND, M_ZPINDY,
  M__COUNT
} asm_mode_t;

typedef struct { uint8_t mode; uint8_t opcode; } opvar_t;
typedef struct { const char* name; const opvar_t* vars; uint8_t count; } opdef_t;
#define OPDEF(name) { #name, vars_##name, (uint8_t)(sizeof(vars_##name)/sizeof(opvar_t)) }

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
    OPDEF(ADC),
    OPDEF(AND),
    OPDEF(ASL),
    OPDEF(BBR0),
    OPDEF(BBR1),
    OPDEF(BBR2),
    OPDEF(BBR3),
    OPDEF(BBR4),
    OPDEF(BBR5),
    OPDEF(BBR6),
    OPDEF(BBR7),
    OPDEF(BBS0),
    OPDEF(BBS1),
    OPDEF(BBS2),
    OPDEF(BBS3),
    OPDEF(BBS4),
    OPDEF(BBS5),
    OPDEF(BBS6),
    OPDEF(BBS7),
    OPDEF(BCC),
    OPDEF(BCS),
    OPDEF(BEQ),
    OPDEF(BIT),
    OPDEF(BMI),
    OPDEF(BNE),
    OPDEF(BPL),
    OPDEF(BRA),
    OPDEF(BRK),
    OPDEF(BVC),
    OPDEF(BVS),
    OPDEF(CLC),
    OPDEF(CLD),
    OPDEF(CLI),
    OPDEF(CLV),
    OPDEF(CMP),
    OPDEF(CPX),
    OPDEF(CPY),
    OPDEF(DEC),
    OPDEF(DEX),
    OPDEF(DEY),
    OPDEF(EOR),
    OPDEF(INC),
    OPDEF(INX),
    OPDEF(INY),
    OPDEF(JMP),
    OPDEF(JSR),
    OPDEF(LDA),
    OPDEF(LDX),
    OPDEF(LDY),
    OPDEF(LSR),
    OPDEF(NOP),
    OPDEF(ORA),
    OPDEF(PHA),
    OPDEF(PHP),
    OPDEF(PHX),
    OPDEF(PHY),
    OPDEF(PLA),
    OPDEF(PLP),
    OPDEF(PLX),
    OPDEF(PLY),
    OPDEF(RMB0),
    OPDEF(RMB1),
    OPDEF(RMB2),
    OPDEF(RMB3),
    OPDEF(RMB4),
    OPDEF(RMB5),
    OPDEF(RMB6),
    OPDEF(RMB7),
    OPDEF(ROL),
    OPDEF(ROR),
    OPDEF(RTI),
    OPDEF(RTS),
    OPDEF(SBC),
    OPDEF(SEC),
    OPDEF(SED),
    OPDEF(SEI),
    OPDEF(SMB0),
    OPDEF(SMB1),
    OPDEF(SMB2),
    OPDEF(SMB3),
    OPDEF(SMB4),
    OPDEF(SMB5),
    OPDEF(SMB6),
    OPDEF(SMB7),
    OPDEF(STA),
    OPDEF(STP),
    OPDEF(STX),
    OPDEF(STY),
    OPDEF(STZ),
    OPDEF(TAX),
    OPDEF(TAY),
    OPDEF(TRB),
    OPDEF(TSB),
    OPDEF(TSX),
    OPDEF(TXA),
    OPDEF(TXS),
    OPDEF(TYA),
    OPDEF(WAI),
    { 0, 0, 0 }
};
