#include <rp6502.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <fcntl.h>
#include "commons.h"

#define APPVER "20260505.0716"
#define APPNAME "Editor"
#define APP_MSG_TITLE CSI "2;1H" CSI HIGHLIGHT_COLOR " razemOS > " ANSI_RESET " " APPNAME ANSI_DARK_GRAY CSI "2;60Hversion " APPVER ANSI_RESET

/* autorepeat: clock() ticks are centiseconds (1 tick = 10 ms) */
#define REPEAT_DELAY  40u   /* 400 ms before first repeat */
#define REPEAT_RATE    5u   /* 50 ms between repeats */

/* --- XRAM register addresses --- */
#define XRAM_STRUCT_SYS_KEYBOARD 0xFF20
#define XRAM_STRUCT_SYS_MOUSE    0xFF40

/* --- GFX canvas --- */
//#define GFX_CANVAS_640x480       0b00000011

/* --- XRAM text buffer: 256 rows x 80 cols x 1 byte = 20480 bytes --- */
#define TEXT_BUF_BASE    0x0000u
#define TEXT_COLS        80u
#define XRAM_SCRATCH     0xA200u   /* scratch for file write (82 bytes) */

/* --- Terminal dimensions (640x480, 16px font) --- */
#define TERM_ROWS        30u
#define TITLE_ROWS       3u    /* fixed title bar at top */
#define MENU_ROWS        4u    /* fixed menu at bottom */
#define EDIT_ROWS        (TERM_ROWS - TITLE_ROWS - MENU_ROWS) /* editable area */

/* --- ANSI escape helpers --- */
#define ANSI_HOME        "\033[H"
#define ANSI_HIDE_CUR    "\033[?25l"
#define ANSI_SHOW_CUR    "\033[?25h"
#define ANSI_REVERSE     "\033[0;7m"
#define ANSI_NORMAL      "\033[0m"

/* --- keyboard --- */
#define KEYBOARD_BYTES 32
uint8_t keystates[KEYBOARD_BYTES] = {0};
#define key(code) (keystates[(code) >> 3] & (1u << ((code) & 7)))

/* --- cursor --- */
struct Cursor {
    uint8_t row;
    uint8_t col;
};
static struct Cursor cur;

/* --- scroll / content state --- */
static uint8_t  scroll_row    = 0u;
static uint16_t content_rows  = 0u;

/* --- file and search state --- */
static char current_filename[64];
static char search_pattern[32];
static char g_linebuf[82];

/* --- Insert/Overwrite mode and clipboard --- */
static uint8_t insert_mode = 1u;
static uint8_t sel_active  = 0u;
static uint8_t sel_row     = 0u;
static uint8_t sel_col     = 0u;
static char    clipboard[81];
static uint8_t clip_len    = 0u;
static char    line_tmp[80];

/* ================================================================
   keycode_to_char: USB HID keycode -> ASCII
   ================================================================ */
static char keycode_to_char(uint8_t code, uint8_t shift, uint8_t caps)
{
    uint8_t upper;
    static const char sh_digits[9] = {'!','@','#','$','%','^','&','*','('};

    if (code >= KEY_A && code <= KEY_Z) {
        upper = shift ? (uint8_t)(!caps) : caps;
        return (char)(code + (upper ? 61 : 93));
    }
    if (code >= KEY_1 && code <= KEY_9) {
        return shift ? sh_digits[code - KEY_1] : (char)('1' + code - KEY_1);
    }
    if (code == KEY_0)          return shift ? ')' : '0';
    if (code == KEY_SPACE)      return ' ';
    if (code == KEY_MINUS)      return shift ? '_' : '-';
    if (code == KEY_DOT)        return shift ? '>' : '.';
    if (code == KEY_SLASH)      return shift ? '?' : '/';
    if (code == KEY_SEMICOLON)  return shift ? ':' : ';';
    if (code == KEY_EQUAL)      return shift ? '+' : '=';
    if (code == KEY_APOSTROPHE) return shift ? '"' : '\'';
    if (code == KEY_COMMA)      return shift ? '<' : ',';
    return 0;
}

/* ================================================================
   menu_print_row: prints text in reverse video to one terminal row,
   padding to TEXT_COLS with spaces.
   ansi_row is 1-based.
   ================================================================ */
static void menu_print_row(uint8_t ansi_row, const char *text)
{
    uint8_t i;
    printf("\033[%d;1H", (int)ansi_row);
    for (i = 0u; text[i] && i < TEXT_COLS; i++) putchar((uint8_t)text[i]);
    while (i < TEXT_COLS) { putchar(' '); i++; }
    printf(ANSI_RESET);
}

/* ================================================================
   draw_title_bar: 3-row title bar at top of terminal (rows 1-3).
   Row 1: empty
   Row 2: program name, version
   Row 3: just semigraphical horizontal line
   ================================================================ */
static void draw_title_bar(void)
{
    static const char menu_line1[] = APP_MSG_TITLE;
    uint8_t i, fn_len, line_len;

    printf("\033[2;1H");
    for (i = 0u; menu_line1[i]; i++) putchar((uint8_t)menu_line1[i]);

    printf("\033[3;1H");
    if (current_filename[0]) {
        for (fn_len = 0u; current_filename[fn_len]; fn_len++) {}
        /* "[" + filename + "]" = fn_len + 2; leave at least 1 line char */
        line_len = (fn_len + 2u < 79u) ? (uint8_t)(80u - fn_len - 2u) : 1u;
        for (i = 0u; i < line_len; i++) putchar('\xc4');
        putchar('\xb4');
        for (i = 0u; i < fn_len; i++) putchar((uint8_t)current_filename[i]);
        putchar('\xb3');
    } else {
        for (i = 0u; i < 80u; i++) putchar('\xc4');
    }
    printf(ANSI_NORMAL);
}

