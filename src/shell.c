/**
 * @file      shell.c
 * @author    WojciechGw <wojciech@post.pl>
 * @copyright WojciechGw. All rights reserved.
 *
 * based on Jason Howard's code <jth@howardlogic.com>
 * 
 * See LICENSE file in the project root folder for license information
 */

#include <rp6502.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#define MEMTOP 0xFEFF

#define CMD_BUF_MAX 511
#define CMD_TOKEN_MAX 64

#define TX_READY (RIA.ready & RIA_READY_TX_BIT)
#define RX_READY (RIA.ready & RIA_READY_RX_BIT)
#define TX_READY_SPIN while(!TX_READY)
#define RX_READY_SPIN while(!RX_READY)

#define CHAR_BELL   0x07
#define CHAR_BS     0x08
#define CHAR_CR     0x0D
#define CHAR_LF     0x0A
#define CHAR_ESC    0x1B
#define CHAR_NCHR   0x5B
#define CHAR_UP     0x41
#define CHAR_DOWN   0x42
#define CHAR_RIGHT  0x43
#define CHAR_LEFT   0x44
#define CHAR_F1     0x50

#define TAB         "\t"
#define NEWLINE     "\r\n"

#define ARRAY_SIZE(x) (sizeof(x) / sizeof(x[0]))

#ifndef AM_DIR
#define AM_DIR 0x10
#define AM_RDO 0x01
#define AM_HID 0x02
#define AM_SYS 0x04
#define AM_VOL 0x08
#define AM_ARC 0x20
#endif

extern void quit(void);

static const char hexdigits[] = "0123456789ABCDEF";

typedef struct {
    int bytes;
    int lastbytes;
    char buffer[CMD_BUF_MAX+1];
    char lastbuffer[CMD_BUF_MAX+1];
} cmdline_t;

typedef struct {
    const char *cmd;
    const char *help;
    int (*func)(int argc, char **argv);
} cmd_t;

typedef void (*char_stream_func_t)(const char *buf, int size);
typedef void (*read_data_func_t)(uint8_t *buf, uint16_t addr, uint16_t size);

// Statically allocate buffers to keep cc65 stack usage low.
static char dir_cwd[128];
static f_stat_t dir_ent;
typedef struct {
    char name[256];
    unsigned long fsize;
    unsigned char fattrib;
    unsigned fdate;
    unsigned ftime;
} dir_list_entry_t;
#define DIR_LIST_MAX 64
static dir_list_entry_t dir_entries[DIR_LIST_MAX];
static dir_list_entry_t dir_tmp;
static unsigned dir_entries_count;
static unsigned dir_i;
static unsigned dir_j;
static char dir_dt_buf[20];
static char dir_arg[256];
static char dir_drive[3];
static char dir_path_buf[256];
static char dir_mask_buf[256];
static char dev_label[16];
static char saved_cwd[128];
static char current_drive = '0';
static char cpm_path[256];
static char cpm_mask[256];
static char cpm_dest[256];
static char cpm_srcfile[256];
static char cpm_dstfile[256];
static char *cpm_args[3];
static char rm_path[256];
static char rm_mask[256];
static char rm_file[256];
static unsigned char bload_buf[128];
static char shell_end_marker;
extern struct _timezone _tz;

// example : int cmd_token(int, char **);
int cmd_help(int, char **);
int cmd_cls(int, char **);
int cmd_memx(int, char **);
int cmd_memr(int, char **);
int cmd_bload(int, char **);
int cmd_bsave(int, char **);
int cmd_brun(int, char **);
int cmd_dir(int, char **);
int cmd_drive(int, char **);
int cmd_drives(int, char **);
int cmd_list(int, char **);
int cmd_cd(int, char **);
int cmd_mkdir(int, char **);
int cmd_rm(int, char **);
int cmd_cp(int, char **);
int cmd_mv(int, char **);
int cmd_mem(int, char **);
int cmd_cm(int, char **);
int cmd_phi2(int, char **);
int cmd_chmod(int, char **);
int cmd_exit(int, char **);
int cmd_time(int, char **);
int cmd_stat(int, char **);

static const cmd_t commands[] = {
    // example : { "token",  "tests the tokenization", cmd_token },
    { "dir",    "active drive directory", cmd_dir},
    { "drive",  "set active drive", cmd_drive},
    { "drives", "shows available drives", cmd_drives},
    { "cd",     "change active directory", cmd_cd},
    { "mkdir",  "create directory", cmd_mkdir},
    { "chmod",  "set file attributes", cmd_chmod},
    { "cp",     "copy file", cmd_cp},
    { "cm",     "copy/move multiple files", cmd_cm},
    { "mv",     "moves/renames a file or directory", cmd_mv},
    { "rm",     "removes a file/files", cmd_rm},
    { "list",   "shows a file content", cmd_list},
    { "stat",   "returns file or directory info", cmd_stat},
    { "bload",  "load binary file to RAM/XRAM", cmd_bload},
    { "bsave",  "save RAM/XRAM to binary file", cmd_bsave},
    { "brun",   "load binary file to RAM and run", cmd_brun},
    { "mem",    "information about memory", cmd_mem},
    { "memx",   "reads xram", cmd_memx },
    { "memr",   "reads ram", cmd_memr },
    { "cls",    "clears terminal", cmd_cls },
    { "time",   "shows local time", cmd_time },
    { "phi2",   "shows CPU clock frequency", cmd_phi2},
    { "help",   "prints a list of commands", cmd_help },
    { "exit",   "exit to the system monitor", cmd_exit},
};

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

// Print a 32-bit value as 8 hex digits.
void tx_hex32(unsigned long val) {
    char out[8];
    int i;
    for(i = 7; i >= 0; i--) {
        out[i] = hexdigits[val & 0xF];
        val >>= 4;
    }
    tx_chars(out, sizeof(out));
}

/* Print 16-bit value as 4 hex digits */
void tx_hex16(uint16_t val) {
    char out[4];
    int i;
    for(i = 3; i >= 0; i--) {
        out[i] = hexdigits[val & 0xF];
        val >>= 4;
    }
    tx_chars(out, sizeof(out));
}

