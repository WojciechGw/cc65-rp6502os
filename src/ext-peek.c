#include "commons.h"

#define APPVER "20260410.0007"

// #define DEBUG

#define HEXDUMP_LINE_SIZE 16
#define TX_READY (RIA.ready & RIA_READY_TX_BIT)
#define TX_READY_SPIN  while (!TX_READY)

typedef void (*char_stream_func_t)(const char *buf, int size);
typedef void (*read_data_func_t)(uint8_t *buf, uint16_t addr, uint16_t size);

static const char hexdigits[] = "0123456789ABCDEF";

// things related to : console command processor operations

inline void tx_char(char c) {
    TX_READY_SPIN;
    RIA.tx = c;
    return;
}

void tx_chars(const char *buf, int ct) {
    for(; ct; ct--, buf++) tx_char(*buf);
    return;
}

void tx_string(const char *buf) {
    while(*buf) tx_char(*buf++);
    return;
}

void tx_hex32(unsigned long val) { // Print a 32-bit value as 8 hex digits.
    char out[8];
    int i;
    for(i = 7; i >= 0; i--) {
        out[i] = hexdigits[val & 0xF];
        val >>= 4;
    }
    tx_chars(out, sizeof(out));
}

void tx_hex16(uint16_t val) { // Print 16-bit value as 4 hex digits
    char out[4];
    int i;
    for(i = 3; i >= 0; i--) {
        out[i] = hexdigits[val & 0xF];
        val >>= 4;
    }
    tx_chars(out, sizeof(out));
}

void tx_dec32(unsigned long val) { // Print an unsigned long in decimal.
    char out[10];
    int i = 10;
    if(val == 0) {
        tx_char('0');
        return;
    }
    while(val && i) {
        out[--i] = '0' + (val % 10);
        val /= 10;
    }
    tx_chars(&out[i], 10 - i);
}

int hexstr(char *str, uint8_t val) { // Assumes str points to at least two bytes.
    str[0] = hexdigits[val >> 4];
    str[1] = hexdigits[val & 0xF];
    return 2;
}

void hexdump(uint16_t addr, uint16_t bytes, char_stream_func_t streamer, read_data_func_t reader) {
    int i;
    uint8_t data[HEXDUMP_LINE_SIZE];
    char string[HEXDUMP_LINE_SIZE * 3 + 32];

    while(bytes) {
        char *str = string;
        int rd = bytes > sizeof(data) ? sizeof(data) : bytes;
        str += hexstr(str, addr >> 8);
        str += hexstr(str, addr & 0xFF);
        *str++ = ' ';
        *str++ = ' ';
        reader(data, addr, rd);
        for(i = 0; i < rd; i++) {
            if (i == 8) *str++ = ' ';
            *str++ = ' ';
            str += hexstr(str, data[i]);
        }
        if(rd < HEXDUMP_LINE_SIZE){
            int missing = HEXDUMP_LINE_SIZE - rd;
            for(i = 0; i < missing; i++){
                if (i == 8) *str++ = ' ';
                *str++ = ' ';
                *str++ = ' ';
                *str++ = ' ';
            }
        }
        *str++ = ' ';
        *str++ = ' ';
        *str++ = 0xB3;
        for(i = 0; i < rd; i++) {
            char b = (data[i] >= 32 && data[i] <= 126) ? data[i] : '.';
            *str++ = b;
        }
        *str++ = 0xB3;
        *str++ = CHAR_CR;
        *str++ = CHAR_LF;
        streamer(string, str - string);
        bytes -= rd;
        addr += rd;
    }
    return;
}

// things related to : memory operations

void ram_reader(uint8_t *buf, uint16_t addr, uint16_t size) {
    uint8_t *data = (uint8_t *)addr;
    for(; size; size--) *buf++ = *data++;
    return;
}

void xram_reader(uint8_t *buf, uint16_t addr, uint16_t size) {
    RIA.step0 = 1;
    RIA.addr0 = addr;
    for(; size; size--) *buf++ = RIA.rw0;
    return;
}


int main(int argc, char **argv) {
    uint32_t offset = 0;

#ifdef DEBUG
    printf("\r\n--------------\r\nargc=%d\r\n", argc);
    for(i = 0; i < argc; i++) {
        printf("argv[%d]=\"%s\"\r\n", i, argv[i]);
    }
#endif

    uint16_t addr = 0;
    uint16_t size = 16;
    uint8_t use_xram = 0;

    if(argc < 1) {
        tx_string("Usage: peek addr [bytes] [/x]" NEWLINE);
        return 0;
    }
    addr = strtoul(argv[0], NULL, 16);
    if(argc > 1) {
        if(!strcmp(argv[1], "/x")) {
            use_xram = 1;
        } else {
            size = strtoul(argv[1], NULL, 0);
        }
    }
    if(argc > 2 && (!strcmp(argv[2], "/x"))) {
        use_xram = 1;
    }
    tx_string(NEWLINE "Peek at the ");
    if(use_xram) {
        tx_string("XRAM" NEWLINE "---" NEWLINE);
        hexdump(addr, size, tx_chars, xram_reader);
    } else {
        tx_string("RAM" NEWLINE "---" NEWLINE);
        hexdump(addr, size, tx_chars, ram_reader);
    }
    tx_string(NEWLINE);

    return 0;

}
