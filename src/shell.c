/**
 * @file      shell.c
 * @author    Wojciech Gwioździk <wojciech@post.pl>
 * @copyright Wojciech Gwioździk. All rights reserved.
 *
 * based on Jason Howard's code <jth@howardlogic.com>
 * See LICENSE file in the project root folder for license information
 */

// Picocomputer 6502 documentation
// https://picocomputer.github.io/index.html

#include "shell.h"

int main(void) {
    int i = 0;
    int v = 0;
    char last_rx = 0;
    char ext_rx = 0;
    static cmdline_t cmdline = {0};
    
    tx_string(CSI_RESET);
    printf(CSI_CURSOR_HIDE); // hide cursor
    printf(APP_MSG_START);
    PAUSE(200);
    printf(CSI_CURSOR_SHOW); // show cursor
    tx_string(CSI_RESET);

    f_chdrive("0:");
    current_drive = '0';
    if(f_getcwd(dir_cwd, sizeof(dir_cwd)) >= 0 && dir_cwd[1] == ':') {
        current_drive = dir_cwd[0];
    }
    cls();
    /*
    // calling commands from code
    {
        char *args[1];
        args[0] = (char *)"";
        cmd_phi2(1, args);
    }
    show_time();
    */
    {
        char *args[1];
        args[0] = (char *)"";
        cmd_drives(1, args);
    }
    
    load_shelldir();

/*
    // ClearDisplayMemory();
    InitTerminalFont();
    
    // PAUSE(500);

    InitDisplay();

    // draw alphabet
    for(i=0;i<26;i++){
        DrawChar(55, i + 2, 0x41 + i, LIGHT_GRAY, BLACK);
    }
    for(i = 0; i < GFX_CHARACTER_COLUMNS; i++){
        DrawChar(4, i, '_', LIGHT_GRAY, BLACK);
    }

    printText( "Welcome to OS Shell for the Picocomputer 6502 (native mode)", 1, 3, WHITE, BLACK);

    for(i = 0; i < GFX_CHARACTER_ROWS; i++){
        if(i % 10){
            DrawChar(i,  0, '-', DARK_GRAY, BLACK);
            DrawChar(i, 79, '-', DARK_GRAY, BLACK);
        } else {
            DrawChar(i,  0, 0x1A, DARK_GRAY, BLACK);
            DrawChar(i, 79, 0x1B, DARK_GRAY, BLACK);
        }
    }
    for(i = 0; i < GFX_CHARACTER_COLUMNS; i++){
        if(i % 10){
            DrawChar( 0, i, 0xB3, DARK_GRAY, BLACK);
            DrawChar(59, i, 0xB3, DARK_GRAY, BLACK);
        } else {
            DrawChar( 0, i, 0x19, DARK_GRAY, BLACK);
            DrawChar(59, i, 0x18, DARK_GRAY, BLACK);
        }
    }
    DrawFontTable(50, 10, WHITE, BLACK, DARK_GREEN, DARK_RED);
    DrawLetters_PL(2, 57, WHITE, DARK_GRAY);
*/
    prompt(true);

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
                } else if(rx == 'O') {
                    ext_rx = 6;
                    continue;
                }
                ext_rx = 0;
            } else if(ext_rx == 2){
                if(rx == CHAR_UP){
                    ext_rx = 0;
                    tx_string("\r\x1b[K");
                    prompt(false);
                    cmdline.bytes = cmdline.lastbytes;
                    memcpy(cmdline.buffer, cmdline.lastbuffer, cmdline.lastbytes);
                    cmdline.buffer[cmdline.bytes] = 0;
                    tx_string(cmdline.buffer);
                    continue;
                } else if(rx == CHAR_DOWN){
                    ext_rx = 0;
                    {
                        char *args[1];
                        args[0] = (char *)"ls";
                        tx_string(NEWLINE);
                        cmd_ls(1, args);
                        cmdline.bytes = 0;
                        cmdline.buffer[0] = 0;
                        prompt(false);
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
                    cmdline.bytes = 0;
                    cmdline.buffer[0] = 0;
                    prompt(false);
                    ext_rx = 0;
                    continue;
                } else if(rx == 'O') { /* F1-F4 in xterm-style ESC O P|Q|R|S  */
                    ext_rx = 6;
                    continue;
                } else {
                    ext_rx = 0;
                }
            } else if(ext_rx == 6) {
                ext_rx = 0;
                // putchar(rx);
                if(rx == CHAR_F1 || rx == 'P') {
                    char path[FNAMELEN];
                    int com_argc = 2;
                    tx_string(NEWLINE);
                    strcpy(path, shelldir);
                    strcat(path, "help.com");
                    com_argv[0] = (char *)"com";
                    com_argv[1] = path;
                    /* Pass current input line as argument if present */
                    if(cmdline.bytes > 0) {
                        cmdline.buffer[cmdline.bytes] = 0; /* ensure NUL */
                        com_argv[2] = cmdline.buffer;
                        com_argc = 3;
                    }
                    cmd_com(com_argc, com_argv);
                    cmdline.bytes = 0;
                    cmdline.buffer[0] = 0;
                    prompt(false);
                    continue;
                }
                if(rx == CHAR_F2 || rx == 'Q') {
                    char path[FNAMELEN];
                    int com_argc = 2;
                    tx_string(NEWLINE);
                    strcpy(path, shelldir);
                    strcat(path, "keyboard.com");
                    com_argv[0] = (char *)"com";
                    com_argv[1] = path;
                    /* Pass current input line as argument if present */
                    if(cmdline.bytes > 0) {
                        cmdline.buffer[cmdline.bytes] = 0; /* ensure NUL */
                        com_argv[2] = cmdline.buffer;
                        com_argc = 3;
                    }
                    cmd_com(com_argc, com_argv);
                    cmdline.bytes = 0;
                    cmdline.buffer[0] = 0;
                    cls();
                    prompt(false);
                    continue;
                }
                if(rx == CHAR_F3 || rx == 'R') {
                    // for external command call
                    char path[FNAMELEN];
                    int com_argc = 2;
                    // for internal command call
                    char *args[1];
                    args[0] = (char *)"time";

                    tx_string(NEWLINE NEWLINE "The current time is" NEWLINE);
                    cmd_time(1, args);
                    cmdline.bytes = 0;
                    cmdline.buffer[0] = 0;

                    strcpy(path, shelldir);
                    strcat(path, "calendar.com");
                    com_argv[0] = (char *)"com";
                    com_argv[1] = path;
                    /* Pass current input line as argument if present */
                    if(cmdline.bytes > 0) {
                        cmdline.buffer[cmdline.bytes] = 0; /* ensure NUL */
                        com_argv[2] = cmdline.buffer;
                        com_argc = 3;
                    }
                    cmd_com(com_argc, com_argv);
                    cmdline.bytes = 0;
                    cmdline.buffer[0] = 0;
                    prompt(false);
                    continue;
                }
            // Normal character (ASCII printable or extended 8-bit, exclude DEL), just put it on the pile.
            } else if(((unsigned char)rx >= 32) && (rx != 127)) {
                ext_rx = 0;
                if(cmdline.bytes == CMD_BUF_MAX) {
                    tx_char(0x7); // if the buffer is full, send a bell
                } else {
                    cmdline.buffer[cmdline.bytes++] = rx;
                    cmdline.buffer[cmdline.bytes] = 0;
                    tx_char(rx);
                }
            // Backspace
            } else if(rx == CHAR_BS || rx == KEY_DEL) {
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
                prompt(false);
            } else {
                ext_rx = 0;
            }
            last_rx = rx; // Last line in RX_READY
        }
    }
}