// Simple wildcard match supporting '*' (0+ chars) and '?' (1 char).
bool match_mask(const char *name, const char *mask) {
    const char *star = 0;
    const char *match = 0;
    while(*name) {
        if(*mask == '?' || *mask == *name) {
            mask++;
            name++;
            continue;
        }
        if(*mask == '*') {
            star = mask++;
            match = name;
            continue;
        }
        if(star) {
            mask = star + 1;
            name = ++match;
            continue;
        }
        return false;
    }
    while(*mask == '*') mask++;
    return *mask == 0;
}

// Print an unsigned long in decimal.
void tx_dec32(unsigned long val) {
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

// Format FAT date/time into YYYY-MM-DD hh:mm:ss
const char *format_fat_datetime(unsigned fdate, unsigned ftime) {
    unsigned year = 1980 + (fdate >> 9);
    unsigned month = (fdate >> 5) & 0xF;
    unsigned day = fdate & 0x1F;
    unsigned hour = ftime >> 11;
    unsigned min = (ftime >> 5) & 0x3F;
    unsigned sec = (ftime & 0x1F) * 2;

    dir_dt_buf[0]  = '0' + (year / 1000);
    dir_dt_buf[1]  = '0' + ((year / 100) % 10);
    dir_dt_buf[2]  = '0' + ((year / 10) % 10);
    dir_dt_buf[3]  = '0' + (year % 10);
    dir_dt_buf[4]  = '-';
    dir_dt_buf[5]  = '0' + (month / 10);
    dir_dt_buf[6]  = '0' + (month % 10);
    dir_dt_buf[7]  = '-';
    dir_dt_buf[8]  = '0' + (day / 10);
    dir_dt_buf[9]  = '0' + (day % 10);
    dir_dt_buf[10] = ' ';
    dir_dt_buf[11] = '0' + (hour / 10);
    dir_dt_buf[12] = '0' + (hour % 10);
    dir_dt_buf[13] = ':';
    dir_dt_buf[14] = '0' + (min / 10);
    dir_dt_buf[15] = '0' + (min % 10);
    dir_dt_buf[16] = ':';
    dir_dt_buf[17] = '0' + (sec / 10);
    dir_dt_buf[18] = '0' + (sec % 10);
    dir_dt_buf[19] = 0;
    return dir_dt_buf;
}

void ram_reader(uint8_t *buf, uint16_t addr, uint16_t size) {
    uint8_t *data = (uint8_t *)addr;
    for(; size; size--) *buf++ = *data++;
    return;
}

void ram_writer(const uint8_t *buf, uint16_t addr, uint16_t size) {
    uint8_t *dst = (uint8_t *)addr;
    for(; size; size--) *dst++ = *buf++;
}

void xram_reader(uint8_t *buf, uint16_t addr, uint16_t size) {
    RIA.step0 = 1;
    RIA.addr0 = addr;
    for(; size; size--) *buf++ = RIA.rw0;
    return;
}

void xram_writer(const uint8_t *buf, uint16_t addr, uint16_t size) {
    RIA.step0 = 1;
    RIA.addr0 = addr;
    for(; size; size--) RIA.rw0 = *buf++;
}

/* Lowest usable address for other programs: 0x0200 + shell size */
uint16_t mem_lo(void) {
    return 0x0200u + (uint16_t)((unsigned)&shell_end_marker);
}

uint16_t mem_top(void) {
    return (uint16_t)(MEMTOP);
}

uint16_t mem_free(void) {
    return (uint16_t)(MEMTOP - mem_lo() + 1);
}

// time display
void show_time(void) {
    time_t tnow;
    struct tm *tmnow;
    char buf[32];
    // tx_string("\x1b[s");       /* save cursor */
    // tx_string("\x1b[1;61H");   /* row 1, col 42 */
    if(time(&tnow) != (time_t)-1) {
        ria_tzset(tnow);       /* adjust TZ/DST in OS */
        tmnow = localtime(&tnow);
        if(tmnow) tmnow->tm_isdst = _tz.daylight; /* cc65 localtime DST fix */
    } else {
        tmnow = 0;
    }
    if(tmnow) {
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S %Z", tmnow);
        tx_string(buf);
    }
    // tx_string("\x1b[u");       /* restore cursor */
}

// Assumes str points to at least two bytes.
int hexstr(char *str, uint8_t val) {
    str[0] = hexdigits[val >> 4];
    str[1] = hexdigits[val & 0xF];
    return 2;
}

#define HEXDUMP_LINE_SIZE 16
void hexdump(uint16_t addr, uint16_t bytes, char_stream_func_t streamer, read_data_func_t reader) {
    int i;
    uint8_t data[HEXDUMP_LINE_SIZE];
    char string[HEXDUMP_LINE_SIZE * 3 + 32];

    while(bytes) {
        char *str = string;
        int rd = bytes > sizeof(data) ? sizeof(data) : bytes;
        str += hexstr(str, addr >> 8);
        str += hexstr(str, addr & 0xFF);
        *str++ = ':';
        reader(data, addr, rd);
        for(i = 0; i < rd; i++) {
            *str++ = ' ';
            str += hexstr(str, data[i]);
        }
        *str++ = ' ';
        for(i = 0; i < rd; i++) {
            char b = (data[i] >= 32 && data[i] <= 126) ? data[i] : '.';
            *str++ = b;
        }
        *str++ = CHAR_CR;
        *str++ = CHAR_LF;
        streamer(string, str - string);
        bytes -= rd;
        addr += rd;
    }
    return;
}

void clearterminal(){
    tx_string("\x1b" "c");
    return;
}

void cls(){
    clearterminal();
    tx_string("Picocomputer 6502 Shell (native environment)" 
              NEWLINE "--------------------------------------------------------------------------------" NEWLINE);
    return;
}

void prompt() {
    if(f_getcwd(dir_cwd, sizeof(dir_cwd)) >= 0) {
        if(dir_cwd[1] == ':') current_drive = dir_cwd[0];
        tx_string(dir_cwd);
        tx_string("> ");
    } else {
        tx_char(current_drive);
        tx_string(":> ");
    }
    return;
}

void help(){
    int i;
    tx_string(NEWLINE "Available commands (case sensitive):" NEWLINE);
    for(i = 0; i < ARRAY_SIZE(commands); i++) {
        tx_string(commands[i].cmd);
        tx_string(TAB);
        tx_string(commands[i].help);
        tx_string(NEWLINE);
    }
    tx_string("Keys:" NEWLINE);
    tx_string("<LEFT>\tchange active drive to previous" NEWLINE);
    tx_string("<RIGHT>\tchange active drive to next" NEWLINE);
    tx_string("<UP>\trecall last command" NEWLINE);
    tx_string("<DOWN>\ta directory of active drive" NEWLINE NEWLINE);
    return;
}

// Reformats the given buffer into tokens, delimited by spaces.  It places those
// tokens in the tokenList array (up to maxTokens).  The given buffer may be
// reformatted as part of this function so you'll want to make a copy of it 
// if you need the original version after this function.  Here are the rules
// it follows:
// - Leading and trailing spaces are ignored, a token may only star with a non
//   space unless a space is escaped (i.e. "\ token" will be tokenized as " token")
// - Additional spaces after the first space will be ignored (i.e. won't create empty tokens)
// - The backslash character causes the following character to be added to the token
//   regardless
// - The " or ' character will cause a quote to start and all characters until the
//   quote of the same type will be placed in a single token.  The backslash escape
//   is still followed, so you can add a quote character w/o exiting a quote block
//   by prefacing it with \ (e.g. "this is a \"test\"" will be interpreted as 
//   'this is a "test"')
int tokenize(char *buf, int maxBuf, char **tokenList, int maxTokens) {
    char *in = buf;
    char *out = buf;
    char *max = buf + maxBuf;
    bool escape = false;
    char quote = 0;
    int tokens = 0;
    int tokenLen = 0;

    while(in != max) {
        char c = *in;
        if(!c) break;
        in++;
        if(!escape) {
            // Check for an backslash escape code.
            if(c == '\\') {
                escape = true;
                continue;
            }
            if(quote) {
                // If we're in a quote, only a quote of the same type as the start
                // can exit the quote
                if(c == quote) {
                    quote = 0;
                    continue;
                }
            } else {
                // Not in a quote, so spaces are token delimiters
                if(c == ' ') {
                    if(!tokenLen) continue; // No token yet, ignore
                    *out++ = 0;
                    tokenLen = 0;
                    tokens++;
                    continue;
                // Check for a start of a quote and record the type
                } else if(c == '\'' || c == '"') {
                    quote = c;
                    continue;
                }
            }
        }

        // Ok, got a character we should put in a token.
        if(!tokenLen) {
            if(tokens == maxTokens) break; // No more space for tokens.
            tokenList[tokens] = out;
        }
        *out++ = c;
        tokenLen++;
        escape = false; // Just set escape to false because a normal character will always end an escape
    }
    // Make sure we account for the current token in progress (if there is one)
    if(tokenLen) {
        *out = 0;
        tokens++;
    }
    // Basic error signalling: unterminated quote or trailing escape.
    if(quote || escape) return -1;
    return tokens;
}

int execute(cmdline_t *cl) {
    int i;
    char *tokenList[CMD_TOKEN_MAX];
    int tokens = 0;
    cl->lastbytes = 0;
    // memcpy(cl->lastbuffer, cl->buffer, cl->bytes); cl->lastbytes = cl->bytes; cl->lastbuffer[cl->bytes] = 0;
    memcpy(cl->lastbuffer, cl->buffer, cl->bytes);
    cl->lastbytes = cl->bytes;
    cl->lastbuffer[cl->bytes] = 0;
    tokens = tokenize(cl->buffer, cl->bytes, tokenList, ARRAY_SIZE(tokenList));
    if(tokens <= 0) {
        if(tokens < 0) tx_string("Parse error: unterminated quote/escape" NEWLINE);
        return 0;
    }
    /* Allow selecting drive by typing e.g. "0:" directly */
    if(tokens == 1 &&
       tokenList[0][0] >= '0' && tokenList[0][0] <= '7' &&
       tokenList[0][1] == ':' && tokenList[0][2] == 0) {
        char *drv_args[2];
        drv_args[0] = (char *)"drive";
        drv_args[1] = tokenList[0];
        return cmd_drive(2, drv_args);
    }
    for(i = 0; i < ARRAY_SIZE(commands); i++) {
        if(!strcmp(tokenList[0], commands[i].cmd)) {
            return commands[i].func(tokens, tokenList);
        }
    }
    tx_string("Unknown command" NEWLINE);
    return -1;
}

int main(void) {
    char last_rx = 0;
    char ext_rx = 0;
    int i = 0;
    int v = 0;
    static cmdline_t cmdline = {0};
    char drv_args_buf[4] = {0};
    char *drv_args[2];
    drv_args[0] = (char *)"drive";
    drv_args[1] = drv_args_buf;
    f_chdrive("0:");
    current_drive = '0';
    if(f_getcwd(dir_cwd, sizeof(dir_cwd)) >= 0 && dir_cwd[1] == ':') {
        current_drive = dir_cwd[0];
    }
    cls();
    {
        char *args[1];
        args[0] = (char *)"";
        cmd_phi2(1, args);
    }
    show_time();
    tx_string(NEWLINE NEWLINE);
    {
        char *args[1];
        args[0] = (char *)"";
        cmd_drives(1, args);
    }
    prompt();

    v = RIA.vsync;
    while (1)
    {
        // if (RIA.vsync == v) show_clock();
        // v = RIA.vsync;

        if(RX_READY) {
            char rx = (char)RIA.rx;
            if(rx == CHAR_ESC){
                ext_rx = 1;
                continue;
            } else if(ext_rx == 1) {
                if(rx == CHAR_NCHR) {
                    ext_rx = 2;
                    continue;
                } else if(rx == 'O') { /* CSI-less F1 from some terminals */
                    ext_rx = 6;
                    continue;
                }
                ext_rx = 0;
            } else if(ext_rx == 2){
                if(rx == CHAR_UP){
                    ext_rx = 0;
                    tx_string("\r\x1b[K");
                    prompt();
                    cmdline.bytes = cmdline.lastbytes;
                    memcpy(cmdline.buffer, cmdline.lastbuffer, cmdline.lastbytes);
                    cmdline.buffer[cmdline.bytes] = 0;
                    tx_string(cmdline.buffer);
                    continue;
                } else if(rx == CHAR_DOWN){
                    ext_rx = 0;
                    {
                        char *args[1];
                        args[0] = (char *)"dir";
                        tx_string(NEWLINE);
                        cmd_dir(1, args);
                        prompt();
                    }
                    continue;
                } else if(rx == CHAR_LEFT || rx == CHAR_RIGHT) {
                    char next = current_drive;
                    if(rx == CHAR_LEFT) {
                        if(next > '0') next--;
                    } else {
                        if(next < '7') next++;
                    }
                    drv_args_buf[0] = next;
                    drv_args_buf[1] = ':';
                    drv_args_buf[2] = 0;
                    cmd_drive(2, drv_args);
                    tx_string("\r\x1b[K");
                    prompt();
                    ext_rx = 0;
                    continue;
                } else if(rx == '1') {
                    ext_rx = 3; // possible ctrl+arrow sequence
                    continue;
                } else if(rx == 'O') { /* F1 in xterm-style ESC O P */
                    ext_rx = 6;
                    continue;
                } else {
                    ext_rx = 0;
                }
            } else if(ext_rx == 3) {
                if(rx == ';') {
                    ext_rx = 4;
                    continue;
                }
                ext_rx = 0;
            } else if(ext_rx == 4) {
                if(rx == '5') {
                    ext_rx = 5;
                    continue;
                }
                ext_rx = 0;
            } else if(ext_rx == 5) {
                ext_rx = 0;
                if(rx == CHAR_DOWN) {
                    char *args[1];
                    args[0] = (char *)"dir";
                    tx_string(NEWLINE);
                    cmd_dir(1, args);
                    prompt();
                    continue;
                }
            } else if(ext_rx == 6) {
                /* Expecting CHAR_F1 */
                ext_rx = 0;
                if(rx == CHAR_F1 || rx == 'P') {
                    char *args[1];
                    args[0] = (char *)"help";
                    tx_string(NEWLINE);
                    cmd_help(1, args);
                    prompt();
                    continue;
                }
            // Normal character, just put it on the pile.
            } else if(rx >= 32 && rx <= 126) {
                ext_rx = 0;
                if(cmdline.bytes == CMD_BUF_MAX) {
                    tx_char(0x7); // if the buffer is full, send a bell
                } else {
                    cmdline.buffer[cmdline.bytes++] = rx;
                    cmdline.buffer[cmdline.bytes] = 0;
                    tx_char(rx);
                }
            // Backspace
            } else if(rx == CHAR_BS) {
                ext_rx = 0;
                if(cmdline.bytes) {
                    cmdline.bytes--;
                    cmdline.buffer[cmdline.bytes] = 0;
                    tx_char(CHAR_BS);
                    tx_char(' ');
                    tx_char(CHAR_BS);
                } else {
                    tx_char(CHAR_BELL);
                }
            // Enter
            } else if(rx == CHAR_CR || rx == CHAR_LF) {
                ext_rx = 0;
                if(rx == CHAR_LF && last_rx == CHAR_CR) continue; // Ignore CRLF
                tx_string(NEWLINE);
                if(cmdline.bytes){
                    // cmdline.lastbytes = cmdline.bytes;
                    execute(&cmdline);
                    cmdline.bytes = 0;
                    cmdline.buffer[0] = 0;
                }
                prompt();
            } else {
                ext_rx = 0;
            }
            last_rx = rx; // Last line in RX_READY
        }
    }
}

int cmd_help(int, char **) {
    help();
    return 0;
}

int cmd_cls(int, char **) {
    cls();
    return 0;
}

int cmd_exit(int status, char **) {
    tx_string(NEWLINE NEWLINE "Exit to Picocomputer 6502 monitor." NEWLINE "Bye, bye !" NEWLINE NEWLINE);
    exit(status);
    return 0;
}

int cmd_cd(int argc, char **argv) {
    if(argc < 2) {
        if(f_getcwd(dir_cwd, sizeof(dir_cwd)) >= 0) {
            tx_string(dir_cwd);
            tx_string(NEWLINE);
        } else {
            tx_string("getcwd failed" NEWLINE);
            return -1;
        }
        return 0;
    }
    if(chdir(argv[1]) < 0) {
        tx_string("chdir failed" NEWLINE);
        return -1;
    }
    if(f_getcwd(dir_cwd, sizeof(dir_cwd)) >= 0 && dir_cwd[1] == ':') {
        current_drive = dir_cwd[0];
    }
    return 0;
}

int cmd_drives(int argc, char **argv) {
    int rc;
    char drv[3] = "0:";
    unsigned i;
    char saved_drive = '0';
    const char *saved_path = "/";
    unsigned long free_blks = 0;
    unsigned long total_blks = 0;
    unsigned long pct = 0;
    (void)argc; (void)argv;

    if(f_getcwd(saved_cwd, sizeof(saved_cwd)) < 0) {
        tx_string("getcwd failed" NEWLINE);
        return -1;
    }
    if(saved_cwd[1] == ':') {
        saved_drive = saved_cwd[0];
        saved_path = saved_cwd + 2;
        if(!*saved_path) saved_path = "/";
    }

    tx_string("drive " TAB "label           " TAB "[MB]" TAB "free" NEWLINE
              "------" TAB "----------------" TAB "------" TAB "----" NEWLINE);
    for(i = 0; i < 8; i++) {
        drv[0] = '0' + i;
        rc = f_chdrive(drv);
        if(rc == 0) {
            if(f_getfree(drv, &free_blks, &total_blks) != 0 || !total_blks) {
                continue; /* Skip drives without size info */
            }
            tx_string("USB");
            tx_string(drv);
            tx_string(TAB);
            if(f_getlabel(drv, dev_label) >= 0) {
                unsigned len = strlen(dev_label);
                tx_string(dev_label);
                while(len < 16) { tx_char(' '); len++; }
            } else {
                tx_string("(no label)       ");
            }
            tx_string(TAB);
            {
                unsigned long mb = total_blks / 2048; /* 512-byte blocks -> MB */
                pct = (free_blks * 100UL) / total_blks;
                tx_dec32(mb);
                tx_string(TAB);
                tx_dec32(pct);
                tx_char('%');
            }
            tx_string(NEWLINE);
        }
    }
    tx_string(NEWLINE);

    drv[0] = saved_drive;
    drv[1] = ':';
    drv[2] = 0;
    if(f_chdrive(drv) == 0) {
        chdir(saved_path);
        current_drive = drv[0];
    }
    return 0;
}

int cmd_drive(int argc, char **argv) {
    char drv[3] = "0:";
    char prev_drive = current_drive;
    char prev_path[128];
    prev_path[0] = 0;
    if(f_getcwd(prev_path, sizeof(prev_path)) < 0) prev_path[0] = 0;
    if(argc < 2 || strlen(argv[1]) < 2 || argv[1][1] != ':' || argv[1][0] < '0' || argv[1][0] > '7') {
        tx_string("Usage: drive 0:-7:" NEWLINE);
        return 0;
    }
    drv[0] = argv[1][0];
    if(f_chdrive(drv) < 0) {
        tx_string("Invalid drive" NEWLINE);
        if(prev_path[0]) {
            drv[0] = prev_drive;
            f_chdrive(drv);
            chdir(prev_path);
        }
        return -1;
    }
    /* Verify drive is actually usable */
    if(f_getcwd(dir_cwd, sizeof(dir_cwd)) < 0) {
        tx_string("Drive not available" NEWLINE);
        drv[0] = prev_drive;
        if(f_chdrive(drv) == 0 && prev_path[0]) chdir(prev_path);
        return -1;
    }
    current_drive = drv[0];
    return 0;
}

int cmd_list(int argc, char **argv) {
    char buf[128];
    int fd;
    int n;
    if(argc < 2) {
        tx_string("Usage: list <file>" NEWLINE);
        return 0;
    }
    fd = open(argv[1], O_RDONLY);
    if(fd < 0) {
        tx_string("Cannot open file" NEWLINE);
        return -1;
    }
    tx_string("----------------------------------------" NEWLINE);
    while((n = read(fd, buf, sizeof(buf))) > 0) {
        int idx;
        for(idx = 0; idx < n; idx++) {
            char ch = buf[idx];
            if(ch == '\n') {
                tx_char('\r');
                tx_char('\n');
            } else {
                tx_char(ch);
            }
        }
    }
    close(fd);
    tx_string(NEWLINE "----------------------------------------" NEWLINE);
    return 0;
}

int cmd_mkdir(int argc, char **argv) {
    if(argc < 2) {
        tx_string("Usage: mkdir <path>" NEWLINE);
        return 0;
    }
    if(f_mkdir(argv[1]) < 0) {
        tx_string("mkdir failed" NEWLINE);
        return -1;
    }
    return 0;
}

int cmd_rm(int argc, char **argv) {
    int i;
    int rc = 0;
    if(argc < 2) {
        tx_string("Usage: rm <file|directory> [more...]" NEWLINE);
        return 0;
    }
    for(i = 1; i < argc; i++) {
        const char *arg = argv[i];
        const char *p = arg;
        char *last_sep = 0;
        int has_wild = 0;
        while(*p) {
            if(*p == '*' || *p == '?') { has_wild = 1; break; }
            p++;
        }
        if(!has_wild) {
            if(unlink(arg) < 0) {
                tx_string("rm failed: ");
                tx_string(arg);
                tx_string(NEWLINE);
                rc = -1;
            }
            continue;
        }

        /* wildcard handling: split path/mask */
        strcpy(rm_path, ".");
        strcpy(rm_mask, "*.*");
        strcpy(rm_file, arg);
        p = rm_file;
        if(*p == '/' || *p == '\\') p++;
        while(*p) {
            if(*p == '/' || *p == '\\') last_sep = (char *)p;
            p++;
        }
        if(last_sep) {
            *last_sep = 0;
            strcpy(rm_path, rm_file);
            strcpy(rm_mask, last_sep + 1);
        } else {
            strcpy(rm_mask, rm_file);
            strcpy(rm_path, ".");
        }
        if(!*rm_mask) strcpy(rm_mask, "*.*");

        {
            int dd = f_opendir(rm_path);
            if(dd < 0) {
                tx_string("opendir failed" NEWLINE);
                rc = -1;
                continue;
            }
            while(1) {
                int rr = f_readdir(&dir_ent, dd);
                if(rr < 0) {
                    tx_string("readdir failed" NEWLINE);
                    rc = -1;
                    break;
                }
                if(!dir_ent.fname[0]) break;
                if(dir_ent.fattrib & AM_DIR) continue; /* skip dirs */
                if(!match_mask(dir_ent.fname, rm_mask)) continue;

                if(!strcmp(rm_path, ".") || !rm_path[0]) {
                    strcpy(rm_file, dir_ent.fname);
                } else {
                    strcpy(rm_file, rm_path);
                    strcat(rm_file, "/");
                    strcat(rm_file, dir_ent.fname);
                }
                if(unlink(rm_file) < 0) {
                    tx_string("rm failed: ");
                    tx_string(rm_file);
                    tx_string(NEWLINE);
                    rc = -1;
                }
            }
            f_closedir(dd);
        }
    }
    return rc;
}

int cmd_cp(int argc, char **argv) {
    char buf[128];
    int src, dst;
    int n;
    if(argc < 3) {
        tx_string("Usage: cp <src> <dst>" NEWLINE);
        return 0;
    }
    src = open(argv[1], O_RDONLY);
    if(src < 0) {
        tx_string("Cannot open source" NEWLINE);
        return -1;
    }
    dst = open(argv[2], O_WRONLY | O_CREAT | O_TRUNC);
    if(dst < 0) {
        tx_string("Cannot open dest" NEWLINE);
        close(src);
        return -1;
    }
    while((n = read(src, buf, sizeof(buf))) > 0) {
        if(write(dst, buf, n) != n) {
            tx_string("Write error" NEWLINE);
            close(src);
            close(dst);
            return -1;
        }
    }
    close(src);
    close(dst);
    return 0;
}

int cmd_mv(int argc, char **argv) {
    if(argc < 3) {
        tx_string("Usage: mv <src> <dst>" NEWLINE);
        return 0;
    }
    if(rename(argv[1], argv[2]) < 0) {
        tx_string("mv failed" NEWLINE);
        return -1;
    }
    return 0;
}

int cmd_bload(int argc, char **argv) {
    int fd;
    int n;
    uint16_t addr;
    int use_xram = 0;
    unsigned long total = 0;
    if(argc < 3) {
        tx_string("Usage: bload <file> <addr> [/x]" NEWLINE);
        return 0;
    }
    fd = open(argv[1], O_RDONLY);
    if(fd < 0) {
        tx_string("Cannot open file" NEWLINE);
        return -1;
    }
    addr = (uint16_t)strtoul(argv[2], NULL, 16);
    if(argc > 3 && strcmp(argv[3], "/x") == 0) use_xram = 1;

    while((n = read(fd, bload_buf, sizeof(bload_buf))) > 0) {
        if(use_xram) xram_writer(bload_buf, addr, n);
        else ram_writer(bload_buf, addr, n);
        addr += (uint16_t)n;
        total += (unsigned long)n;
    }
    close(fd);
    if(n < 0) {
        tx_string("Read error" NEWLINE);
        return -1;
    }
    tx_string("Bytes loaded: ");
    tx_dec32(total);
    tx_string(NEWLINE);
    return 0;
}

int cmd_bsave(int argc, char **argv) {
    int fd;
    uint16_t addr;
    uint16_t size;
    int use_xram = 0;
    unsigned long total = 0;
    if(argc < 4) {
        tx_string("Usage: bsave <file> <addr> <size> [/x]" NEWLINE);
        return 0;
    }
    fd = open(argv[1], O_WRONLY | O_CREAT | O_TRUNC);
    if(fd < 0) {
        tx_string("Cannot open file" NEWLINE);
        return -1;
    }
    addr = (uint16_t)strtoul(argv[2], NULL, 16);
    size = (uint16_t)strtoul(argv[3], NULL, 0);
    if(argc > 4 && strcmp(argv[4], "/x") == 0) use_xram = 1;

    while(size) {
        int chunk = size > sizeof(bload_buf) ? sizeof(bload_buf) : size;
        if(use_xram) {
            xram_reader(bload_buf, addr, chunk);
        } else {
            ram_reader(bload_buf, addr, chunk);
        }
        if(write(fd, bload_buf, chunk) != chunk) {
            tx_string("Write error" NEWLINE);
            close(fd);
            return -1;
        }
        addr += (uint16_t)chunk;
        size -= (uint16_t)chunk;
        total += (unsigned long)chunk;
    }
    close(fd);
    tx_string("Bytes saved: ");
    tx_dec32(total);
    tx_string(NEWLINE);
    return 0;
}

int cmd_brun(int argc, char **argv) {
    int fd;
    int n;
    uint16_t addr;
    uint16_t start;
    void (*fn)(void);
    if(argc < 3) {
        tx_string("Usage: brun <file> <addr>" NEWLINE);
        return 0;
    }
    fd = open(argv[1], O_RDONLY);
    if(fd < 0) {
        tx_string("Cannot open file" NEWLINE);
        return -1;
    }
    start = (uint16_t)strtoul(argv[2], NULL, 16);
    addr = start;

    while((n = read(fd, bload_buf, sizeof(bload_buf))) > 0) {
        ram_writer(bload_buf, addr, n);
        addr += (uint16_t)n;
    }
    close(fd);
    if(n < 0) {
        tx_string("Read error" NEWLINE);
        return -1;
    }
    // tx_string("Bytes loaded: ");
    // tx_dec32((unsigned long)(addr - start));
    clearterminal();
    fn = (void (*)(void))start;
    fn();
    return 0;
}

int cmd_cm(int argc, char **argv) {
    int dirdes = -1;
    int mv_mode = 0;
    int rc = 0;
    int count = 0;
    if(argc < 3) {
        tx_string("Usage: cm <mask> <dest_dir> [/m]" NEWLINE);
        return 0;
    }
    strcpy(cpm_dest, argv[2]);
    if(argc > 3 && !strcmp(argv[3], "/m")) mv_mode = 1;

    /* Split mask into path and wildcard */
    {
        char *p = argv[1];
        char *last_sep = 0;
        strcpy(cpm_path, ".");
        strcpy(cpm_mask, "*.*");
        strcpy(cpm_srcfile, p); /* reuse as temp */
        if(*p == '/' || *p == '\\') p++;
        while(*p) {
            if(*p == '/' || *p == '\\') last_sep = p;
            p++;
        }
        if(last_sep) {
            *last_sep = 0;
            strcpy(cpm_path, argv[1]);
            strcpy(cpm_mask, last_sep + 1);
        } else {
            strcpy(cpm_mask, argv[1]);
            strcpy(cpm_path, ".");
        }
        if(!*cpm_mask) strcpy(cpm_mask, "*.*");
    }

    dirdes = f_opendir(cpm_path);
    if(dirdes < 0) {
        tx_string("opendir failed" NEWLINE);
        return -1;
    }

    /* Pass 1: count matching files */
    while(1) {
        rc = f_readdir(&dir_ent, dirdes);
        if(rc < 0) {
            tx_string("readdir failed" NEWLINE);
            break;
        }
        if(!dir_ent.fname[0]) break;
        if(dir_ent.fattrib & AM_DIR) continue;
        if(!match_mask(dir_ent.fname, cpm_mask)) continue;
        count++;
    }
    f_closedir(dirdes);
    if(rc < 0) return -1;
    tx_string("Files to process: ");
    tx_dec32(count);
    tx_string(NEWLINE);
    if(!count) return 0;

    dirdes = f_opendir(cpm_path);
    if(dirdes < 0) {
        tx_string("opendir failed" NEWLINE);
        return -1;
    }

    while(1) {
        rc = f_readdir(&dir_ent, dirdes);
        if(rc < 0) {
            tx_string("readdir failed" NEWLINE);
            break;
        }
        if(!dir_ent.fname[0]) break;
        if(dir_ent.fattrib & AM_DIR) continue; /* skip directories */
        if(!match_mask(dir_ent.fname, cpm_mask)) continue;

        /* build src path */
        if(!strcmp(cpm_path, ".") || !cpm_path[0]) {
            strcpy(cpm_srcfile, dir_ent.fname);
        } else {
            strcpy(cpm_srcfile, cpm_path);
            strcat(cpm_srcfile, "/");
            strcat(cpm_srcfile, dir_ent.fname);
        }
        /* build dst path */
        strcpy(cpm_dstfile, cpm_dest);
        if(cpm_dest[0] && cpm_dest[strlen(cpm_dest)-1] != '/' && cpm_dest[strlen(cpm_dest)-1] != '\\')
            strcat(cpm_dstfile, "/");
        strcat(cpm_dstfile, dir_ent.fname);

        if(mv_mode){
            tx_string("moving ");
        } else {
            tx_string("copying ");
        }
        tx_string(cpm_srcfile);
        tx_string(" -> ");
        tx_string(cpm_dstfile);
        tx_string(NEWLINE);

        cpm_args[0] = "cp";
        cpm_args[1] = cpm_srcfile;
        cpm_args[2] = cpm_dstfile;
        rc = cmd_cp(3, cpm_args);
        if(rc < 0) break;
        if(mv_mode) {
            if(unlink(cpm_srcfile) < 0) {
                tx_string("mv cleanup failed" NEWLINE);
                rc = -1;
                break;
            }
        }
    }

    f_closedir(dirdes);
    return (rc < 0) ? -1 : 0;
}

int cmd_memx(int argc, char **argv) {
    uint16_t addr = 0;
    uint16_t size = 16;

    if(argc < 2) {
        tx_string("Usage: memx addr [bytes]" NEWLINE);
        return 0;
    }
    addr = strtoul(argv[1], NULL, 16);
    if(argc > 2) size = strtoul(argv[2], NULL, 0);

    hexdump(addr, size, tx_chars, xram_reader);
    return 0;
}

int cmd_memr(int argc, char **argv) {
    uint16_t addr = 0;
    uint16_t size = 16;

    if(argc < 2) {
        tx_string("Usage: memr addr [bytes]" NEWLINE);
        return 0;
    }
    addr = strtoul(argv[1], NULL, 16);
    if(argc > 2) size = strtoul(argv[2], NULL, 0);

    hexdump(addr, size, tx_chars, ram_reader);
    return 0;
}

int cmd_dir(int argc, char **argv) {
    const char *mask = "*.*";
    const char *path = ".";
    char *p;
    int dirdes = -1;
    int rc = 0;

    dir_drive[0] = dir_drive[1] = dir_drive[2] = 0;
    if(argc >= 2) {
        strcpy(dir_arg, argv[1]);
        // Handle drive prefix like "0:" or "A:"
        if(dir_arg[1] == ':') {
            if(dir_arg[0] < '0' || dir_arg[0] > '7') {
                tx_string("Invalid drive" NEWLINE);
                return -1;
            }
            dir_drive[0] = dir_arg[0];
            dir_drive[1] = ':';
            dir_drive[2] = 0;
            if(f_chdrive(dir_drive) < 0) {
                tx_string("Invalid drive" NEWLINE);
                return -1;
            }
            current_drive = dir_drive[0];
            p = dir_arg + 2;
        } else {
            p = dir_arg;
        }
        if(*p == '/' || *p == '\\') p++;
        if(*p) {
            char *last_sep = 0;
            char *iter = p;
            while(*iter) {
                if(*iter == '/' || *iter == '\\') last_sep = iter;
                iter++;
            }
            if(last_sep) {
                *last_sep = 0;
                mask = last_sep + 1;
                path = (*p) ? p : ".";
            } else {
                mask = p;
                path = ".";
            }
        } else {
            mask = "*.*";
            path = ".";
        }
        if(!*mask) mask = "*.*";
    }

    // Copy path/mask into static buffers for reuse.
    strcpy(dir_path_buf, path);
    strcpy(dir_mask_buf, mask);

    if(f_getcwd(dir_cwd, sizeof(dir_cwd)) < 0) {
        tx_string("getcwd failed" NEWLINE);
        return -1;
    }

    tx_string(NEWLINE "Directory: ");
    tx_string(dir_cwd);
    tx_string(NEWLINE NEWLINE);

    dirdes = f_opendir(dir_path_buf[0] ? dir_path_buf : ".");
    if(dirdes < 0) {
        tx_string("opendir failed" NEWLINE);
        return -1;
    }

    dir_entries_count = 0;
    while(1) {
        rc = f_readdir(&dir_ent, dirdes);
        if(rc < 0) {
            tx_string("readdir failed" NEWLINE);
            break;
        }
        if(!dir_ent.fname[0]) break; // No more entries
        if(dir_entries_count == DIR_LIST_MAX) {
            tx_string("Directory listing truncated, too many entries, use wildcards to narrow results" NEWLINE);
            break;
        }

        // Apply mask only to files; always include directories so they are visible.
        if(!(dir_ent.fattrib & AM_DIR)) {
            if(!match_mask(dir_ent.fname, dir_mask_buf)) continue;
        }

        strcpy(dir_entries[dir_entries_count].name, dir_ent.fname);
        dir_entries[dir_entries_count].fsize = dir_ent.fsize;
        dir_entries[dir_entries_count].fattrib = dir_ent.fattrib;
        dir_entries[dir_entries_count].fdate = dir_ent.fdate;
        dir_entries[dir_entries_count].ftime = dir_ent.ftime;
        dir_entries_count++;
    }

    if(f_closedir(dirdes) < 0 && rc >= 0) {
        tx_string("closedir failed" NEWLINE);
        rc = -1;
    }

    if(rc < 0) return -1;

    // Sort: files first, directories after, each group alphabetically.
    for(dir_i = 0; dir_i < dir_entries_count; dir_i++) {
        for(dir_j = dir_i + 1; dir_j < dir_entries_count; dir_j++) {
            unsigned a_dir = dir_entries[dir_i].fattrib & AM_DIR;
            unsigned b_dir = dir_entries[dir_j].fattrib & AM_DIR;
            int swap = 0;
            if(a_dir != b_dir) {
                // Files (a_dir==0) come before directories.
                if(a_dir && !b_dir) swap = 1;
            } else {
                if(strcmp(dir_entries[dir_i].name, dir_entries[dir_j].name) > 0) swap = 1;
            }
            if(swap) {
                dir_tmp = dir_entries[dir_i];
                dir_entries[dir_i] = dir_entries[dir_j];
                dir_entries[dir_j] = dir_tmp;
            }
        }
    }

    for(dir_i = 0; dir_i < dir_entries_count; dir_i++) {
        unsigned name_len;
        if(dir_entries[dir_i].fattrib & AM_DIR) {
            tx_char('[');
            tx_string(dir_entries[dir_i].name);
            tx_char(']');
            name_len = strlen(dir_entries[dir_i].name) + 2; // brackets
        } else {
            tx_string(dir_entries[dir_i].name);
            name_len = strlen(dir_entries[dir_i].name);
        }
        while(name_len < 32) {
            tx_char(' ');
            name_len++;
        }
        tx_char('\t');
        tx_string(format_fat_datetime(dir_entries[dir_i].fdate, dir_entries[dir_i].ftime));
        tx_char('\t');
        if(dir_entries[dir_i].fattrib & AM_DIR) {
            tx_string("<DIR>");
        } else {
            tx_dec32(dir_entries[dir_i].fsize);
        }
        tx_string(NEWLINE);
    }
    tx_string(NEWLINE);
    return 0;
}

int cmd_time(int argc, char **argv) {
    (void)argc; (void)argv;
    tx_string(NEWLINE);
    show_time();
    tx_string(NEWLINE NEWLINE);
    return 0;
}

int cmd_phi2(int argc, char **argv) {
    int hz = phi2();
    (void)argc; (void)argv;
    tx_string("65C02 clock speed: ");
    tx_dec32(hz);
    tx_string(" Hz" NEWLINE);
    return 0;
}

int cmd_mem(int argc, char **argv) {
    uint16_t bottom = mem_lo();
    uint16_t top = mem_top();
    uint16_t free = (uint16_t)(top - bottom + 1);
    (void)argc; (void)argv;
    tx_string(NEWLINE "Memory available for user programs" NEWLINE NEWLINE);
    tx_string("range:        0x");
    tx_hex16(bottom);
    tx_string(" ... 0x");
    tx_hex16(top);
    tx_string(NEWLINE);
    tx_string("size [bytes]: ");
    tx_dec32(free);
    tx_string(NEWLINE NEWLINE);
    return 0;
}

int cmd_stat(int argc, char **argv) {
    if(argc < 2) {
        tx_string("Usage: stat <path>" NEWLINE);
        return 0;
    }
    if(f_stat(argv[1], &dir_ent) < 0) {
        tx_string("stat failed" NEWLINE);
        return -1;
    }
    tx_string("Name   : ");
    tx_string(dir_ent.fname);
    tx_string(NEWLINE);
    tx_string("Short  : ");
    tx_string(dir_ent.altname);
    tx_string(NEWLINE);
    tx_string("Size   : ");
    tx_dec32(dir_ent.fsize);
    tx_string(" bytes" NEWLINE);
    tx_string("Attr   : ");
    tx_char((dir_ent.fattrib & AM_RDO) ? 'R' : '-');
    tx_char((dir_ent.fattrib & AM_HID) ? 'H' : '-');
    tx_char((dir_ent.fattrib & AM_SYS) ? 'S' : '-');
    tx_char((dir_ent.fattrib & AM_VOL) ? 'V' : '-');
    tx_char((dir_ent.fattrib & AM_DIR) ? 'D' : '-');
    tx_char((dir_ent.fattrib & AM_ARC) ? 'A' : '-');
    tx_string(NEWLINE);
    tx_string("Mod    : ");
    tx_string(format_fat_datetime(dir_ent.fdate, dir_ent.ftime));
    tx_string(NEWLINE NEWLINE);
    return 0;
}

int cmd_chmod(int argc, char **argv) {
    unsigned set = 0;
    unsigned clear = 0;
    int i;
    if(argc < 3) {
        tx_string("Usage: chmod <path> <flags...>" NEWLINE);
        tx_string("Use R+/R- H+/H- S+/S- A+/A- for read-only/hidden/system/archive" NEWLINE);
        return 0;
    }
    for(i = 2; i < argc; i++) {
        char c = argv[i][0];
        char op = argv[i][1];
        if(argv[i][2] != 0 || (op != '+' && op != '-')) continue;
        switch(c) {
            case 'R': if(op == '+') set |= AM_RDO; else clear |= AM_RDO; break;
            case 'H': if(op == '+') set |= AM_HID; else clear |= AM_HID; break;
            case 'S': if(op == '+') set |= AM_SYS; else clear |= AM_SYS; break;
            case 'A': if(op == '+') set |= AM_ARC; else clear |= AM_ARC; break;
            default: break;
        }
    }
    if(set == 0 && clear == 0) {
        tx_string("No attributes to change" NEWLINE);
        return 0;
    }
    if(f_chmod(argv[1], set, clear | set) < 0) {
        tx_string("chmod failed" NEWLINE);
        return -1;
    }
    return 0;
}

/*
// shell command scaffolding
int cmd_token(int argc, char **argv) {
    int i;
    for(i = 0; i < argc; i++) {
        printf("%d: [%s]" NEWLINE, i, argv[i]);
    }
    return 0;
}
*/
