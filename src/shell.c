/**
 * @file      main.c
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Howard Logic.  All rights reserved.
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

#define TAB         "\t"
#define NEWLINE     "\r\n"

#define ARRAY_SIZE(x) (sizeof(x) / sizeof(x[0]))

#ifndef AM_DIR
#define AM_DIR 0x10
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

// example : int cmd_token(int, char **);
int cmd_help(int, char **);
int cmd_cls(int, char **);
int cmd_mx(int, char **);
int cmd_mr(int, char **);
int cmd_dir(int, char **);
int cmd_drive(int, char **);
int cmd_drives(int, char **);
int cmd_list(int, char **);
int cmd_cd(int, char **);
int cmd_quit(int, char **);

static const cmd_t commands[] = {
    // example : { "token",  "tests the tokenization", cmd_token },
    { "help",   "print a list of commands", cmd_help },
    { "cls",    "clear terminal", cmd_cls },
    { "mx",     "reads xram", cmd_mx },
    { "mr",     "reads ram", cmd_mr },
    { "dir",    "current drive directory", cmd_dir},
    { "drive",  "set current drive", cmd_drive},
    { "drives", "list available drives", cmd_drives},
    { "list",   "print text file", cmd_list},
    { "cd",     "change directory", cmd_cd},
    { "quit",   "quit shell to system monitor", cmd_quit},
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

void xram_reader(uint8_t *buf, uint16_t addr, uint16_t size) {
    RIA.step0 = 1;
    RIA.addr0 = addr;
    for(; size; size--) *buf++ = RIA.rw0;
    return;
}

void ram_reader(uint8_t *buf, uint16_t addr, uint16_t size) {
    uint8_t *data = (uint8_t *)addr;
    for(; size; size--) *buf++ = *data++;
    return;
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

void cls(){
    tx_string("\x1b" "c" "Picocomputer 6502 Shell" 
                 NEWLINE "--------------------------------------------------------------------------------"
                 NEWLINE "now you are in native 6502 environment"
                 NEWLINE "type \"help\" for available commands" NEWLINE NEWLINE);
    return;
}

void prompt() {
    if(f_getcwd(dir_cwd, sizeof(dir_cwd)) >= 0 && dir_cwd[1] == ':') {
        current_drive = dir_cwd[0];
    }
    tx_char(current_drive);
    tx_string(":> ");
    return;
}

void help(){
    int i;
    tx_string("Available commands:" NEWLINE);
    for(i = 0; i < ARRAY_SIZE(commands); i++) {
        tx_string(commands[i].cmd);
        tx_string(TAB);
        tx_string(commands[i].help);
        tx_string(NEWLINE);
    }
    tx_string("Keyboard:" NEWLINE);
    tx_string("up\trecall last command" NEWLINE);
    tx_string("down\thelp" NEWLINE);
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
    static cmdline_t cmdline = {0};
    if(f_getcwd(dir_cwd, sizeof(dir_cwd)) >= 0 && dir_cwd[1] == ':') {
        current_drive = dir_cwd[0];
    }
    cls();
    prompt();
    while(1) {
        if(RX_READY) {
            char rx = (char)RIA.rx;
            if(rx == CHAR_ESC){
                ext_rx = 1;
            } else if(rx == CHAR_NCHR && ext_rx == 1) {
                ext_rx = 2;
            } else if(ext_rx == 2){
                ext_rx = 0;
                if(rx == CHAR_UP){
                    // Up
                    /* cmdline.bytes = 0;
                    for (i = 0; i < cmdline.lastbytes; i++){
                        cmdline.buffer[cmdline.bytes++] = cmdline.lastbuffer[i];
                        tx_char(cmdline.buffer[i]);
                    } */
                    tx_string("\r\x1b[K");
                    prompt();
                    cmdline.bytes = cmdline.lastbytes;
                    memcpy(cmdline.buffer, cmdline.lastbuffer, cmdline.lastbytes);
                    cmdline.buffer[cmdline.bytes] = 0;
                    tx_string(cmdline.buffer);
                } else if(rx == CHAR_DOWN){
                    // Down
                    tx_string(NEWLINE);
                    help();
                    prompt();
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

int cmd_help(int, char **) {
    help();
    return 0;
}

int cmd_cls(int, char **) {
    cls();
    return 0;
}

int cmd_quit(int, char **) {
    quit();
    return 0;
}

int cmd_cd(int argc, char **argv) {
    if(argc < 2) {
        tx_string("Usage: cd <path>" NEWLINE);
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

    for(i = 0; i < 8; i++) {
        drv[0] = '0' + i;
        rc = f_chdrive(drv);
        if(rc == 0) {
            tx_string(drv);
            tx_char('\t');
            if(f_getlabel(drv, dev_label) >= 0) {
                tx_string(dev_label);
            } else {
                tx_string("(no label)");
            }
            tx_string(NEWLINE);
        }
    }

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
    if(argc < 2) {
        if(f_getcwd(dir_cwd, sizeof(dir_cwd)) >= 0) {
            tx_string("Current drive: ");
            tx_char(dir_cwd[0]);
            tx_string(":" NEWLINE);
            current_drive = dir_cwd[0];
        } else {
            tx_string("getcwd failed" NEWLINE);
            return -1;
        }
        return 0;
    }
    if(strlen(argv[1]) < 2 || argv[1][1] != ':' || argv[1][0] < '0' || argv[1][0] > '7') {
        tx_string("Usage: drive 0:-7:" NEWLINE);
        return 0;
    }
    drv[0] = argv[1][0];
    if(f_chdrive(drv) < 0) {
        tx_string("Invalid drive" NEWLINE);
        return -1;
    }
    current_drive = drv[0];
    if(f_getcwd(dir_cwd, sizeof(dir_cwd)) >= 0) {
        tx_string("Current drive: ");
        tx_char(current_drive);
        tx_string(":" NEWLINE);
    }
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
    while((n = read(fd, buf, sizeof(buf))) > 0) {
        tx_chars(buf, n);
    }
    close(fd);
    tx_string(NEWLINE NEWLINE);
    return 0;
}

int cmd_mx(int argc, char **argv) {
    uint16_t addr = 0;
    uint16_t size = 16;

    if(argc < 2) {
        tx_string("Usage: xr addr [bytes]" NEWLINE);
        return 0;
    }
    addr = strtoul(argv[1], NULL, 16);
    if(argc > 2) size = strtoul(argv[2], NULL, 0);

    hexdump(addr, size, tx_chars, xram_reader);
    return 0;
}

int cmd_mr(int argc, char **argv) {
    uint16_t addr = 0;
    uint16_t size = 16;

    if(argc < 2) {
        tx_string("Usage: mr addr [bytes]" NEWLINE);
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

    tx_string("Directory: ");
    tx_string(dir_cwd);
    tx_string(NEWLINE);

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
            tx_string("Directory listing truncated" NEWLINE);
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
        if(dir_entries[dir_i].fattrib & AM_DIR) {
            tx_char('[');
            tx_string(dir_entries[dir_i].name);
            tx_string("]" NEWLINE);
        } else {
            unsigned name_len = strlen(dir_entries[dir_i].name);
            tx_string(dir_entries[dir_i].name);
            while(name_len < 32) {
                tx_char(' ');
                name_len++;
            }
            tx_char('\t');
            tx_string(format_fat_datetime(dir_entries[dir_i].fdate, dir_entries[dir_i].ftime));
            tx_char('\t');
            tx_dec32(dir_entries[dir_i].fsize);
            tx_string(NEWLINE);
        }
    }

    return 0;
}