/* ================================================================
   draw_menu_bar: 4-row status bar at bottom of terminal.
   Row EDIT_ROWS+1: just semigraphical horizontal line
   Row EDIT_ROWS+2: key shortcuts
   Row EDIT_ROWS+3: [INS]/[OVR] + status/filename
   Row EDIT_ROWS+4: MARK SET indicator
   ================================================================ */
static void draw_menu_bar(const char *status)
{
    uint8_t     i;
    const char *mode_str;
    const char *info;
    char        row2[81];

    mode_str = insert_mode ? "mode [INS]" : "mode [OVR]";
    info     = status ? status : (current_filename[0] ? current_filename : "none");

    printf("\033[%d;1H", TITLE_ROWS + EDIT_ROWS + 1u);
    for (i = 0u; i < 80u; i++) putchar('\xc4');

    menu_print_row(TITLE_ROWS + EDIT_ROWS + 2u,
        "[Ins] type mode [^K] mark [F5] open [F6] save [F7] find [Esc] exit");

    /* compose "[INS/OVR] info..." */
    for (i = 0u; mode_str[i]; i++) row2[i] = mode_str[i];
    row2[i++] = ' ';
    for (; info[0] && (i < 80u); i++, info++) row2[i] = *info;
    row2[i] = 0;
    menu_print_row(TITLE_ROWS + EDIT_ROWS + 3u, row2);

    menu_print_row(TITLE_ROWS + EDIT_ROWS + 4u,
        sel_active ? "MARK SET  [^C] copy [^X] cut [^K] clear" : "");
}

/* ================================================================
   redraw_screen: redraws EDIT_ROWS visible lines from XRAM + menu.
   Repositions terminal cursor at cur.row/col.
   ================================================================ */
static void redraw_screen(void)
{
    uint8_t  r, j;
    uint16_t xrow;

    printf(ANSI_HIDE_CUR ANSI_HOME);
    draw_title_bar();
    for (r = 0u; r < EDIT_ROWS; r++) {
        xrow = (uint16_t)scroll_row + r;
        printf("\033[%d;1H", (int)(r + 1u + TITLE_ROWS));
        if (xrow < content_rows) {
            RIA.addr1 = TEXT_BUF_BASE + xrow * TEXT_COLS;
            RIA.step1 = 1;
            for (j = 0u; j < TEXT_COLS; j++) {
                char c = (char)RIA.rw1;
                g_linebuf[j] = c ? c : ' ';
            }
            for (j = 0u; j < TEXT_COLS; j++) putchar((uint8_t)g_linebuf[j]);
        } else if (xrow == content_rows) {
            /* "- END of document -" centred in dark grey */
            static const char eod[] = "- End of document -";
            printf(ANSI_DARK_GRAY);
            for (j = 0u; eod[j]; j++) putchar((uint8_t)eod[j]);
            printf(ANSI_NORMAL);
        } else {
            /* blank lines beyond END marker */
            for (j = 0u; j < TEXT_COLS; j++) putchar(' ');
        }
    }
    draw_menu_bar(NULL);
    printf("\033[%d;%dH" ANSI_SHOW_CUR,
           (int)((cur.row - scroll_row) + 1u + TITLE_ROWS),
           (int)(cur.col + 1u));
}

/* ================================================================
   menu_input: blocking text-input dialog in menu rows 2-3.
   Returns 1 on Enter, 0 on Esc.
   ================================================================ */
static int menu_input(const char *prompt, char *buf, uint8_t maxlen)
{
    uint8_t prev_ks[KEYBOARD_BYTES];
    uint8_t cur_ks[KEYBOARD_BYTES];
    uint8_t len, pos, k, j, code, was, now, i;
    uint8_t shift, caps;
    int     done, result;
    char    ch;
    uint8_t input_row = TITLE_ROWS + EDIT_ROWS + 4u;

    /* measure pre-filled content so caller can seed the buffer */
    for (len = 0u; buf[len] && len < (uint8_t)(maxlen - 1u); len++) {}
    pos    = len;   /* cursor at end of pre-filled text */
    done   = 0;
    result = 0;

    for (k = 0u; k < KEYBOARD_BYTES; k++) prev_ks[k] = keystates[k];

    menu_print_row(TITLE_ROWS + EDIT_ROWS + 3u, prompt);
    menu_print_row(input_row, buf);
    printf(ANSI_SHOW_CUR "\033[%d;%dH", (int)input_row, (int)(pos + 1u));

    while (!done) {
        for (k = 0u; k < KEYBOARD_BYTES; k++) {
            RIA.addr1 = XRAM_STRUCT_SYS_KEYBOARD + k;
            RIA.step1 = 0;
            cur_ks[k] = RIA.rw1;
        }

        shift = (uint8_t)(((cur_ks[KEY_LEFTSHIFT  >> 3] >> (KEY_LEFTSHIFT  & 7)) & 1u) |
                           ((cur_ks[KEY_RIGHTSHIFT >> 3] >> (KEY_RIGHTSHIFT & 7)) & 1u));
        caps  = (uint8_t)( (cur_ks[KEY_CAPSLOCK   >> 3] >> (KEY_CAPSLOCK   & 7)) & 1u);

        for (k = 0u; k < KEYBOARD_BYTES && !done; k++) {
            for (j = 0u; j < 8u && !done; j++) {
                was = (prev_ks[k] >> j) & 1u;
                now = (cur_ks [k] >> j) & 1u;
                if (!was && now) {
                    code = (uint8_t)((k << 3) | j);
                    if (code == KEY_ENTER || code == KEY_KPENTER) {
                        result = 1; done = 1;
                    } else if (code == KEY_ESC) {
                        result = 0; done = 1;
                    } else if (code == KEY_LEFT) {
                        if (pos > 0u) pos--;
                    } else if (code == KEY_RIGHT) {
                        if (pos < len) pos++;
                    } else if (code == KEY_HOME) {
                        pos = 0u;
                    } else if (code == KEY_END) {
                        pos = len;
                    } else if (code == KEY_BACKSPACE) {
                        if (pos > 0u) {
                            for (i = pos - 1u; i < len - 1u; i++) buf[i] = buf[i + 1u];
                            len--;
                            pos--;
                            buf[len] = 0;
                            menu_print_row(input_row, buf);
                        }
                    } else if (code == KEY_DELETE) {
                        if (pos < len) {
                            for (i = pos; i < len - 1u; i++) buf[i] = buf[i + 1u];
                            len--;
                            buf[len] = 0;
                            menu_print_row(input_row, buf);
                        }
                    } else {
                        ch = keycode_to_char(code, shift, caps);
                        if (ch && len < (uint8_t)(maxlen - 1u)) {
                            for (i = len; i > pos; i--) buf[i] = buf[i - 1u];
                            buf[pos++] = ch;
                            len++;
                            buf[len] = 0;
                            menu_print_row(input_row, buf);
                        }
                    }
                    /* reposition terminal cursor after every keystroke */
                    printf("\033[%d;%dH", (int)input_row, (int)(pos + 1u));
                }
            }
            prev_ks[k] = cur_ks[k];
        }
    }

    printf(ANSI_HIDE_CUR);
    for (k = 0u; k < KEYBOARD_BYTES; k++) keystates[k] = cur_ks[k];
    return result;
}