static void load_shelldir(void) {
    FILE *f;
    char *nl;

    strncpy(shelldir, default_shelldir, sizeof(shelldir));
    shelldir[sizeof(shelldir) - 1] = '\0';

    f = fopen("USB0:/shell.txt", "r");
    if (!f) return;

    if (fgets(shelldir, sizeof(shelldir), f)) {
        nl = strpbrk(shelldir, "\r\n");
        if (nl) *nl = '\0';
        if (shelldir[0] == '\0') {
            strncpy(shelldir, default_shelldir, sizeof(shelldir));
            shelldir[sizeof(shelldir) - 1] = '\0';
        }
    } else {
        strncpy(shelldir, default_shelldir, sizeof(shelldir));
        shelldir[sizeof(shelldir) - 1] = '\0';
    }
    fclose(f);
}


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

// Read a single line from the console; returns length or -1 if cancelled with ESC.
static int read_line_editor(char *buf, int maxlen) {
    int len = 0;
    char c = 0;
    while(1) {
        RX_READY_SPIN;
        c = (char)RIA.rx;
        if(c == CHAR_CR || c == CHAR_LF) {
            tx_string(NEWLINE);
            buf[len] = 0;
            return len;
        }
        if(c == CHAR_BS || c == KEY_DEL) {
            if(len) {
                len--;
                tx_string("\b \b");
            } else {
                tx_char(CHAR_BELL);
            }
            continue;
        }
        if(c == CHAR_ESC) {
            buf[0] = 0;
            return -1;
        }
        if(((unsigned char)c >= 32) && (c != 127)) {
            if(len < maxlen - 1) {
                buf[len++] = c;
                tx_char(c);
            } else {
                tx_char(CHAR_BELL);
            }
        }
    }
}

