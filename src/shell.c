/**
 * @file      shell.c
 * @author    Wojciech Gwioździk
 * @copyright 2026 (c) Wojciech Gwioździk
 *
 * based on Jason Howard's ideas & code <jth@howardlogic.com>
 * https://github.com/jthwho/rp6502-shell
 */

// Picocomputer 6502 documentation
// https://picocomputer.github.io/index.html

// TO DO
// -----------------------------------------------------------------------------
// plenty of things ...

#include "shell.h"

#define APPVER "20260411.1404"
#define APPNAME "razemOS"
#define APP_MSG_START ANSI_DARK_GRAY CSI "12;35H" APPNAME
#define APP_HOURGLASS CSI "14;36H" ANSI_DARK_GRAY ".........." CSI "10D" ANSI_RESET
#define APP_MSG_TITLE CSI "2;1H" CSI HIGHLIGHT_COLOR " " APPNAME " > " ANSI_RESET " for Picocomputer 6502" ANSI_DARK_GRAY CSI "2;60Hversion " APPVER ANSI_RESET
#define APP_STARTPROMPTPOS CSI "3;1H"
#define APP_MSG_EXIT CSI_RESET

void *__fastcall__ argv_mem(size_t size) { return malloc(size); }

// argv[0] saved here before any sub-program call can corrupt the heap argv buffer
static char shell_prog[FNAMELEN];

int main(int argc, char *argv[]){

    int stage;
#ifdef DEBUG
    int i;
    printf("argc = %d\n", argc);
    for (i = 0; i < argc; i++)
        printf("argv[%d] = %s\n", i, argv[i]);
#endif

    strncpy(shell_prog, argv[0], sizeof(shell_prog) - 1);
    shell_prog[sizeof(shell_prog) - 1] = '\0';

    stage = (argc == 1) ? 0
          : (argc == 2 && !strcmp(argv[1], "BOOT"))  ? 1
          : (argc == 2 && !strcmp(argv[1], "START")) ? 2
          : -1;

    // init stage -> self call with argument BOOT
    if (stage == 0) {
        char arg[] = "BOOT";
        ria_attr_set(1, RIA_ATTR_LAUNCHER);
        ria_execl(shell_prog, arg, NULL);
    } else if (stage == 1) {
        char arg[] = "START";
        startstage_boot();
        ria_execl(shell_prog, arg, NULL);
    } else if (stage == 2) {
        startstage_shell();
    } else {
        printf(EXCLAMATION "boot error" NEWLINE NEWLINE);
    }

    return -1;

}

static int startstage_boot(){

    int i = 0;
    struct tm *tmnow = get_time();

    printf(CSI_RESET CSI_CURSOR_HIDE);

    // boot screen
    xreg(1, 0, 0, GFX_CANVAS_640x480);
    xram0_struct_set(GFX_STRUCT, vga_mode3_config_t, x_wrap, false);
    xram0_struct_set(GFX_STRUCT, vga_mode3_config_t, y_wrap, false);
    xram0_struct_set(GFX_STRUCT, vga_mode3_config_t, x_pos_px, 0);
    xram0_struct_set(GFX_STRUCT, vga_mode3_config_t, y_pos_px, 0);
    xram0_struct_set(GFX_STRUCT, vga_mode3_config_t, width_px, 640);
    xram0_struct_set(GFX_STRUCT, vga_mode3_config_t, height_px, 480);
    xram0_struct_set(GFX_STRUCT, vga_mode3_config_t, xram_data_ptr, 0x2000);
    xram0_struct_set(GFX_STRUCT, vga_mode3_config_t, xram_palette_ptr, 0xFFFF);

    xreg(1, 0, 1, GFX_MODE_BITMAP, GFX_BITMAP_bpp1, GFX_STRUCT, GFX_PLANE_0, 1, 440);
    PAUSE(150);
    xreg(1, 0, 1, GFX_MODE_CONSOLE, GFX_PLANE_2);

    // printf(APP_MSG_START);

    if(!(appflags & APPFLAG_RTC)){
        printf(APP_HOURGLASS);
        i = 0;
        while (1){
            ++i;
            printf(".");
            PAUSE(50);
            if(i % 10){
                printf(CSI "1m");
                tmnow = get_time();
                if(appflags & APPFLAG_RTC) break;
            } else {
                printf(CSI "10D" ANSI_DARK_GRAY ".........." CSI "10D" ANSI_WHITE);
            }
            if (i > 60){
                if(!(appflags & APPFLAG_RTC)) printf(NEWLINE "RTC is not set.");
                // set_time();
                break;
            }
        }
    } else {
        printf(CSI_CLS ANSI_RESET CSI_CURSOR_SHOW);
    }
    return 0;
}