/* Returns one past the last non-space byte in the given XRAM row (0..TEXT_COLS). */
static uint8_t line_text_len(uint8_t row)
{
    uint8_t j, last = 0u;
    RIA.addr1 = TEXT_BUF_BASE + (uint16_t)row * TEXT_COLS;
    RIA.step1 = 1;
    for (j = 0u; j < TEXT_COLS; j++) {
        if ((char)RIA.rw1 != ' ') last = j + 1u;
    }
    return last;
}

/* ================================================================
   editor_clear: fills XRAM text buffer with spaces and clears
   the terminal display.
   ================================================================ */
static void editor_clear(void)
{
    uint16_t i;

    RIA.addr0 = TEXT_BUF_BASE;
    RIA.step0 = 1;
    for (i = 0u; i < 20480u; i++) RIA.rw0 = ' ';

    printf(ANSI_HIDE_CUR ANSI_CLS ANSI_HOME);

    cur.row      = 0u;
    cur.col      = 0u;
    scroll_row   = 0u;
    content_rows = 0u;
    sel_active   = 0u;
}

/* ================================================================
   load_file: reads text file into XRAM text buffer.
   ================================================================ */
static int load_file(const char *filename)
{
    int      fd, nbytes;
    uint16_t row;
    uint8_t  col, bi, n, need_addr, stop;
    char     c;

    fd = open(filename, O_RDONLY);
    if (fd < 0) return -1;

    editor_clear();
    row       = 0u;
    col       = 0u;
    need_addr = 1u;
    stop      = 0u;

    for (;;) {
        if (stop) break;
        nbytes = read(fd, g_linebuf, 80);
        if (nbytes <= 0) {
            if (col > 0u) row++;
            break;
        }
        n = (uint8_t)nbytes;
        for (bi = 0u; bi < n && !stop; bi++) {
            c = g_linebuf[bi];
            if (c == '\r') continue;
            if (c == '\n') {
                row++;
                col       = 0u;
                need_addr = 1u;
                if (row >= 256u) stop = 1u;
            } else if (col < TEXT_COLS) {
                if (need_addr) {
                    RIA.addr0 = TEXT_BUF_BASE + row * TEXT_COLS;
                    RIA.step0 = 1;
                    need_addr = 0u;
                }
                RIA.rw0 = (uint8_t)c;
                col++;
            }
        }
    }

    close(fd);

    content_rows = row;
    strncpy(current_filename, filename, 63u);
    current_filename[63] = 0;
    cur.row    = 0u;
    cur.col    = 0u;
    scroll_row = 0u;

    redraw_screen();
    return 1;
}

/* ================================================================
   save_file: writes XRAM text buffer to file, trimming trailing spaces.
   ================================================================ */
static int save_file(const char *filename)
{
    int      fd;
    uint16_t row;
    uint8_t  j, len;

    fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) return -1;

    for (row = 0u; row < content_rows; row++) {
        RIA.addr1 = TEXT_BUF_BASE + row * TEXT_COLS;
        RIA.step1 = 1;
        for (j = 0u; j < TEXT_COLS; j++) g_linebuf[j] = (char)RIA.rw1;

        len = TEXT_COLS;
        while (len > 0u && g_linebuf[len - 1u] == ' ') len--;

        g_linebuf[len] = '\n';
        /* copy to XRAM scratch, then write via write_xram (write() is hijacked to UART) */
        RIA.addr0 = XRAM_SCRATCH;
        RIA.step0 = 1;
        for (j = 0u; j <= len; j++) RIA.rw0 = (uint8_t)g_linebuf[j];
        write_xram(XRAM_SCRATCH, (unsigned)(len + 1u), fd);
    }

    close(fd);
    strncpy(current_filename, filename, 63u);
    current_filename[63] = 0;
    return 1;
}

/* ================================================================
   find_text: searches XRAM buffer for pattern from one position past
   cursor. Highlights match with reverse video and scrolls.
   ================================================================ */