// Print existing file content with CR/LF translation.
static void tx_print_existing(const char *buf, unsigned len) {
    unsigned i;
    if(len == 0) {
        tx_string("<empty>" NEWLINE);
        return;
    }
    for(i = 0; i < len; i++) {
        char c = buf[i];
        if(c == '\n') {
            tx_string(NEWLINE);
        } else {
            tx_char(c);
        }
    }
    tx_string(NEWLINE);
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
    return (uint16_t)((unsigned)&shell_end_marker);
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
    tx_string(CSI_RESET);
    tx_string(APP_MSG_TITLE);
    return;
}

void prompt(bool first_time) {
    if(f_getcwd(dir_cwd, sizeof(dir_cwd)) >= 0) {
        if(dir_cwd[1] == ':') current_drive = dir_cwd[0];
        tx_string(dir_cwd);
    } else {
        tx_char(current_drive);
        tx_string(":");
    }
    tx_string(first_time ? SHELLPROMPT_1ST : SHELLPROMPT);
    return;
}

static void refresh_current_drive(void) {
    if(f_getcwd(dir_cwd, sizeof(dir_cwd)) >= 0 && dir_cwd[1] == ':') {
        current_drive = dir_cwd[0];
    }
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
    /* Try implicit .com execution: SHELLDIR/<name>.com */
    {
        unsigned name_len = (unsigned)strlen(tokenList[0]);
        unsigned prefix_len = (unsigned)strlen(shelldir);
        int com_argc;
        int j;
        if(prefix_len + name_len + 5 < sizeof(com_fname)) {
            memcpy(com_fname, shelldir, prefix_len);
            memcpy(com_fname + prefix_len, tokenList[0], name_len);
            memcpy(com_fname + prefix_len + name_len, ".com", 5); /* includes NUL */
            com_argv[0] = (char *)"com";
            com_argv[1] = com_fname;
            com_argc = tokens + 1; /* add synthetic command name */
            if(com_argc > CMD_TOKEN_MAX + 1) com_argc = CMD_TOKEN_MAX + 1;
            for(j = 1; j < tokens && (j + 1) < (CMD_TOKEN_MAX + 1); j++) {
                com_argv[j + 1] = tokenList[j];
            }
            return cmd_com(com_argc, com_argv);
        }
    }
    tx_string("Unknown command" NEWLINE);
    return -1;
}

/*
    internal commands
*/

int cmd_cls(int, char **) {
    cls();
    return 0;
}

