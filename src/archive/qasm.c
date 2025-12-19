#include <rp6502.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

static const char *menu[4] =  {"\xFE", "File", "Edit", "Tools"};
static const char *menu_dot[2] = {"setup", "about"};
static const char *menu_file[4] =  {"new", "load", "save", "save as ..."};
static const char *menu_edit[3] =  {"cut", "copy", "paste"};
static const char *menu_tools[3] =  {"build", "run", "trace"};

#define VEC_IRQ 0xFFFA
#define VEC_RST 0xFFFC
#define VEC_NMI 0xFFFE

volatile uint8_t *vecIRQ = (volatile uint8_t *)VEC_IRQ;
volatile uint8_t *vecRST = (volatile uint8_t *)VEC_RST;
volatile uint8_t *vecNMI = (volatile uint8_t *)VEC_NMI;

typedef struct {
    uint16_t pc;  /* 0,1 */
    uint8_t  p;   /* 2 */
    uint8_t  a;   /* 3 */
    uint8_t  x;   /* 4 */
    uint8_t  y;   /* 5 */
    uint8_t  s;   /* 6 */
} CpuState;
extern CpuState cpu_state;
extern void capture_cpu_state(void);

CpuState cpu_state;

extern void run(void);
extern void msg(void);
extern void end(void);

void print_bin8(uint8_t v){
    int i = 0;
    for (i = 7; i >= 0; i--) {
        putchar( (v & (1u << i)) ? '1' : '0' );
    }
}

void cls(){
    // printf("\x1b[2J");
    printf("\x1b");
    printf("c");
}

void setpos(uint8_t row, uint8_t col){
    printf("\x1b[%d;%dH", row, col);
}

void printat(uint8_t row, uint8_t col, char *txt){
    printf("\x1b[%d;%dH%s", row, col, txt);
}

void show_CPU_status(){

    uint16_t IRQVECaddr = vecIRQ[0] | (vecIRQ[1] << 8);
    uint16_t RSTVECaddr = vecRST[0] | (vecRST[1] << 8);
    uint16_t NMIVECaddr = vecNMI[0] | (vecNMI[1] << 8);

    setpos(30,49);
    printf("IRQ 0x%04X RST 0x%04X NMI 0x%04X", IRQVECaddr, RSTVECaddr, NMIVECaddr);
    asm("jsr _capture_cpu_state");
    printat(27, 53, "PC   SR AC XR YR SP NV-BDIZC");
    setpos(28, 53);
    printf("%04X %02X %02X %02X %02X %02X ", cpu_state.pc, cpu_state.p, cpu_state.a, cpu_state.x, cpu_state.y, cpu_state.s);
    print_bin8(cpu_state.p);
}

void showMemory(uint16_t addrStart, uint16_t addrStop){
    uint16_t addr = 0;
    uint8_t i = 0;
    for ( addr = addrStart; addr <= addrStop; addr += 16) {
        uint8_t *p = (uint8_t *)addr;
        printf("%04X ", addr);
        for (i = 0; i < 16 && (addr + i) <= addrStop; i++) {
            printf( (i == 8 ? "  %02X" : " %02X"), p[i]);
        }
        printf("\n");
    }
}

char buf[255];

void main(){

    uint8_t i = 0;
    uint8_t c = 1;

    uint8_t fd = 0;
    uint16_t bytecounter = 0;

    uint16_t addr = 0x8000u;
    unsigned char *dst = (unsigned char*)addr;

    cls();
    // printf("\33[48;2;%d;%d;%dm  ", 0x80, 0x80, 0x80);
    printat(30, 1, (char *)menu[0]);
    c = 3;
    for(i = 1; i < (sizeof(menu) / sizeof(menu[0])); i++) {
        printat(30, c-1, " ");
        printat(30, c, (char *)menu[i]);
        c += strlen(menu[i]) + 1;
    }
    // puts("\33[0m");
    setpos(29,1);
    memset(buf, '_', 80);  // wypełnij 80 bajtów '_'
    buf[80] = '\0';        // zakończenie stringa
    printf("%s", buf);
    // for(i = 0; i < 80; i++) printf("_");

    show_CPU_status();

    setpos(1,1);
    showMemory(addr, addr + 0xFF);

    printf("> CPU\n");
    msg();
    printf("> LOAD $8000 outs8000.bin\n");
    fd = open("outs8000.bin", O_RDONLY);
    if(fd){
        bytecounter = read(fd, buf, 255);
        close(fd);
        printf("  SUCCESS > read %d bytes\n", bytecounter);
    } else {
        printf("  ERROR > read outs8000.bin file\n");
    }

    for (i = 0; i < bytecounter; ++i) {
        dst[i] = (unsigned char)buf[i];
    }
    printf("> RUN $8000\n");
    run();
    printf("> RUN $8000\n");
    run();
    printf("> QUIT\n\n");
    end();
    // while(1){}
}