static int find_text(const char *pattern)
{
    uint8_t  plen, i, col, match;
    uint16_t row, start_row;
    uint8_t  start_col;
    uint8_t  disp_row;

    plen = (uint8_t)strlen(pattern);
    if (plen == 0u || plen > TEXT_COLS) return 0;

    if (cur.col + 1u < TEXT_COLS) {
        start_row = (uint16_t)cur.row;
        start_col = (uint8_t)(cur.col + 1u);
    } else {
        start_row = (uint16_t)cur.row + 1u;
        start_col = 0u;
    }

    for (row = start_row; row < content_rows; row++) {
        RIA.addr1 = TEXT_BUF_BASE + row * TEXT_COLS;
        RIA.step1 = 1;
        for (i = 0u; i < TEXT_COLS; i++) g_linebuf[i] = (char)RIA.rw1;

        col = (row == start_row) ? start_col : 0u;

        for (; (uint16_t)col + plen <= TEXT_COLS; col++) {
            match = 1u;
            for (i = 0u; i < plen; i++) {
                if (g_linebuf[(uint8_t)(col + i)] != pattern[i]) { match = 0u; break; }
            }
            if (match) {
                cur.row = (uint8_t)row;
                cur.col = col;

                /* scroll so found line appears near center */
                if ((uint16_t)row > (uint16_t)(EDIT_ROWS / 2u)) {
                    scroll_row = (uint8_t)(row - EDIT_ROWS / 2u);
                    if (content_rows > EDIT_ROWS &&
                        scroll_row > (uint8_t)(content_rows - EDIT_ROWS))
                        scroll_row = (uint8_t)(content_rows - EDIT_ROWS);
                } else {
                    scroll_row = 0u;
                }

                redraw_screen();

                /* highlight matched text with reverse video */
                /*
                disp_row = (uint8_t)(row - scroll_row);
                printf("\033[%d;%dH" CSI HIGHLIGHT_COLOR,
                       (int)(disp_row + 1u + TITLE_ROWS), (int)(col + 1u));
                for (i = 0u; i < plen; i++) {
                    putchar(g_linebuf[(uint8_t)(col + i)]
                            ? g_linebuf[(uint8_t)(col + i)] : ' ');
                }
                printf(ANSI_RESET);
                */
                // restore cursor position
                printf("\033[%d;%dH", (int)(disp_row + 1u + TITLE_ROWS), (int)(col + 1u));
                return 1;
            }
        }
    }
    return 0;
}

/* ================================================================
   line_shift_right: shifts chars at positions col..78 one slot right.
   ================================================================ */
static void line_shift_right(uint8_t row, uint8_t col)
{
    uint8_t i, n;

    n = (uint8_t)(79u - col);
    if (n == 0u) return;

    RIA.addr1 = TEXT_BUF_BASE + (uint16_t)row * TEXT_COLS + col;
    RIA.step1 = 1;
    for (i = 0u; i < n; i++) line_tmp[i] = (char)RIA.rw1;

    RIA.addr0 = TEXT_BUF_BASE + (uint16_t)row * TEXT_COLS + col + 1u;
    RIA.step0 = 1;
    for (i = 0u; i < n; i++) RIA.rw0 = (uint8_t)line_tmp[i];
}

/* ================================================================
   line_shift_left: shifts chars at positions from_col..79 one slot left.
   ================================================================ */
static void line_shift_left(uint8_t row, uint8_t from_col)
{
    uint8_t i, n;

    if (from_col == 0u) return;
    n = (uint8_t)(80u - from_col);

    RIA.addr1 = TEXT_BUF_BASE + (uint16_t)row * TEXT_COLS + from_col;
    RIA.step1 = 1;
    for (i = 0u; i < n; i++) line_tmp[i] = (char)RIA.rw1;

    RIA.addr0 = TEXT_BUF_BASE + (uint16_t)row * TEXT_COLS + from_col - 1u;
    RIA.step0 = 1;
    for (i = 0u; i < n; i++) RIA.rw0 = (uint8_t)line_tmp[i];

    /* clear last position */
    RIA.addr0 = TEXT_BUF_BASE + (uint16_t)row * TEXT_COLS + 79u;
    RIA.step0 = 0;
    RIA.rw0   = ' ';
}

/* ================================================================
   rows_shift_down: moves rows [from_row .. content_rows-1] one slot
   down in XRAM (row by row, bottom-up), then clears from_row.
   Caller must have checked content_rows < 255 before calling.
   ================================================================ */
static void rows_shift_down(uint8_t from_row)
{
    uint16_t r;
    uint8_t  j;

    /* copy bottom-up so we don't overwrite source before reading it */
    for (r = (uint16_t)content_rows; r > (uint16_t)from_row; r--) {
        RIA.addr1 = TEXT_BUF_BASE + (r - 1u) * TEXT_COLS;
        RIA.step1 = 1;
        for (j = 0u; j < TEXT_COLS; j++) line_tmp[j] = (char)RIA.rw1;
        RIA.addr0 = TEXT_BUF_BASE + r * TEXT_COLS;
        RIA.step0 = 1;
        for (j = 0u; j < TEXT_COLS; j++) RIA.rw0 = (uint8_t)line_tmp[j];
    }

    /* clear the newly freed row */
    RIA.addr0 = TEXT_BUF_BASE + (uint16_t)from_row * TEXT_COLS;
    RIA.step0 = 1;
    for (j = 0u; j < TEXT_COLS; j++) RIA.rw0 = ' ';
}

/* ================================================================
   rows_shift_up: moves rows [from_row+1 .. content_rows-1] one slot
   up in XRAM, then clears the last row, decrements content_rows.
   ================================================================ */
static void rows_shift_up(uint8_t from_row)
{
    uint16_t r;
    uint8_t  j;

    for (r = (uint16_t)from_row + 1u; r < (uint16_t)content_rows; r++) {
        RIA.addr1 = TEXT_BUF_BASE + r * TEXT_COLS;
        RIA.step1 = 1;
        for (j = 0u; j < TEXT_COLS; j++) line_tmp[j] = (char)RIA.rw1;
        RIA.addr0 = TEXT_BUF_BASE + (r - 1u) * TEXT_COLS;
        RIA.step0 = 1;
        for (j = 0u; j < TEXT_COLS; j++) RIA.rw0 = (uint8_t)line_tmp[j];
    }

    /* clear the vacated last row */
    if (content_rows > 0u) {
        RIA.addr0 = TEXT_BUF_BASE + (uint16_t)(content_rows - 1u) * TEXT_COLS;
        RIA.step0 = 1;
        for (j = 0u; j < TEXT_COLS; j++) RIA.rw0 = ' ';
        content_rows--;
    }
}

