#include "commons.h"

#define APPVER "20260502.1138"

// #define DEBUG

#define FNAMELEN 64
#define HEXDUMP_LINE_SIZE 16

typedef void (*char_stream_func_t)(const char *buf, int size);
typedef void (*read_data_func_t)(uint8_t *buf, uint16_t addr, uint16_t size);

static f_stat_t dir_ent;
static const char hexdigits[] = "0123456789ABCDEF";
static int filehex_fd = -1;
static uint32_t filehex_base = 0;

static void file_reader(uint8_t *buf, uint16_t addr, uint16_t size) {
    off_t pos = (off_t)filehex_base + (off_t)addr;
    if(filehex_fd < 0) return;
    if(lseek(filehex_fd, pos, SEEK_SET) < 0) return;
    read(filehex_fd, buf, size);
}

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

int main(int argc, char **argv) {
    uint32_t offset = 0;
    uint32_t bytes;
    uint32_t maxbytes;
    uint16_t dump_bytes;

    
#ifdef DEBUG
    printf("\r\n--------------\r\nargc=%d\r\n", argc);
    for(i = 0; i < argc; i++) {
        printf("argv[%d]=\"%s\"\r\n", i, argv[i]);
    }
#endif

    if(argc < 1) {
        tx_string("Usage: hex <file> [offset [bytes]]" NEWLINE);
        return 0;
    }
    if(f_stat(argv[0], &dir_ent) < 0) {
        tx_string(EXCLAMATION "stat failed" NEWLINE);
        return -1;
    }
    maxbytes = dir_ent.fsize;
    if(argc > 1) offset = (uint32_t)strtoul(argv[1], NULL, 0);
    if(offset > maxbytes) {
        tx_string(EXCLAMATION "offset past end of file" NEWLINE);
        return -1;
    }
    maxbytes -= offset;
    bytes = (argc > 2) ? (uint32_t)strtoul(argv[2], NULL, 0) : maxbytes;
    if(bytes > maxbytes) bytes = maxbytes;
    if(bytes == 0) {
        tx_string(EXCLAMATION "nothing to show" NEWLINE);
        return 0;
    }
    filehex_fd = open(argv[0], O_RDONLY);
    if(filehex_fd < 0) {
        tx_string(EXCLAMATION "can't open a file" NEWLINE);
        return -1;
    }
    filehex_base = offset;
    dump_bytes = (bytes > 0xFFFFu) ? 0xFFFFu : (uint16_t)bytes;
    hexdump((uint16_t)offset, dump_bytes, tx_chars, file_reader);
    close(filehex_fd);
    filehex_fd = -1;
    filehex_base = 0;
    tx_string(NEWLINE);
    return 0;

}