static int startstage_shell(){

    int i = 0;
    int v = 0;
    char last_rx = 0;
    char ext_rx = 0;

    // struct tm *tmnow = get_time();

    f_chdrive("0:");
    current_drive = '0';
    if(f_getcwd(dir_cwd, sizeof(dir_cwd)) >= 0 && dir_cwd[1] == ':') {
        current_drive = dir_cwd[0];
    }
    strncpy(shelldir, default_shelldir, sizeof(shelldir));
    shelldir[sizeof(shelldir) - 1] = '\0';

    prompt(PROMPT_CLS);
    // printf(APP_MSG_TITLE APP_STARTPROMPTPOS);
    // prompt(PROMPT_FIRST);

    while (1)
    {

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
                if(rx == CHAR_UP && (cmdline.lastbytes > 0)){
                    ext_rx = 0;
                    tx_string(CSI "K" CSI "1A");
                    prompt(PROMPT);
                    cmdline.bytes = cmdline.lastbytes;
                    memcpy(cmdline.buffer, cmdline.lastbuffer, cmdline.lastbytes);
                    cmdline.buffer[cmdline.bytes] = 0;
                    tx_string(cmdline.buffer);
                    continue;
                } else if(rx == CHAR_DOWN){
                    continue;
                } else if(rx == CHAR_LEFT){
                    continue;
                } else if(rx == CHAR_RIGHT) {
                    continue;
                } else if(rx == 'O') { // handle terminal access
                    ext_rx = 6;
                    continue;
                } else {
                    ext_rx = 0;
                }
            } else if(ext_rx == 6) {
                ext_rx = 0;
                if(rx == CHAR_F1 || rx == 'P') {
                    int com_argc = 2;
                    com_argv[0] = (char *)"com";
                    com_argv[1] = (char *)"help.com";
                    /* Pass current input line as argument if present */
                    if(cmdline.bytes > 0) {
                        cmdline.buffer[cmdline.bytes] = 0; /* ensure NUL */
                        com_argv[2] = cmdline.buffer;
                        com_argc = 3;
                    }
                    cmd_com(com_argc, com_argv);
                    cmdline.bytes = 0;
                    cmdline.buffer[0] = 0;
                    prompt(PROMPT);
                    continue;
                }
                if(rx == CHAR_F2 || rx == 'Q') {
                    ext_rx = 0;
                    execute_cmd(&cmdline, "keyboard");
                    cmdline.bytes = 0;
                    cmdline.buffer[0] = 0;
                    prompt(PROMPT_CLS);
                    continue;
                }
                if(rx == CHAR_F3 || rx == 'R') {
                    ext_rx = 0;
                    tx_string(NEWLINE);
                    execute_cmd(&cmdline, "date /a");
                    cmdline.bytes = 0;
                    cmdline.buffer[0] = 0;
                    prompt(PROMPT);
                    continue;
                }
                if(rx == CHAR_F4 || rx == 'S') {
                    ext_rx = 0;
                    {
                        // internal command call
                        char *args[1];
                        args[0] = (char *)"ls";
                        tx_string(NEWLINE);
                        cmd_ls(1, args);
                        cmdline.bytes = 0;
                        cmdline.buffer[0] = 0;
                        prompt(PROMPT);
                    }
                    continue;
                }
            // SOT (0x01) — auto-launch file receiver (crx.com /auto)
            } else if(rx == '\x01') {
                ext_rx = 0;
                execute_cmd(&cmdline, "crx /auto");
                cmdline.bytes = 0;
                cmdline.buffer[0] = 0;
                prompt(PROMPT_CLS);
                continue;
            // Normal character (ASCII printable or extended 8-bit, exclude DEL), just put it on the pile.
            } else if(((unsigned char)rx >= 32) && (rx != 127)) {
                ext_rx = 0;
                if(cmdline.bytes == CMD_BUF_MAX) {
                    tx_char(CHAR_BELL); // if the buffer is full, sound a bell
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
                if(cmdline.bytes){
                    tx_string(NEWLINE);
                    cmdline.lastbytes = cmdline.bytes;
                    execute(&cmdline);
                    cmdline.bytes = 0;
                    cmdline.buffer[0] = 0;
                    ext_rx = 0;
                    prompt(PROMPT);
                }
            } else {
                ext_rx = 0;
            }
            last_rx = rx; // Last line in RX_READY
        }
    }
}