/* ================================================================
   do_backspace_join: join current row onto end of previous row.
   The text of current row is appended (starting at prev row's text
   length), then current row is removed by shifting rows up.
   Cursor moves to the join point on the previous row.
   ================================================================ */
static void do_backspace_join(void)
{
    uint8_t prev_len, cur_len, j, write_col;
    char    cur_line[80];

    if (cur.row == 0u) return;

    prev_len = line_text_len((uint8_t)(cur.row - 1u));
    cur_len  = line_text_len(cur.row);

    /* if merged text would exceed one row, only move cursor — don't destroy data */
    if ((uint16_t)prev_len + cur_len > (uint16_t)TEXT_COLS) {
        cur.row--;
        cur.col = prev_len;
        if (cur.row < scroll_row) scroll_row = cur.row;
        redraw_screen();
        return;
    }

    /* safe to merge: append current row text to previous row */
    if (cur_len > 0u) {
        RIA.addr1 = TEXT_BUF_BASE + (uint16_t)cur.row * TEXT_COLS;
        RIA.step1 = 1;
        for (j = 0u; j < cur_len; j++) cur_line[j] = (char)RIA.rw1;
    }

    write_col = prev_len;
    for (j = 0u; j < cur_len; j++, write_col++) {
        RIA.addr0 = TEXT_BUF_BASE + (uint16_t)(cur.row - 1u) * TEXT_COLS + write_col;
        RIA.step0 = 0;
        RIA.rw0   = (uint8_t)cur_line[j];
    }

    cur.row--;
    cur.col = prev_len;

    rows_shift_up(cur.row + 1u);

    if (cur.row < scroll_row) scroll_row = cur.row;

    redraw_screen();
}

/* ================================================================
   do_enter: split current row at cur.col.
   Text from cur.col..79 of current row moves to start of next row;
   all rows below shift down by one.
   ================================================================ */
static void do_enter(void)
{
    uint8_t tail_len, j;
    char    tail[80];

    if (cur.row >= 255u) return;

    /* save tail (text after cursor on this row) */
    tail_len = 0u;
    if (cur.col < TEXT_COLS) {
        RIA.addr1 = TEXT_BUF_BASE + (uint16_t)cur.row * TEXT_COLS + cur.col;
        RIA.step1 = 1;
        for (j = cur.col; j < TEXT_COLS; j++) {
            char c = (char)RIA.rw1;
            tail[tail_len++] = c ? c : ' ';
        }
        /* trim trailing spaces from tail */
        while (tail_len > 0u && tail[tail_len - 1u] == ' ') tail_len--;
    }

    /* erase tail from current row */
    RIA.addr0 = TEXT_BUF_BASE + (uint16_t)cur.row * TEXT_COLS + cur.col;
    RIA.step0 = 1;
    for (j = cur.col; j < TEXT_COLS; j++) RIA.rw0 = ' ';

    /* shift all rows below current row down by one */
    rows_shift_down((uint8_t)(cur.row + 1u));
    content_rows++;

    /* write tail at start of new row */
    if (tail_len > 0u) {
        RIA.addr0 = TEXT_BUF_BASE + (uint16_t)(cur.row + 1u) * TEXT_COLS;
        RIA.step0 = 1;
        for (j = 0u; j < tail_len; j++) RIA.rw0 = (uint8_t)tail[j];
    }

    /* advance cursor */
    cur.row++;
    cur.col = 0u;
    if ((uint8_t)(cur.row - scroll_row) >= EDIT_ROWS)
        scroll_row = (uint8_t)(cur.row - EDIT_ROWS + 1u);

    redraw_screen();
}

/* ================================================================
   do_copy: copies selection or current line to clipboard.
   ================================================================ */
static void do_copy(void)
{
    uint8_t i, start_col, end_col;

    if (sel_active && sel_row == cur.row) {
        if (sel_col < cur.col) { start_col = sel_col; end_col = cur.col; }
        else                   { start_col = cur.col; end_col = sel_col; }
        clip_len = (uint8_t)(end_col - start_col);
        if (clip_len > TEXT_COLS) clip_len = (uint8_t)TEXT_COLS;
        RIA.addr1 = TEXT_BUF_BASE + (uint16_t)cur.row * TEXT_COLS + start_col;
        RIA.step1 = 1;
        for (i = 0u; i < clip_len; i++) clipboard[i] = (char)RIA.rw1;
    } else {
        RIA.addr1 = TEXT_BUF_BASE + (uint16_t)cur.row * TEXT_COLS;
        RIA.step1 = 1;
        for (i = 0u; i < TEXT_COLS; i++) clipboard[i] = (char)RIA.rw1;
        clip_len = (uint8_t)TEXT_COLS;
        while (clip_len > 0u && clipboard[clip_len - 1u] == ' ') clip_len--;
    }

    clipboard[clip_len] = 0;
    sel_active = 0u;
    draw_menu_bar("COPIED");
}

/* ================================================================
   do_cut: copies to clipboard then clears the source region.
   ================================================================ */
static void do_cut(void)
{
    uint8_t i, was_sel, saved_sel_row, cut_start, cut_end;

    was_sel       = sel_active;
    saved_sel_row = sel_row;
    if (sel_col < cur.col) { cut_start = sel_col; cut_end = cur.col; }
    else                   { cut_start = cur.col; cut_end = sel_col; }

    do_copy();

    if (was_sel && saved_sel_row == cur.row && cut_start < cut_end) {
        RIA.addr0 = TEXT_BUF_BASE + (uint16_t)cur.row * TEXT_COLS + cut_start;
        RIA.step0 = 1;
        for (i = cut_start; i < cut_end; i++) RIA.rw0 = ' ';
        cur.col = cut_start;
    } else {
        RIA.addr0 = TEXT_BUF_BASE + (uint16_t)cur.row * TEXT_COLS;
        RIA.step0 = 1;
        for (i = 0u; i < TEXT_COLS; i++) RIA.rw0 = ' ';
        cur.col = 0u;
    }
    redraw_screen();
    draw_menu_bar("Cut.");
}

