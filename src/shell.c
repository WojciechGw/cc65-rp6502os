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

#define NEWLINE     "\r\n"

#define ARRAY_SIZE(x) (sizeof(x) / sizeof(x[0]))

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

// example : int cmd_token(int, char **);
int cmd_help(int, char **);
int cmd_xr(int, char **);
int cmd_mr(int, char **);
int cmd_dir(int, char **);
int cmd_cls(int, char **);

static const cmd_t commands[] = {
    // example : { "token",  "tests the tokenization", cmd_token },
    { "help",   "print a list of commands", cmd_help },
    { "cls",    "clear terminal", cmd_cls },
    { "xr",     "reads xram", cmd_xr },
    { "mr",     "reads ram", cmd_mr },
    { "dir",    "current drive directory", cmd_dir}
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
    tx_string("\x1b" "c" "Picocomputer 6502 Command Processor" NEWLINE "-----------------------------------" 
              NEWLINE NEWLINE "type \"help\" for commands list" NEWLINE NEWLINE);
    return;
}

void prompt() {
    tx_string("> ");
    return;
}

void help(){
    int i;
    tx_string("Available commands: " NEWLINE);
    for(i = 0; i < ARRAY_SIZE(commands); i++) {
        tx_string(commands[i].cmd);
        tx_string(" - ");
        tx_string(commands[i].help);
        tx_string(NEWLINE);
    }
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
                    tx_string("\r\x1b[K> ");
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

int cmd_help(int, char **) {
    help();
    return 0;
}

int cmd_cls(int, char **) {
    cls();
    return 0;
}

int cmd_xr(int argc, char **argv) {
    uint16_t addr;
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
    uint16_t addr;
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
    uint16_t addr = 0;
    uint16_t size = 16;

    if(argc >= 2) {
        addr = strtoul(argv[1], NULL, 16);
    }
    if(argc > 2) size = strtoul(argv[2], NULL, 0);

    return 0;
}