int cmd_exit(int status, char **) {
    tx_string(APP_MSG_EXIT);
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

    tx_string(NEWLINE "Drives list" NEWLINE NEWLINE "drive" TAB "label      " TAB "[MB]" TAB "free" NEWLINE
              "-----" TAB "-----------" TAB "------" TAB "----" NEWLINE);
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
                while(len < 11) { tx_char(' '); len++; }
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
        tx_string("Usage: list <filename>" NEWLINE);
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

int cmd_edit(int argc, char **argv) {
    int fd = -1;
    int rc = 0;
    int n;
    unsigned existing_len = 0;
    unsigned new_len = 0;
    char line_buf[128];
    char *existing = malloc(EDIT_BUF_MAX);
    char *new_content = malloc(EDIT_BUF_MAX);

    if(argc < 2) {
        tx_string("Usage: edit <file>" NEWLINE);
        free(existing); free(new_content);
        return 0;
    }
    if(!existing || !new_content) {
        tx_string("edit: OOM" NEWLINE);
        free(existing); free(new_content);
        return -1;
    }

    fd = open(argv[1], O_RDONLY);
    if(fd >= 0) {
        while((n = read(fd, existing + existing_len, EDIT_BUF_MAX - 1 - existing_len)) > 0) {
            existing_len += n;
            if(existing_len >= EDIT_BUF_MAX - 1) break;
        }
        if(n < 0) {
            tx_string("Cannot read file" NEWLINE);
            rc = -1;
            goto cleanup;
        }
        close(fd);
        fd = -1;
    }
    existing[existing_len] = 0;

    tx_string("---- existing content ----" NEWLINE);
    tx_print_existing(existing, existing_len);
    if(existing_len >= EDIT_BUF_MAX - 1) {
        tx_string("[display truncated]" NEWLINE);
    }
    tx_string("--------------------------" NEWLINE);
    tx_string("Type new content. '.' on its own line saves, '!' aborts, ESC cancels." NEWLINE);

    while(1) {
        int line_len;
        tx_string("> ");
        line_len = read_line_editor(line_buf, sizeof(line_buf));
        if(line_len < 0) {
            tx_string("Edit cancelled" NEWLINE);
            rc = -1;
            goto cleanup;
        }
        if(line_len == 1 && line_buf[0] == '!') {
            tx_string("Edit aborted, file left unchanged" NEWLINE);
            goto cleanup;
        }
        if(line_len == 1 && line_buf[0] == '.') {
            break;
        }
        if(new_len + (unsigned)line_len + 1 >= EDIT_BUF_MAX) {
            tx_string("Buffer full, stopping input" NEWLINE);
            break;
        }
        memcpy(new_content + new_len, line_buf, line_len);
        new_content[new_len + line_len] = '\n';
        new_len += line_len + 1;
    }

    fd = open(argv[1], O_WRONLY | O_CREAT | O_TRUNC);
    if(fd < 0) {
        tx_string("Cannot open file for writing" NEWLINE);
        rc = -1;
        goto cleanup;
    }
    if(write(fd, new_content, new_len) != (int)new_len) {
        tx_string("Write failed" NEWLINE);
        rc = -1;
        goto cleanup;
    }
    tx_string("Saved" NEWLINE);

cleanup:
    if(fd >= 0) close(fd);
    free(existing);
    free(new_content);
    return rc;
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

int cmd_rename(int argc, char **argv) {
    if(argc < 3) {
        tx_string("Usage: rename <old> <new>" NEWLINE);
        return 0;
    }
    if(rename(argv[1], argv[2]) < 0) {
        tx_string("rename failed" NEWLINE);
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
    // clearterminal();
    fn = (void (*)(void))start;
    fn();
    return 0;
}

int cmd_com(int argc, char **argv) {
    int fd;
    int n;
    uint16_t addr = COM_LOAD_ADDR;
    void (*fn)(void);
    int user_argc;
    char **user_argv;

    if(argc < 2) {
        tx_string("Usage: com <file.com> [args...]" NEWLINE);
        return 0;
    }
    fd = open(argv[1], O_RDONLY);
    if(fd < 0) {
        tx_string("This is not an internal or external shell command." NEWLINE);
        return -1;
    }

    while((n = read(fd, bload_buf, sizeof(bload_buf))) > 0) {
        ram_writer(bload_buf, addr, n);
        addr += (uint16_t)n;
    }
    close(fd);
    if(n < 0) {
        tx_string("Read error" NEWLINE);
        return -1;
    }

    /* Save and overwrite argc/argv block */
    memcpy(run_args_backup, (void *)RUN_ARGS_BASE, RUN_ARGS_BLOCK_SIZE);
    user_argc = argc - 2;
    if(user_argc < 0) user_argc = 0;
    user_argv = argv + 2;
    build_run_args(user_argc, user_argv);

    // clearterminal();
    fn = (void (*)(void))COM_LOAD_ADDR;
    fn();
    memcpy((void *)RUN_ARGS_BASE, run_args_backup, RUN_ARGS_BLOCK_SIZE);
    refresh_current_drive();
    return 0;
}

int cmd_run(int argc, char **argv) {
    void (*fn)(void);
    uint16_t addr;
    int user_argc;
    char **user_argv;

    if(argc < 2) {
        tx_string("Usage: run <addr> [arg1, ...]" NEWLINE);
        return 0;
    }

    addr = (uint16_t)strtoul(argv[1], NULL, 16);
    memcpy(run_args_backup, (void *)RUN_ARGS_BASE, RUN_ARGS_BLOCK_SIZE);
    user_argc = argc - 2;
    if(user_argc < 0) user_argc = 0;
    user_argv = argv + 2;

    build_run_args(user_argc, user_argv);
    //clearterminal();
    fn = (void (*)(void))addr;
    fn();
    memcpy((void *)RUN_ARGS_BASE, run_args_backup, RUN_ARGS_BLOCK_SIZE);
    refresh_current_drive();
    return 0;
}

static void build_run_args(int user_argc, char **user_argv) {
    uint8_t *base = (uint8_t *)RUN_ARGS_BASE;
    uint16_t *ptrs = (uint16_t *)(RUN_ARGS_BASE + 1);
    uint8_t *strp = (uint8_t *)(RUN_ARGS_BASE + 1 + RUN_ARGS_MAX * 2);
    uint8_t *end = strp + RUN_ARGS_BUF;
    int i;

    if(user_argc > RUN_ARGS_MAX) user_argc = RUN_ARGS_MAX;
    if(user_argc < 0) user_argc = 0;
    base[0] = (uint8_t)user_argc;

    for(i = 0; i < user_argc; i++) {
        const char *s = user_argv[i];
        size_t len = strlen(s);
        if(strp + len + 1 > end) {
            base[0] = (uint8_t)i; /* truncate argc to stored args */
            break;
        }
        ptrs[i] = (uint16_t)(uintptr_t)strp;
        memcpy(strp, s, len + 1);
        strp += len + 1;
    }
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

int cmd_time(int argc, char **argv) {
    (void)argc; (void)argv;
    show_time();
    tx_string(NEWLINE NEWLINE);
    return 0;
}

int cmd_phi2(int argc, char **argv) {
    int hz = phi2();
    (void)argc; (void)argv;
    tx_string("65C02 clock speed: ");
    tx_dec32(hz);
    tx_string(" Hz" NEWLINE NEWLINE);
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
    tx_string("Name       : ");
    tx_string(dir_ent.fname);
    tx_string(NEWLINE);
    tx_string("Short      : ");
    tx_string(dir_ent.altname);
    tx_string(NEWLINE);
    tx_string("Size       : ");
    tx_dec32(dir_ent.fsize);
    tx_string(" bytes" NEWLINE);
    tx_string("Attributes : ");
    tx_char((dir_ent.fattrib & AM_RDO) ? 'R' : '-');
    tx_char((dir_ent.fattrib & AM_HID) ? 'H' : '-');
    tx_char((dir_ent.fattrib & AM_SYS) ? 'S' : '-');
    tx_char((dir_ent.fattrib & AM_VOL) ? 'V' : '-');
    tx_char((dir_ent.fattrib & AM_DIR) ? 'D' : '-');
    tx_char((dir_ent.fattrib & AM_ARC) ? 'A' : '-');
    tx_string(NEWLINE);
    tx_string("Timestamp  : ");
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
int cmd_dir(int argc, char **argv) {
    const char *mask = "*";
    const char *path = ".";
    char *p;
    int dirdes = -1;
    int rc = 0;
    unsigned sort_mode = 0; // 0=name,1=time desc (youngest),2=time asc,3=size asc,4=size desc
    unsigned files_count = 0;
    unsigned dirs_count = 0;
    unsigned long total_bytes = 0;
    int i;
    char dir_drive[3] = {0};
    char *dir_arg = malloc(FNAMELEN);
    char *dir_path_buf = malloc(FNAMELEN);
    char *dir_mask_buf = malloc(FNAMELEN);
    dir_list_entry_t *dir_entries = malloc(sizeof(dir_list_entry_t) * DIR_LIST_MAX);
    dir_list_entry_t dir_tmp;
    unsigned dir_entries_count = 0;
    unsigned dir_i = 0, dir_j = 0;

    if(!dir_arg || !dir_path_buf || !dir_mask_buf || !dir_entries) {
        free(dir_arg); free(dir_path_buf); free(dir_mask_buf); free(dir_entries);
        return -1;
    }

    dir_drive[0] = dir_drive[1] = dir_drive[2] = 0;
    if(argc >= 2) {
        strcpy(dir_arg, argv[1]);
        // Handle drive prefix like "0:" or "A:"
        if(dir_arg[1] == ':') {
            if(dir_arg[0] < '0' || dir_arg[0] > '7') {
                tx_string("Invalid drive" NEWLINE);
                rc = -1;
                goto cleanup;
            }
            dir_drive[0] = dir_arg[0];
            dir_drive[1] = ':';
            dir_drive[2] = 0;
            if(f_chdrive(dir_drive) < 0) {
                tx_string("Invalid drive" NEWLINE);
                rc = -1;
                goto cleanup;
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
            mask = "*";
            path = ".";
        }
        if(!*mask) mask = "*";
    }
    // Parse optional flags /da /dd /sa /sd
    for(i = 2; i < argc; i++) {
        if(argv[i][0] == '/') {
            if(!strcmp(argv[i], "/da")) sort_mode = 1;
            else if(!strcmp(argv[i], "/dd")) sort_mode = 2;
            else if(!strcmp(argv[i], "/sa")) sort_mode = 3;
            else if(!strcmp(argv[i], "/sd")) sort_mode = 4;
        }
    }

    // Copy path/mask into static buffers for reuse.
    strcpy(dir_path_buf, path);
    strcpy(dir_mask_buf, mask);

    if(f_getcwd(dir_cwd, sizeof(dir_cwd)) < 0) {
        tx_string("getcwd failed" NEWLINE);
        rc = -1;
        goto cleanup;
    }

    tx_string(NEWLINE "Directory: ");
    tx_string(dir_cwd);
    tx_string(NEWLINE NEWLINE);

    dirdes = f_opendir(dir_path_buf[0] ? dir_path_buf : ".");
    if(dirdes < 0) {
        tx_string("opendir failed" NEWLINE);
        rc = -1;
        goto cleanup;
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
    dirdes = -1;

    if(rc < 0) return -1;

    // Sort: files first, directories after, each group alphabetically.
    for(dir_i = 0; dir_i < dir_entries_count; dir_i++) {
        for(dir_j = dir_i + 1; dir_j < dir_entries_count; dir_j++) {
            unsigned a_dir = dir_entries[dir_i].fattrib & AM_DIR;
            unsigned b_dir = dir_entries[dir_j].fattrib & AM_DIR;
            int swap = 0;
            unsigned long a_val = 0;
            unsigned long b_val = 0;
            switch(sort_mode) {
                case 1: // time desc (youngest first)
                case 2: // time asc
                    a_val = ((unsigned long)dir_entries[dir_i].fdate << 16) | dir_entries[dir_i].ftime;
                    b_val = ((unsigned long)dir_entries[dir_j].fdate << 16) | dir_entries[dir_j].ftime;
                    if(sort_mode == 1) swap = a_val < b_val;
                    else swap = a_val > b_val;
                    break;
                case 3: // size asc
                    a_val = dir_entries[dir_i].fsize;
                    b_val = dir_entries[dir_j].fsize;
                    swap = a_val > b_val;
                    break;
                case 4: // size desc
                    a_val = dir_entries[dir_i].fsize;
                    b_val = dir_entries[dir_j].fsize;
                    swap = a_val < b_val;
                    break;
                default: // name, files before dirs
                    if(a_dir != b_dir) {
                        if(a_dir && !b_dir) swap = 1;
                    } else if(strcmp(dir_entries[dir_i].name, dir_entries[dir_j].name) > 0) {
                        swap = 1;
                    }
                    break;
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
            dirs_count++;
        } else {
            tx_dec32(dir_entries[dir_i].fsize);
            total_bytes += dir_entries[dir_i].fsize;
            files_count++;
        }
        tx_char('\t');
        tx_char((dir_entries[dir_i].fattrib & AM_RDO) ? 'R' : '-');
        tx_char((dir_entries[dir_i].fattrib & AM_HID) ? 'H' : '-');
        tx_char((dir_entries[dir_i].fattrib & AM_SYS) ? 'S' : '-');
        tx_char((dir_entries[dir_i].fattrib & AM_VOL) ? 'V' : '-');
        tx_char((dir_entries[dir_i].fattrib & AM_DIR) ? 'D' : '-');
        tx_char((dir_entries[dir_i].fattrib & AM_ARC) ? 'A' : '-');
        tx_string(NEWLINE);
    }
    tx_string(NEWLINE);
    tx_string("Files: ");
    tx_dec32(files_count);
    tx_string("  Dirs: ");
    tx_dec32(dirs_count);
    tx_string("  Bytes: ");
    tx_dec32(total_bytes);
    tx_string(NEWLINE NEWLINE);
cleanup:
    if(dirdes >= 0) f_closedir(dirdes);
    free(dir_arg);
    free(dir_path_buf);
    free(dir_mask_buf);
    free(dir_entries);
    return rc;
}
*/
int cmd_ls(int argc, char **argv){
    int dirdes;
    int rc = 0;
    (void)argc;
    (void)argv;

    dirdes = f_opendir(".");
    if(dirdes < 0) {
        tx_string("opendir failed" NEWLINE);
        return -1;
    }

    tx_string(NEWLINE 
              "size      file/directory name" NEWLINE
              "--------- -------------------" NEWLINE
              );

    while(1) {
        rc = f_readdir(&dir_ent, dirdes);
        if(rc < 0) {
            tx_string("readdir failed" NEWLINE);
            break;
        }
        if(!dir_ent.fname[0]) break; /* end of directory */

        /* Column 1: size or <DIR>, left aligned to width 6 */
        if(dir_ent.fattrib & AM_DIR) {
            const char *label = "<DIR>     ";
            tx_string(label);
        } else {
            char buf[12];
            unsigned long val = dir_ent.fsize;
            int pos = 0;
            if(val == 0) {
                buf[pos++] = '0';
            } else {
                char tmp[12];
                int tpos = 0;
                while(val && tpos < (int)sizeof(tmp)) {
                    tmp[tpos++] = '0' + (val % 10);
                    val /= 10;
                }
                while(tpos) buf[pos++] = tmp[--tpos];
            }
            buf[pos] = 0;
            tx_string(buf);
            while(pos++ < 10) tx_char(' ');
        }

        /* Column 2: entry name */
        tx_string(dir_ent.fname);
        tx_string(NEWLINE);
    }

    if(f_closedir(dirdes) < 0 && rc >= 0) {
        tx_string("closedir failed" NEWLINE);
        rc = -1;
    }
    tx_string(NEWLINE);
    return rc;
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

void InitTerminalFont(void){
    
    uint16_t i = 0;
    uint16_t j = 0;
    int fd = 0;
    int result = 0;

    RIA.addr0 = GFX_FONT_CUSTOM;
    RIA.step0 = 1;
    // clean up font bank
    for(i = 0; i < 0x2000; i++){
        RIA.rw0 = 0;
    }
    // read data and place them in xram from bottom to top [8x8inorder16x16]
    sprintf(*filename,"%s%s", shelldir, "[font8x8]font00.bmp\0");
    fd = open(*filename, O_RDONLY);
    if(fd){
        // lseek(fd,0x003E,SEEK_SET); // problems ...
        read_xram(GFX_FONT_CUSTOM, 0x3E, fd); // omit BMP bpp1 header
        for(i = 0; i < 16; i++){
            for(j = 16; j > 0; j--){
                read_xram(GFX_FONT_CUSTOM + 0x700 + ((j - 1) * 0x10), 0x10, fd);
                read_xram(GFX_FONT_CUSTOM + 0x600 + ((j - 1) * 0x10), 0x10, fd);
                read_xram(GFX_FONT_CUSTOM + 0x500 + ((j - 1) * 0x10), 0x10, fd);
                read_xram(GFX_FONT_CUSTOM + 0x400 + ((j - 1) * 0x10), 0x10, fd);
                read_xram(GFX_FONT_CUSTOM + 0x300 + ((j - 1) * 0x10), 0x10, fd);
                read_xram(GFX_FONT_CUSTOM + 0x200 + ((j - 1) * 0x10), 0x10, fd);
                read_xram(GFX_FONT_CUSTOM + 0x100 + ((j - 1) * 0x10), 0x10, fd);
                read_xram(GFX_FONT_CUSTOM + 0x000 + ((j - 1) * 0x10), 0x10, fd);
            }
        }
        close(fd);
        // printf("\r\nSUCCESS: reading [font8x8]font00.bmp file %i\r\n", fd);
    } else {
        printf("\r\nERROR: reading [font8x8]font00.bmp file %i\r\n", fd);
    }

}

// ----------------------------------------------------------------------------
// Changes TxDisplay bg_clr and fg_clr, then overwrites display using them.
// Doesn't clear top or bottom rows, as these are for menu and status bar.
// ----------------------------------------------------------------------------
void ClearDisplayMemory(void){
    uint8_t i;
    uint16_t displaymemsize = 2 * GFX_CHARACTER_COLUMNS * GFX_CHARACTER_ROWS;

    RIA.addr0 = canvas_data;
    RIA.step0 = 1;

    for (i = 0; i < displaymemsize; i++) {
        RIA.rw0 = 0;
    }
}

void ClearDisplay(uint8_t fg, uint8_t bg){
    uint8_t r, c;

    fg_clr = fg;
    bg_clr = bg;

    RIA.addr0 = canvas_data;
    RIA.step0 = 1;

    for (r = 0; r < canvas_r; r++) {
        for (c = 0; c < canvas_c; c++) {
            DrawChar(r, c, ' ', fg, bg);
        }
    }
}

void InitDisplay(void){
    uint8_t x_offset = 0;
    uint8_t y_offset = 0;
#ifdef GFX_FONTSIZE16
    uint8_t font_bpp_opt = GFX_CHARACTER_FONTSIZE8x16 | GFX_CHARACTER_bpp4; // 10 8x16 font at 4bpp
#endif
#ifdef GFX_FONTSIZE8
    uint8_t font_bpp_opt = GFX_CHARACTER_FONTSIZE8x8 | GFX_CHARACTER_bpp4; // 10 8x8 font at 4bpp
#endif
    
    // initialize the canvas
    xreg(1, 0, 0, canvas_type);

    xram0_struct_set(canvas_struct, vga_mode1_config_t, x_wrap, false);
    xram0_struct_set(canvas_struct, vga_mode1_config_t, y_wrap, false);
    xram0_struct_set(canvas_struct, vga_mode1_config_t, x_pos_px, x_offset);
    xram0_struct_set(canvas_struct, vga_mode1_config_t, y_pos_px, y_offset);
    xram0_struct_set(canvas_struct, vga_mode1_config_t, width_chars, canvas_c);
    xram0_struct_set(canvas_struct, vga_mode1_config_t, height_chars, canvas_r);
    xram0_struct_set(canvas_struct, vga_mode1_config_t, xram_data_ptr, canvas_data);
    xram0_struct_set(canvas_struct, vga_mode1_config_t, xram_palette_ptr, GFX_CHARACTER_PAL_PTR);
    xram0_struct_set(canvas_struct, vga_mode1_config_t, xram_font_ptr, GFX_CHARACTER_FONT_PTR);
    xreg(1, 0, 1, GFX_MODE_CHARACTER, font_bpp_opt, canvas_struct, GFX_PLANE_1);

    ClearDisplay(fg_clr, bg_clr);

}

void DrawChar(uint8_t row, uint8_t col, char ch, uint8_t fg, uint8_t bg)
{
    // for 4-bit color, index 2 bytes per ch
    RIA.addr0 = canvas_data + 2 * (row * canvas_c + col);
    RIA.step0 = 1;
    RIA.rw0 = ch;
    RIA.rw0 = (bg << 4) | fg;
}

void GetChar(uint8_t row, uint8_t col, char *pch, uint8_t *pfg, uint8_t *pbg)
{
    uint8_t bgfg;

    // for 4-bit color, index 2 bytes per ch
    RIA.addr0 = canvas_data + 2 * (row * canvas_c + col);
    RIA.step0 = 1;
    *pch = RIA.rw0;
    bgfg = RIA.rw0;
    *pbg = bgfg >> 4;
    *pfg = bgfg & 0x0F;
}

bool BackupChars(uint8_t row, uint8_t col, uint8_t width, uint8_t height, uint8_t *pstash)
{
    if (pstash != NULL) {
        uint8_t * pbyte = pstash;
        uint16_t r, c;
        for (r = row; r < row + height; r++) {
            for (c = col; c < col + width; c++) {
                // for 4-bit color, index 2 bytes per ch
                RIA.addr0 = canvas_data + 2*(r*canvas_c + c);
                RIA.step0 = 1;
                *(pbyte++) = RIA.rw0; // ch
                *(pbyte++) = RIA.rw0; // bgfg
            }
        }
        return true; // backup succeeded
    }
    return false;
}

bool RestoreChars(uint8_t row, uint8_t col, uint8_t width, uint8_t height, uint8_t *pstash)
{
    if (pstash != NULL) {
        uint8_t * pbyte = pstash;
        uint16_t r, c;
        for (r = row; r < row + height; r++) {
            for (c = col; c < col + width; c++) {
                // for 4-bit color, index 2 bytes per ch
                RIA.addr0 = canvas_data + 2*(r*canvas_c + c);
                RIA.step0 = 1;
                RIA.rw0 = *(pbyte++); // ch
                RIA.rw0 = *(pbyte++); // bgfg
            }
        }
        return true; // restore suceeded
    }
    return false;
}

void printText(char *text, uint8_t x, uint8_t y, uint8_t fg, uint8_t bg)
{
    uint8_t i;

    strncpy(msg, text, sizeof(msg) - 1);
    msg[sizeof(msg) - 1] = 0;

    for (i = 0; msg[i] && (x + i) < GFX_CHARACTER_COLUMNS; i++) {
        DrawChar(y, x + i, msg[i], fg, bg);
    }
}

void DrawLetters_PL(uint8_t x, uint8_t y, uint8_t fg, uint8_t bg){

    // polish letters PC852 ĄąĆćĘęŁłŃńÓóŚśŻżŹź
    // int letters_polish[18] = { 164,  165,  143,  134,  168,  169,  157,  136,  227,  228,  224,  162,  151,  152,  189,  190,  141,  171};
    uint8_t letters_polish[18] = {0xA4, 0xA5, 0x8F, 0x86, 0xA8, 0xA9, 0x9D, 0x88, 0xE3, 0xE4, 0xE0, 0xA2, 0x97, 0x98, 0xBD, 0xBE, 0x8D, 0xAB};

    uint8_t i = 0;

    for(i = 0; i < sizeof(letters_polish); i++){
        DrawChar(y,  x + i, (char)letters_polish[i], fg, bg);
    }

}

void DrawFontTable(uint8_t x, uint8_t y, uint8_t fg, uint8_t bg, uint8_t bgc, uint8_t bgr){

    uint8_t i = 0;
    uint8_t j = 0;

    for(j = 0; j < 16; j++){
        DrawChar(y + 1 + j, x     , (char)(j < 10 ? 48 + j : 65 + j - 10), fg, bgr);
        DrawChar(y + 1 + j, x + 17, (char)(j < 10 ? 48 + j : 65 + j - 10), fg, bgr);
        for(i = 0; i < 16; i++){
            DrawChar(y        , x + i + 1, (char)(i < 10 ? 48 + i : 65 + i - 10), fg, bgc);
            DrawChar(y + 17   , x + i + 1, (char)(i < 10 ? 48 + i : 65 + i - 10), fg, bgc);
            DrawChar(y + 1 + j, x + i + 1, (char)(j * 16 + i), fg, bg);
        }
    }

}