/* ================================================================
   do_paste: inserts or overwrites clipboard text at cursor.
   ================================================================ */
static void do_paste(void)
{
    uint8_t  i, paste_len, save_len;
    uint16_t base;

    if (clip_len == 0u) return;

    paste_len = clip_len;
    if ((uint16_t)cur.col + paste_len > TEXT_COLS)
        paste_len = (uint8_t)(TEXT_COLS - cur.col);

    base = TEXT_BUF_BASE + (uint16_t)cur.row * TEXT_COLS + cur.col;

    if (insert_mode) {
        save_len = (uint8_t)((uint16_t)cur.col + paste_len < TEXT_COLS
                             ? TEXT_COLS - cur.col - paste_len : 0u);
        if (save_len > 0u) {
            RIA.addr1 = base;
            RIA.step1 = 1;
            for (i = 0u; i < save_len; i++) line_tmp[i] = (char)RIA.rw1;
        }
        RIA.addr0 = base;
        RIA.step0 = 1;
        for (i = 0u; i < paste_len; i++) RIA.rw0 = (uint8_t)clipboard[i];
        if (save_len > 0u) {
            RIA.addr0 = base + (uint16_t)paste_len;
            RIA.step0 = 1;
            for (i = 0u; i < save_len; i++) RIA.rw0 = (uint8_t)line_tmp[i];
        }
    } else {
        RIA.addr0 = base;
        RIA.step0 = 1;
        for (i = 0u; i < paste_len; i++) RIA.rw0 = (uint8_t)clipboard[i];
    }

    cur.col = (uint8_t)(cur.col + paste_len);
    if (cur.col >= TEXT_COLS) cur.col = (uint8_t)(TEXT_COLS - 1u);
    redraw_screen();
    draw_menu_bar(NULL);
}

/* ================================================================
   main
   ================================================================ */