void cls(){ // clear screen
    printf(CSI_CLS APP_MSG_TITLE APP_STARTPROMPTPOS CSI_CURSOR_SHOW);
    return;
}

void prompt(uint8_t mode) {
    if(mode == PROMPT_CLS) cls();    
    printf("%s%s%s", (mode == PROMPT_FIRST ? "" : NEWLINE), dir_cwd, (mode == PROMPT_FIRST ? SHELLPROMPT_1ST : SHELLPROMPT));
    return;
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
#ifdef CODE_HEXDUMP
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
#endif

// things related to : disk operations

bool match_mask(const char *name, const char *mask) { // Simple wildcard match supporting '*' (0+ chars) and '?' (1 char).
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

const char *format_fat_datetime(unsigned fdate, unsigned ftime) { // Format FAT date/time into YYYY-MM-DD hh:mm:ss
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

// things related to : memory operations

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

#ifdef CODE_PEEK
static void file_reader(uint8_t *buf, uint16_t addr, uint16_t size) {
    off_t pos = (off_t)filehex_base + (off_t)addr;
    if(filehex_fd < 0) return;
    if(lseek(filehex_fd, pos, SEEK_SET) < 0) return;
    read(filehex_fd, buf, size);
}
#endif

// things related to memory

uint16_t mem_lo(void) { // Lowest usable address for other programs as shell base adress + shell size
    return (uint16_t)(_BSS_RUN__ + (uint16_t)_BSS_SIZE__);
}

uint16_t mem_top(void) {
    return (uint16_t)(MEMTOP);
}

uint16_t mem_free(void) {
    return (uint16_t)(MEMTOP - mem_lo() + 1);
}

// things related to : RTC

struct tm *get_time(void) { // Return pointer to current RTC time; tm_year=1970 signals "RTC not set".
    static struct tm tmnow = {0};
    time_t tnow;

    tmnow.tm_year = 70; /* 1970-01-01 is treated as "RTC not set" */
    tmnow.tm_mday = 1;

    if(time(&tnow) != (time_t)-1) {
        {
            struct tm *res = localtime(&tnow);
            if(res) {
                tmnow = *res;
            }
        }
    }
    if(tmnow.tm_year > 70){
        appflags |= APPFLAG_RTC;
    } else {
        appflags &= (unsigned char)~APPFLAG_RTC;
    }
    return &tmnow;
}

void show_time(void) { // print current date & time to console

    struct tm *tmnow = get_time();
    char buf[32];

    if(!(appflags | APPFLAG_RTC)){
        tx_string(NEWLINE ANSI_RED "[Time: ERROR] Real Time Clock is not set." ANSI_RESET NEWLINE NEWLINE);
        return;
    }
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S %Z", tmnow);
    tx_string(NEWLINE "Current date & time is ");
    tx_string(buf);
    tx_string(NEWLINE);
    return;
}

// things related to : internal commands

static int tokenize(char *buf, int maxBuf, char **tokenList, int maxTokens) {
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

static int execute(cmdline_t *cl) {
    int i;
    char *tokenList[CMD_TOKEN_MAX];
    int tokens = 0;
    cl->lastbytes = 0;
    memcpy(cl->lastbuffer, cl->buffer, cl->bytes);
    cl->lastbytes = cl->bytes;
    cl->lastbuffer[cl->bytes] = 0;
    tokens = tokenize(cl->buffer, cl->bytes, tokenList, ARRAY_SIZE(tokenList));
    if(tokens <= 0) {
        if(tokens < 0) tx_string(EXCLAMATION "unterminated quote/escape" NEWLINE);
        return 0;
    }
    // Allow selecting drive by typing e.g. "0:" directly
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
    // Try implicit .exe execution: load address from file trailer
    {
        unsigned name_len = (unsigned)strlen(tokenList[0]);
        if(name_len > 4 && !strcmp(tokenList[0] + name_len - 4, ".exe")) {
            int exe_argc = tokens + 1;
            int j;
            if(exe_argc > CMD_TOKEN_MAX + 1) exe_argc = CMD_TOKEN_MAX + 1;
            exe_argv[0] = (char *)"exe";
            exe_argv[1] = tokenList[0];
            for(j = 1; j < tokens && (j + 1) < (CMD_TOKEN_MAX + 1); j++) {
                exe_argv[j + 1] = tokenList[j];
            }
            return cmd_exe(exe_argc, exe_argv);
        }
    }
    // Try implicit .com execution: MSC0:/SHELL/<name>.com, then shelldir (ROM:)
    {
        static const char msc_prefix[] = "MSC0:/SHELL/";
        unsigned name_len = (unsigned)strlen(tokenList[0]);
        unsigned prefix_len;
        int probe_fd;
        int com_argc;
        int j;

        com_argc = tokens + 1;
        if(com_argc > CMD_TOKEN_MAX + 1) com_argc = CMD_TOKEN_MAX + 1;
        com_argv[0] = (char *)"com";
        for(j = 1; j < tokens && (j + 1) < (CMD_TOKEN_MAX + 1); j++)
            com_argv[j + 1] = tokenList[j];

        /* probe MSC0:/SHELL/ then shelldir (ROM: by default) */
        {
            const char *prefixes[2];
            unsigned prefix_lens[2];
            int k;

            prefixes[0] = msc_prefix;
            prefix_lens[0] = (unsigned)(sizeof(msc_prefix) - 1u);
            prefixes[1] = shelldir;
            prefix_lens[1] = (unsigned)strlen(shelldir);

            for(k = 0; k < 2; k++) {
                prefix_len = prefix_lens[k];
                if(prefix_len + name_len + 5 <= sizeof(com_fname)) {
                    memcpy(com_fname, prefixes[k], prefix_len);
                    memcpy(com_fname + prefix_len, tokenList[0], name_len);
                    memcpy(com_fname + prefix_len + name_len, ".com", 5);
                    probe_fd = open(com_fname, O_RDONLY);
                    if(probe_fd >= 0) {
                        close(probe_fd);
                        com_argv[1] = com_fname;
                        return cmd_com(com_argc, com_argv);
                    }
                }
            }
        }
    }
    tx_string(EXCLAMATION "Unknown command" NEWLINE);
    return -1;
}

static int execute_cmd(cmdline_t *cl, const char *cmd) {
    unsigned len = (unsigned)strlen(cmd);
    if(len > CMD_BUF_MAX) len = CMD_BUF_MAX;
    memcpy(cl->buffer, cmd, len + 1);
    cl->bytes = (int)len;
    return execute(cl);
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

static void refresh_current_drive(void) { // helper for cmd_com & cmd_run
    if(f_getcwd(dir_cwd, sizeof(dir_cwd)) >= 0 && dir_cwd[1] == ':') {
        current_drive = dir_cwd[0];
    }
}

// ----------------- internal commands ----------------------

int cmd_cls(int, char **) { // clear screen
    cls();
    return 0;
}

int cmd_exit(int status, char **) { // exit to rp6502 monitor
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
            tx_string(EXCLAMATION "failed" NEWLINE);
            return -1;
        }
        return 0;
    }
    if(chdir(argv[1]) < 0) {
        tx_string(EXCLAMATION "failed" NEWLINE);
        return -1;
    }
    if(f_getcwd(dir_cwd, sizeof(dir_cwd)) >= 0 && dir_cwd[1] == ':') {
        current_drive = dir_cwd[0];
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
        tx_string(EXCLAMATION "invalid drive" NEWLINE);
        if(prev_path[0]) {
            drv[0] = prev_drive;
            f_chdrive(drv);
            chdir(prev_path);
        }
        return -1;
    }
    // verify drive is actually usable
    if(f_getcwd(dir_cwd, sizeof(dir_cwd)) < 0) {
        // tx_string("Drive not available" NEWLINE);
        drv[0] = prev_drive;
        if(f_chdrive(drv) == 0 && prev_path[0]) chdir(prev_path);
        return -1;
    }
    current_drive = drv[0];
    return 0;
}

int cmd_list(int argc, char **argv) {
    char buf[80];
    int fd;
    int n;
    int lines = 0;
    int lineno = 1;
    int at_line_start = 1;
    if(argc < 2) {
        tx_string("Usage: list <filename>" NEWLINE);
        return 0;
    }
    fd = open(argv[1], O_RDONLY);
    if(fd < 0) {
        tx_string(ANSI_RED EXCLAMATION "can't open file" ANSI_RESET NEWLINE);
        return -1;
    }
    tx_string(NEWLINE ANSI_DARK_GRAY "-- START --" ANSI_RESET NEWLINE);
    while((n = read(fd, buf, sizeof(buf))) > 0) {
        int idx;
        for(idx = 0; idx < n; idx++) {
            char ch = buf[idx];
            if(at_line_start) {
                tx_string(ANSI_DARK_GRAY);
                tx_dec32((unsigned long)lineno);
                tx_string(TAB ANSI_RESET);
                at_line_start = 0;
            }
            if(ch == '\n') {
                tx_string(NEWLINE);
                lines++;
                lineno++;
                at_line_start = 1;
                if(lines >= 28) {
                    char ans;
                    tx_string(NEWLINE ANSI_DARK_GRAY "--More-- (q to quit)" ANSI_RESET);
                    RX_READY_SPIN;
                    ans = (char)RIA.rx;
                    tx_string(NEWLINE "\x1b[K"); // clear prompt line
                    if(ans == 'q' || ans == 'Q' || ans == CHAR_ESC) {
                        close(fd);
                        tx_string(NEWLINE ANSI_DARK_GRAY "--- END ---" ANSI_RESET NEWLINE);
                        return 0;
                    }
                    lines = 0;
                }
            } else {
                tx_char(ch);
            }
        }
    }
    close(fd);
    tx_string(NEWLINE "--- END ---" NEWLINE);
    return 0;
}

int cmd_mkdir(int argc, char **argv) {
    if(argc < 2) {
        tx_string("Usage: mkdir <path>" NEWLINE);
        return 0;
    }
    if(f_mkdir(argv[1]) < 0) {
        tx_string(EXCLAMATION "failed" NEWLINE);
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
                tx_string(EXCLAMATION "failed: ");
                tx_string(arg);
                tx_string(NEWLINE);
                rc = -1;
            }
            continue;
        }

        // wildcard handling: split path/mask
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
                tx_string(EXCLAMATION "directory opening failed" NEWLINE);
                rc = -1;
                continue;
            }
            while(1) {
                int rr = f_readdir(&dir_ent, dd);
                if(rr < 0) {
                    tx_string(EXCLAMATION "directory reading failed" NEWLINE);
                    rc = -1;
                    break;
                }
                if(!dir_ent.fname[0]) break;
                if(dir_ent.fattrib & AM_DIR) continue;
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

int cmd_rename(int argc, char **argv) {
    if(argc < 3) {
        tx_string("Usage: rename <old> <new>" NEWLINE);
        return 0;
    }
    if(rename(argv[1], argv[2]) < 0) {
        tx_string(EXCLAMATION "failed" NEWLINE);
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
        tx_string(EXCLAMATION "can't open a file" NEWLINE);
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
        tx_string(EXCLAMATION "reading error" NEWLINE);
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
        tx_string(EXCLAMATION "can't open a file" NEWLINE);
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
            tx_string(EXCLAMATION "writing error" NEWLINE);
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
        tx_string(EXCLAMATION "can't open a file" NEWLINE);
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
        tx_string(EXCLAMATION "reading error" NEWLINE);
        return -1;
    }
    fn = (void (*)(void))start;
    fn();
    return 0;
}

int cmd_exe(int argc, char **argv) { // run binary with load address stored in last two bytes (LSB, MSB)
    int fd;
    unsigned long bytes_left;
    uint8_t addr_buf[2];
    uint16_t load_addr;
    uint16_t addr;
    void (*fn)(void);
    int user_argc;
    char **user_argv;

    if(argc < 2) {
        tx_string("Usage: exe <file.exe> [args...]" NEWLINE);
        return 0;
    }
    if(f_stat(argv[1], &dir_ent) < 0) {
        tx_string(EXCLAMATION "can't read file status" NEWLINE);
        return -1;
    }
    if(dir_ent.fsize < 2) {
        tx_string(EXCLAMATION "invalid EXE" NEWLINE);
        return -1;
    }

    fd = open(argv[1], O_RDONLY);
    if(fd < 0) {
        tx_string(EXCLAMATION "can't open a file" NEWLINE);
        return -1;
    }
    if(lseek(fd, (off_t)dir_ent.fsize - 2, SEEK_SET) < 0) {
        tx_string(EXCLAMATION "seeking failed" NEWLINE);
        close(fd);
        return -1;
    }
    if(read(fd, addr_buf, sizeof(addr_buf)) != (int)sizeof(addr_buf)) {
        tx_string(EXCLAMATION "reading failed" NEWLINE);
        close(fd);
        return -1;
    }
    load_addr = (uint16_t)(addr_buf[0] | ((uint16_t)addr_buf[1] << 8));
    if(lseek(fd, 0, SEEK_SET) < 0) {
        tx_string(EXCLAMATION "seeking failed" NEWLINE);
        close(fd);
        return -1;
    }

    bytes_left = dir_ent.fsize - 2;
    addr = load_addr;
    while(bytes_left) {
        int chunk = (bytes_left > (unsigned long)sizeof(bload_buf)) ? (int)sizeof(bload_buf) : (int)bytes_left;
        int read_bytes = read(fd, bload_buf, chunk);
        if(read_bytes <= 0) {
            tx_string(EXCLAMATION "reading error" NEWLINE);
            close(fd);
            return -1;
        }
        ram_writer(bload_buf, addr, (uint16_t)read_bytes);
        addr += (uint16_t)read_bytes;
        bytes_left -= (unsigned long)read_bytes;
    }
    close(fd);

    memcpy(run_args_backup, (void *)RUN_ARGS_BASE, RUN_ARGS_BLOCK_SIZE);
    user_argc = argc - 2;
    if(user_argc < 0) user_argc = 0;
    user_argv = argv + 2;
    build_run_args(user_argc, user_argv);

    fn = (void (*)(void))load_addr;
    fn();
    memcpy((void *)RUN_ARGS_BASE, run_args_backup, RUN_ARGS_BLOCK_SIZE);
    refresh_current_drive();
    return 0;
}

int cmd_com(int argc, char **argv) { // run external command
    int fd;
    int n;
    uint16_t addr = COM_LOAD_ADDR;
    void (*fn)(void);
    int user_argc;
    char **user_argv;
    static char path_buf[FNAMELEN];
    const char *resolved;
    unsigned name_len, prefix_len;

    if(argc < 2) {
        tx_string("Usage: com <file.com> [args...]" NEWLINE);
        return 0;
    }

    resolved = argv[1];
    fd = -1;

    if(!strchr(argv[1], ':') && !strchr(argv[1], '/')) {
        name_len = (unsigned)strlen(argv[1]);

        /* 1. current directory */
        fd = open(argv[1], O_RDONLY);

        /* 2. SHELLDRIVEDIRDEFAULT */
        if(fd < 0) {
            prefix_len = sizeof(SHELLDRIVEDIRDEFAULT) - 1;
            if(prefix_len + name_len < sizeof(path_buf)) {
                memcpy(path_buf, SHELLDRIVEDIRDEFAULT, prefix_len);
                memcpy(path_buf + prefix_len, argv[1], name_len + 1);
                fd = open(path_buf, O_RDONLY);
                if(fd >= 0) resolved = path_buf;
            }
        }

        /* 3. shelldir (defaults to SHELLDIRDEFAULT) */
        if(fd < 0) {
            prefix_len = (unsigned)strlen(shelldir);
            if(prefix_len + name_len < sizeof(path_buf)) {
                memcpy(path_buf, shelldir, prefix_len);
                memcpy(path_buf + prefix_len, argv[1], name_len + 1);
                fd = open(path_buf, O_RDONLY);
                if(fd >= 0) resolved = path_buf;
            }
        }
    } else {
        fd = open(argv[1], O_RDONLY);
    }

    // printf("%s", resolved);
    if(fd < 0) {
        tx_string(EXCLAMATION "isn't an internal or external command" NEWLINE);
        return -1;
    }

    while((n = read(fd, bload_buf, sizeof(bload_buf))) > 0) {
        ram_writer(bload_buf, addr, n);
        addr += (uint16_t)n;
    }
    close(fd);
    if(n < 0) {
        tx_string(EXCLAMATION "reading error" NEWLINE);
        return -1;
    }

    /* Save and overwrite argc/argv block */
    memcpy(run_args_backup, (void *)RUN_ARGS_BASE, RUN_ARGS_BLOCK_SIZE);
    user_argc = argc - 2;
    if(user_argc < 0) user_argc = 0;
    user_argv = argv + 2;
    build_run_args(user_argc, user_argv);

    fn = (void (*)(void))COM_LOAD_ADDR;
    fn();
    memcpy((void *)RUN_ARGS_BASE, run_args_backup, RUN_ARGS_BLOCK_SIZE);
    refresh_current_drive();
    return 0;
}

int cmd_run(int argc, char **argv) { // run at address with optional args
    void (*fn)(void);
    uint16_t addr;
    int user_argc;
    char **user_argv;

    if(argc ==  2 && strcmp(argv[1],"/?") == 0) {
        tx_string("Usage: run [addr] [arg1, ...]" NEWLINE);
        return 0;
    }
    if(argc == 1){
        addr = COM_LOAD_ADDR;
    } else {
        addr = (uint16_t)strtoul(argv[1], NULL, 16);
    }
    memcpy(run_args_backup, (void *)RUN_ARGS_BASE, RUN_ARGS_BLOCK_SIZE);
    user_argc = argc - 2;
    if(user_argc < 0) user_argc = 0;
    user_argv = argv + 2;
    build_run_args(user_argc, user_argv);
    fn = (void (*)(void))addr;
    fn();
    memcpy((void *)RUN_ARGS_BASE, run_args_backup, RUN_ARGS_BLOCK_SIZE);
    refresh_current_drive();
    return 0;
}

int cmd_copy(int argc, char **argv) {
    char buf[64];
    int src, dst;
    int n;
    if(argc < 3) {
        tx_string("Usage: copy <src> <dst>" NEWLINE);
        return 0;
    }
    src = open(argv[1], O_RDONLY);
    if(src < 0) {
        tx_string(EXCLAMATION "can't open source" NEWLINE);
        return -1;
    }
    dst = open(argv[2], O_WRONLY | O_CREAT | O_TRUNC);
    if(dst < 0) {
        tx_string(EXCLAMATION "can't open destination" NEWLINE);
        close(src);
        return -1;
    }

    while((n = read(src, buf, sizeof(buf))) > 0) {
        if(write(dst, buf, n) != n) {
            tx_string(EXCLAMATION "write error" NEWLINE);
            close(src);
            close(dst);
            return -1;
        }
    }
    close(src);
    close(dst);
    return 0;
}

int cmd_cp(int argc, char **argv) {
    int dirdes = -1;
    int mv_mode = 0;
    int rc = 0;
    int count = 0;
    if(argc < 3) {
        tx_string("Usage: cp <src> <dst> [/m]" NEWLINE);
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
        tx_string(EXCLAMATION "directory opening failed" NEWLINE);
        return -1;
    }

    /* Pass 1: count matching files */
    while(1) {
        rc = f_readdir(&dir_ent, dirdes);
        if(rc < 0) {
            tx_string(EXCLAMATION "directory reading failed" NEWLINE);
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
        tx_string(EXCLAMATION "directory opening failed" NEWLINE);
        return -1;
    }

    while(1) {
        rc = f_readdir(&dir_ent, dirdes);
        if(rc < 0) {
            tx_string(EXCLAMATION "directory reading failed" NEWLINE);
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
        tx_string(" > ");
        tx_string(cpm_dstfile);
        tx_string(NEWLINE);

        if(!mv_mode){
            cpm_args[0] = "copy";
            cpm_args[1] = cpm_srcfile;
            cpm_args[2] = cpm_dstfile;
            rc = cmd_copy(3, cpm_args);
            if(rc < 0) {tx_string(ANSI_RED EXCLAMATION "copying error" ANSI_RESET); break;}
        }

        if(mv_mode) {
            rc = rename(cpm_srcfile, cpm_dstfile);
            if(rc < 0) {tx_string(ANSI_RED EXCLAMATION "moving error" ANSI_RESET); break;}
        }
    }

    f_closedir(dirdes);
    return (rc < 0) ? -1 : 0;
}

#ifdef CODE_PEEK
int cmd_peek(int argc, char **argv) {
    uint16_t addr = 0;
    uint16_t size = 16;
    uint8_t use_xram = 0;

    if(argc < 2) {
        tx_string("Usage: peek addr [bytes] [/x]" NEWLINE);
        return 0;
    }
    addr = strtoul(argv[1], NULL, 16);
    if(argc > 2) {
        if(!strcmp(argv[2], "/x")) {
            use_xram = 1;
        } else {
            size = strtoul(argv[2], NULL, 0);
        }
    }
    if(argc > 3 && (!strcmp(argv[3], "/x"))) {
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
#endif

#ifdef CODE_TIME
int cmd_time(int argc, char **argv) {
    (void)argc; (void)argv;
    show_time();
    return 0;
}
#endif

#ifdef CODE_PHI2
int cmd_phi2(int argc, char **argv) {
    int hz = phi2();
    (void)argc; (void)argv;
    tx_string("65C02 system clock speed: ");
    tx_dec32(hz);
    tx_string(" Hz" NEWLINE);
    return 0;
}
#endif

int cmd_mem(int argc, char **argv) {
    uint16_t bottom = mem_lo();
    uint16_t top = mem_top();
    uint16_t free = mem_free();
    (void)argc; (void)argv;
    tx_string(NEWLINE APPNAME " memory info" NEWLINE NEWLINE);
    tx_hex16(bottom);
    tx_string(" lowest" NEWLINE);
    tx_hex16(top);
    tx_string(" highest" NEWLINE );
    tx_dec32(free);
    tx_string(" B free" NEWLINE );
    tx_hex16(COM_LOAD_ADDR);
    tx_string(" .com load address" NEWLINE);
    return 0;
}

#ifdef CODE_HEXDUMP
int cmd_hex(int argc, char **argv) {
    uint32_t offset = 0;
    uint32_t bytes;
    uint32_t maxbytes;
    uint16_t dump_bytes;

    if(argc < 2) {
        tx_string("Usage: hex <file> [offset] [bytes]" NEWLINE);
        return 0;
    }
    if(f_stat(argv[1], &dir_ent) < 0) {
        tx_string(EXCLAMATION "stat failed" NEWLINE);
        return -1;
    }
    maxbytes = dir_ent.fsize;
    if(argc > 2) offset = (uint32_t)strtoul(argv[2], NULL, 0);
    if(offset > maxbytes) {
        tx_string(EXCLAMATION "offset past end of file" NEWLINE);
        return -1;
    }
    maxbytes -= offset;
    bytes = (argc > 3) ? (uint32_t)strtoul(argv[3], NULL, 0) : maxbytes;
    if(bytes > maxbytes) bytes = maxbytes;
    if(bytes == 0) {
        tx_string(EXCLAMATION "nothing to show" NEWLINE);
        return 0;
    }
    filehex_fd = open(argv[1], O_RDONLY);
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
#endif
int cmd_stat(int argc, char **argv) {
    if(argc < 2) {
        tx_string("Usage: stat <path>" NEWLINE);
        return 0;
    }
    if(f_stat(argv[1], &dir_ent) < 0) {
        tx_string(EXCLAMATION "stat failed" NEWLINE);
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
    tx_string(NEWLINE);
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
        tx_string(EXCLAMATION "nothing to change" NEWLINE);
        return 0;
    }
    if(f_chmod(argv[1], set, clear | set) < 0) {
        tx_string(EXCLAMATION "chmod failed" NEWLINE);
        return -1;
    }
    return 0;
}

int cmd_ls(int argc, char **argv){
    int dirdes;
    int rc = 0;
    (void)argc;
    (void)argv;

    dirdes = f_opendir(".");
    if(dirdes < 0) {
        tx_string(EXCLAMATION "open directory failed" NEWLINE);
        return -1;
    }

    tx_string(NEWLINE 
              "size      file/directory name" NEWLINE
              "--------- -------------------" NEWLINE
              );

    while(1) {
        rc = f_readdir(&dir_ent, dirdes);
        if(rc < 0) { tx_string(EXCLAMATION "readdir failed" NEWLINE); break;}
        if(!dir_ent.fname[0]) break; // end of directory

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
        tx_string(EXCLAMATION "closedir failed" NEWLINE);
        rc = -1;
    }
    return rc;
}

#ifdef CODE_LAUNCH
int cmd_launcher(int argc, char **argv){

    if(argc < 2) {
        tx_string("Usage: launcher /s - register as a launcher" NEWLINE
                  "       launcher /r - deregister" NEWLINE
                  "       launcher /status - status" NEWLINE);
        return 0;
    } else if(!strcmp(argv[1],"/s")){
        ria_attr_set(1, RIA_ATTR_LAUNCHER);
        return 1;
    } else if(!strcmp(argv[1],"/r")) {
        ria_attr_set(0, RIA_ATTR_LAUNCHER);
        return -1;
    } else if(!strcmp(argv[1],"/status") && ria_attr_get(RIA_ATTR_LAUNCHER)){
        tx_string(NEWLINE APPNAME " is a launcher" NEWLINE); 
    } else {
        tx_string(NEWLINE APPNAME " isn't a launcher" NEWLINE); 
    }
    return 0;

}
#endif

#ifdef CODE_CART
int cmd_cart(int argc, char **argv){
    if(argc < 2) {
        tx_string("Usage: cart romname (without .rp6502 extension)" NEWLINE);
    } else {
        char fname[FNAMELEN];
        int cf;
        strcpy(fname, argv[1]);
        strcat(fname, ".rp6502");
        cf = open(fname, O_RDONLY);
        if(cf >= 0){
            close(cf);
            PAUSE(25);
            // ria_execl(fname);
            ria_execl(fname, NULL);
        } else {
            tx_string(NEWLINE EXCLAMATION "file not found" NEWLINE);
        }
    }
    return 0;
}
#endif