int main(int argc, char **argv)
{
    uint8_t mouse_wheel, mouse_wheel_prev;
    int     mouse_wheel_change;
    bool    handled_key;
    uint8_t k, j, new_key, new_keys, last_key;
    uint8_t key_capslock, key_shifts, key_ctrl;
    uint8_t max_scroll;
    int     ok;
    char    ch;
    uint8_t repeat_key;
    clock_t repeat_start, repeat_last;
    uint8_t target_col;

    /* init state */
    cur.row             = 0u;
    cur.col             = 0u;
    current_filename[0] = 0;
    search_pattern[0]   = 0;
    content_rows        = 0u;
    scroll_row          = 0u;
    last_key            = 0u;
    handled_key         = false;
    mouse_wheel         = 0u;
    mouse_wheel_prev    = 0u;
    mouse_wheel_change  = 0;
    insert_mode         = 1u;
    sel_active          = 0u;
    sel_row             = 0u;
    sel_col             = 0u;
    clip_len            = 0u;
    clipboard[0]        = 0;
    repeat_key          = 0u;
    repeat_start        = 0;
    repeat_last         = 0;
    target_col          = 0u;

    xreg_ria_keyboard(XRAM_STRUCT_SYS_KEYBOARD);
    xreg_ria_mouse(XRAM_STRUCT_SYS_MOUSE);

    /* clear text buffer and display */
    editor_clear();
    draw_title_bar();
    draw_menu_bar(NULL);
    printf(OSC_CURSOR_COLOR "408040" OSC_ST ANSI_SHOW_CUR "\033[%d;1H", (int)(TITLE_ROWS + 1u));

    /* open file passed as argument */
    if (argc >= 1 && argv[0][0]) {
        strncpy(current_filename, argv[0], 63u);
        current_filename[63] = 0;
        ok = load_file(current_filename);
        draw_menu_bar(ok >= 0 ? current_filename : EXCLAMATION "cannot open file");
        cur.row    = 0u;
        cur.col    = 0u;
        scroll_row = 0u;
        printf("\033[%d;1H", (int)(TITLE_ROWS + 1u));
    }

    /* capture initial mouse wheel position */
    RIA.addr1 = XRAM_STRUCT_SYS_MOUSE + 3;
    RIA.step1 = 0;
    mouse_wheel      = RIA.rw1;
    mouse_wheel_prev = mouse_wheel;

    /* ---- main event loop ---- */
    while (1) {

        /* --- mouse wheel scroll --- */
        RIA.addr1 = XRAM_STRUCT_SYS_MOUSE + 3;
        RIA.step1 = 0;
        mouse_wheel        = RIA.rw1;
        mouse_wheel_change = (int)mouse_wheel_prev - (int)mouse_wheel;

        if (mouse_wheel_change == 1 || mouse_wheel_change == -1) {
            if (mouse_wheel_change < 0) {
                /* scroll up */
                if (scroll_row > 0u) {
                    scroll_row--;
                    if (cur.row >= (uint8_t)(scroll_row + EDIT_ROWS))
                        cur.row = (uint8_t)(scroll_row + EDIT_ROWS - 1u);
                    redraw_screen();
                }
            } else {
                /* scroll down */
                max_scroll = (content_rows > (uint16_t)EDIT_ROWS)
                             ? (uint8_t)(content_rows - EDIT_ROWS) : 0u;
                if (scroll_row < max_scroll) {
                    scroll_row++;
                    if (cur.row < scroll_row)
                        cur.row = scroll_row;
                    redraw_screen();
                }
            }
        }
        mouse_wheel_prev = mouse_wheel;

        /* --- scan keyboard --- */
        for (k = 0u; k < KEYBOARD_BYTES; k++) {
            RIA.addr1 = XRAM_STRUCT_SYS_KEYBOARD + k;
            RIA.step1 = 0;
            new_keys  = RIA.rw1;
            for (j = 0u; j < 8u; j++) {
                uint8_t code = (uint8_t)((k << 3) + j);
                new_key = new_keys & (uint8_t)(1u << j);
                /* rising edge: key just pressed (0→1) */
                if ((code > 3u) && new_key && !(keystates[k] & (uint8_t)(1u << j))) {
                    last_key     = code;
                    handled_key  = false;
                    repeat_key   = code;
                    repeat_start = clock();
                    repeat_last  = repeat_start;
                }
            }
            keystates[k] = new_keys;
        }

        /* --- autorepeat: fire again if key held long enough --- */
        if (repeat_key) {
            if (key(repeat_key)) {
                clock_t now = clock();
                if ((now - repeat_start) >= (clock_t)REPEAT_DELAY &&
                    (now - repeat_last)  >= (clock_t)REPEAT_RATE) {
                    last_key    = repeat_key;
                    handled_key = false;
                    repeat_last = now;
                }
            } else {
                repeat_key = 0u;
            }
        }

        key_capslock = (uint8_t)(key(KEY_CAPSLOCK)   ? 1u : 0u);
        key_shifts   = (uint8_t)((key(KEY_LEFTSHIFT) || key(KEY_RIGHTSHIFT)) ? 1u : 0u);
        key_ctrl     = (uint8_t)((key(KEY_LEFTCTRL)  || key(KEY_RIGHTCTRL))  ? 1u : 0u);

        if (!(keystates[0] & 1u)) {
            if (!handled_key) {

                /* --- Ctrl+key combos (no autorepeat) --- */
                if (key_ctrl && key(KEY_K)) {
                    repeat_key = 0u;
                    if (sel_active && sel_row == cur.row && sel_col == cur.col) {
                        sel_active = 0u;
                    } else {
                        sel_active = 1u;
                        sel_row    = cur.row;
                        sel_col    = cur.col;
                    }
                    draw_menu_bar(NULL);

                } else if (key_ctrl && key(KEY_C)) {
                    repeat_key = 0u;
                    do_copy();

                } else if (key_ctrl && key(KEY_X)) {
                    repeat_key = 0u;
                    do_cut();

                } else if (key_ctrl && key(KEY_V)) {
                    repeat_key = 0u;
                    do_paste();

                /* --- Insert / Overwrite mode toggle (no autorepeat) --- */
                } else if (key(KEY_INSERT)) {
                    repeat_key  = 0u;
                    insert_mode = insert_mode ? 0u : 1u;
                    draw_menu_bar(NULL);

                /* --- File / Search dialogs (no autorepeat) --- */
                } else if (key(KEY_F5)) {
                    if (menu_input("Open: (e.g. MSC0:/dir/file.txt)", current_filename, 64u)) {
                        ok = load_file(current_filename);
                        draw_menu_bar(ok >= 0 ? current_filename : EXCLAMATION "cannot open file");
                    } else {
                        redraw_screen();
                    }

                } else if (key(KEY_F6)) {
                    repeat_key = 0u;
                    if (menu_input("Save: (e.g. MSC0:/dir/file.txt)", current_filename, 64u)) {
                        ok = save_file(current_filename);
                        draw_title_bar();
                        draw_menu_bar(ok >= 0 ? "Saved." : EXCLAMATION "cannot save file");
                        printf("\033[%d;%dH",
                               (int)((cur.row - scroll_row) + 1u + TITLE_ROWS),
                               (int)(cur.col + 1u));
                    } else {
                        redraw_screen();
                    }

                } else if (key(KEY_F7)) {
                    repeat_key = 0u;
                    if (menu_input("Find:", search_pattern, 32u)) {
                        if (!find_text(search_pattern)) {
                            draw_menu_bar("NOT FOUND");
                        }
                    } else {
                        redraw_screen();
                    }

                /* --- Cursor movement --- */
                } else if (key(KEY_LEFT)) {
                    if (cur.col > 0u) {
                        cur.col--;
                    } else if (cur.row > 0u) {
                        cur.row--;
                        cur.col = line_text_len(cur.row);
                        if (cur.row < scroll_row) {
                            scroll_row = cur.row;
                            redraw_screen();
                        }
                    }
                    target_col = cur.col;

                } else if (key(KEY_RIGHT)) {
                    { uint8_t lim = line_text_len(cur.row);
                      if (cur.col < lim) {
                          cur.col++;
                      } else if ((uint16_t)cur.row < content_rows) {
                          cur.row++;
                          cur.col = 0u;
                          if ((uint8_t)(cur.row - scroll_row) >= EDIT_ROWS) {
                              scroll_row = (uint8_t)(cur.row - EDIT_ROWS + 1u);
                              redraw_screen();
                          }
                      }
                    }
                    target_col = cur.col;

                } else if (key(KEY_UP)) {
                    if (repeat_key == KEY_UP && last_key == KEY_UP) {
                        /* continuing vertical move — keep target_col */
                    } else {
                        target_col = cur.col;
                    }
                    if (cur.row > 0u) {
                        cur.row--;
                        { uint8_t lim = line_text_len(cur.row);
                          cur.col = (target_col <= lim) ? target_col : lim;
                        }
                        if (cur.row < scroll_row) {
                            scroll_row = cur.row;
                            redraw_screen();
                        }
                    }

                } else if (key(KEY_DOWN)) {
                    if (repeat_key == KEY_DOWN && last_key == KEY_DOWN) {
                        /* continuing vertical move — keep target_col */
                    } else {
                        target_col = cur.col;
                    }
                    if ((uint16_t)cur.row < content_rows) {
                        cur.row++;
                        { uint8_t lim = (cur.row < content_rows)
                                        ? line_text_len(cur.row) : 0u;
                          cur.col = (target_col <= lim) ? target_col : lim;
                        }
                        if ((uint8_t)(cur.row - scroll_row) >= EDIT_ROWS) {
                            scroll_row = (uint8_t)(cur.row - EDIT_ROWS + 1u);
                            redraw_screen();
                        }
                    }

                } else if (key_ctrl && key_shifts && key(KEY_HOME)) {
                    repeat_key = 0u;
                    cur.row    = 0u;
                    cur.col    = 0u;
                    scroll_row = 0u;
                    redraw_screen();

                } else if (key_ctrl && key_shifts && key(KEY_END)) {
                    repeat_key = 0u;
                    cur.row    = (content_rows > 0u) ? (uint8_t)(content_rows - 1u) : 0u;
                    cur.col    = line_text_len(cur.row);
                    if (content_rows > (uint16_t)EDIT_ROWS)
                        scroll_row = (uint8_t)(content_rows - EDIT_ROWS);
                    else
                        scroll_row = 0u;
                    redraw_screen();

                } else if (key(KEY_HOME)) {
                    repeat_key = 0u;
                    cur.col = 0u;

                } else if (key(KEY_END)) {
                    repeat_key = 0u;
                    cur.col = line_text_len(cur.row);

                } else if (key(KEY_PAGEUP)) {
                    if (cur.row >= (uint8_t)EDIT_ROWS) {
                        cur.row -= (uint8_t)EDIT_ROWS;
                    } else {
                        cur.row = 0u;
                    }
                    if (cur.row < scroll_row) {
                        scroll_row = cur.row;
                        redraw_screen();
                    }

                } else if (key(KEY_PAGEDOWN)) {
                    if ((uint16_t)cur.row + (uint16_t)EDIT_ROWS < (uint16_t)content_rows) {
                        cur.row += (uint8_t)EDIT_ROWS;
                    } else {
                        cur.row = (content_rows > 0u) ? (uint8_t)(content_rows - 1u) : 0u;
                    }
                    if ((uint8_t)(cur.row - scroll_row) >= EDIT_ROWS) {
                        scroll_row = (uint8_t)(cur.row - EDIT_ROWS + 1u);
                        redraw_screen();
                    }

                /* --- Enter: split line at cursor --- */
                } else if (key(KEY_ENTER) || key(KEY_KPENTER)) {
                    do_enter();

                /* --- Backspace --- */
                } else if (key(KEY_BACKSPACE)) {
                    if (cur.col > 0u) {
                        cur.col--;
                        if (insert_mode) {
                            line_shift_left(cur.row, (uint8_t)(cur.col + 1u));
                        } else {
                            RIA.addr0 = TEXT_BUF_BASE + (uint16_t)cur.row * TEXT_COLS + cur.col;
                            RIA.step0 = 0;
                            RIA.rw0   = ' ';
                        }
                        /* redraw current line */
                        {
                            uint8_t j2;
                            RIA.addr1 = TEXT_BUF_BASE + (uint16_t)cur.row * TEXT_COLS;
                            RIA.step1 = 1;
                            for (j2 = 0u; j2 < TEXT_COLS; j2++) {
                                char c = (char)RIA.rw1;
                                g_linebuf[j2] = c ? c : ' ';
                            }
                            printf("\033[%d;1H",
                                   (int)((cur.row - scroll_row) + 1u + TITLE_ROWS));
                            for (j2 = 0u; j2 < TEXT_COLS; j2++)
                                putchar((uint8_t)g_linebuf[j2]);
                        }
                    } else if (cur.row > 0u) {
                        /* at col 0: join this row onto end of previous row */
                        do_backspace_join();
                    }

                /* --- ESC: exit --- */
                } else if (key(KEY_ESC)) {
                    // xreg_vga_canvas(GFX_CANVAS_640x480);   /* restore default console mode */
                    // xreg(1, 0, 1, 0);
                    break;

                /* --- Generic character input --- */
                } else {
                    ch = keycode_to_char(last_key, (uint8_t)(key_shifts && !key_ctrl),
                                         key_capslock);
                    if (ch) {
                        if (insert_mode && cur.col < (uint8_t)(TEXT_COLS - 1u)) {
                            line_shift_right(cur.row, cur.col);
                        }
                        RIA.addr0 = TEXT_BUF_BASE + (uint16_t)cur.row * TEXT_COLS + cur.col;
                        RIA.step0 = 0;
                        RIA.rw0   = (uint8_t)ch;

                        /* print char at terminal position */
                        printf("\033[%d;%dH%c",
                               (int)((cur.row - scroll_row) + 1u + TITLE_ROWS),
                               (int)(cur.col + 1u),
                               ch);

                        if (cur.col < (uint8_t)(TEXT_COLS - 1u)) {
                            cur.col++;

                            /* redraw rest of line (insert mode shifts chars) */
                            if (insert_mode) {
                                uint8_t j2;
                                RIA.addr1 = TEXT_BUF_BASE + (uint16_t)cur.row * TEXT_COLS + cur.col;
                                RIA.step1 = 1;
                                for (j2 = cur.col; j2 < TEXT_COLS; j2++) {
                                    char c = (char)RIA.rw1;
                                    putchar(c ? c : ' ');
                                }
                            }

                            /* extend content tracking */
                            if ((uint16_t)(cur.row + 1u) > content_rows)
                                content_rows = (uint16_t)(cur.row + 1u);

                        } else if (cur.row < 255u) {
                            /* last column reached — wrap to next row */
                            cur.row++;
                            cur.col = 0u;
                            if ((uint16_t)(cur.row + 1u) > content_rows)
                                content_rows = (uint16_t)(cur.row + 1u);
                            if ((uint8_t)(cur.row - scroll_row) >= EDIT_ROWS) {
                                scroll_row = (uint8_t)(cur.row - EDIT_ROWS + 1u);
                                redraw_screen();
                            }
                        }
                    }
                }

                /* position cursor after any key action */
                printf("\033[%d;%dH",
                       (int)((cur.row - scroll_row) + 1u + TITLE_ROWS),
                       (int)(cur.col + 1u));
                handled_key = true;
            }
        } else {
            handled_key = false;
        }
    }

    xreg_ria_keyboard(0xFFFF);
    xreg_ria_mouse(0xFFFF);
    printf(CSI_RESET CSI_CURSOR_HOME CSI_CURSOR_SHOW);
    return 0;

}
