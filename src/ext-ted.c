#include <rp6502.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <fcntl.h>
#include "commons.h"

#define APPVER "20260508.0806"
#define APPNAME "TEd"
#define APP_MSG_TITLE CSI "1;1H" CSI HIGHLIGHT_COLOR " razemOS > " ANSI_RESET " " APPNAME ANSI_DARK_GRAY CSI "1;60Hversion " APPVER ANSI_RESET

/* autorepeat: clock() ticks are centiseconds (1 tick = 10 ms) */
#define REPEAT_DELAY      40u   /* 400 ms before first repeat */
#define REPEAT_RATE        5u   /* 50 ms between repeats (default) */
#define REPEAT_RATE_FAST   2u   /* 20 ms between repeats (arrow keys without Shift) */

/* --- XRAM register addresses --- */
#define XRAM_STRUCT_SYS_KEYBOARD 0xFF20
#define XRAM_STRUCT_SYS_MOUSE    0xFF40

/* --- GFX canvas --- */
//#define GFX_CANVAS_640x480       0b00000011

/* --- XRAM text buffer: 256 rows x 80 cols x 1 byte = 20480 bytes --- */
#define TEXT_BUF_BASE    0x0000u
#define TEXT_COLS        80u
#define XRAM_SCRATCH     0xA200u   /* scratch for file write (82 bytes) */

/* --- XRAM clipboard: up to 32 whole lines stored after text buffer --- */
#define CLIP_BUF_BASE    0x5100u
#define CLIP_MAX_LINES   32u

/* --- Terminal dimensions (640x480, 16px font) --- */
#define TERM_ROWS        30u
#define TITLE_ROWS       2u    /* fixed title bar at top */
#define MENU_ROWS        2u    /* fixed menu at bottom */
#define EDIT_ROWS        (TERM_ROWS - TITLE_ROWS - MENU_ROWS) /* editable area */

/* --- ANSI escape helpers --- */
#define ANSI_HOME        "\033[H"
#define ANSI_HIDE_CUR    "\033[?25l"
#define ANSI_SHOW_CUR    "\033[?25h"
#define ANSI_REVERSE     "\033[0;7m"
#define ANSI_NORMAL      "\033[0m"
#define ANSI_SEL_BG      "\x1b[48;2;60;60;60m"
#define ANSI_SEL_BG_QA   "\x1b[48;2;220;0;0m"
#define ANSI_SEL_BG_OFF  "\x1b[49m"

/* --- filenames --- */
#define NEW_FILENAME APPNAME "-NewDocument.txt"

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
static uint8_t view_mode   = 0u;   /* 1 = read-only view, editing disabled */
static uint8_t doc_dirty   = 0u;   /* 1 when document has unsaved changes */
static uint8_t insert_mode = 1u;
static uint8_t sel_active  = 0u;
static uint8_t sel_row     = 0u;
static uint8_t sel_col     = 0u;   /* anchor column for char-level selection */
#define SEL_MODE_LINE 0u
#define SEL_MODE_CHAR 1u
static uint8_t sel_mode    = SEL_MODE_LINE;
static uint8_t clip_lines   = 0u;   /* number of whole rows in XRAM clipboard */
static uint8_t clip_is_char = 0u;   /* 1 = clipboard holds char fragment, not whole rows */
static char    line_tmp[80];

static uint8_t sel_min_row(void);
static uint8_t sel_max_row(void);

/* ------------------------------------------------------------------ */

static void flush_rx()
{
    int i;
    while (RX_READY) i = RIA.rx;
}

/* ================================================================
   keycode_to_char: USB HID keycode -> ASCII
   ================================================================ */
static char keycode_to_char(uint8_t code, uint8_t shift, uint8_t caps, uint8_t ralt)
{
    uint8_t upper;
    static const char sh_digits[9] = {'!','@','#','$','%','^','&','*','('};

    /* Right Alt + letter → Polish CP852 chars */
    if (ralt) {
        switch (code) {
            case KEY_A: return shift ? (char)0xA4u : (char)0xA5u; /* Ą / ą */
            case KEY_C: return shift ? (char)0x8Fu : (char)0x86u; /* Ć / ć */
            case KEY_E: return shift ? (char)0xA8u : (char)0xA9u; /* Ę / ę */
            case KEY_L: return shift ? (char)0x9Du : (char)0x88u; /* Ł / ł */
            case KEY_N: return shift ? (char)0xE3u : (char)0xE4u; /* Ń / ń */
            case KEY_O: return shift ? (char)0xE0u : (char)0xA2u; /* Ó / ó */
            case KEY_S: return shift ? (char)0x97u : (char)0x98u; /* Ś / ś */
            case KEY_X: return shift ? (char)0x8Du : (char)0xABu; /* Ź / ź */
            case KEY_Z: return shift ? (char)0xBDu : (char)0xBEu; /* Ż / ż */
            default:    return 0;
        }
    }

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
    if (code == KEY_LEFTBRACE)  return shift ? '{' : '[';
    if (code == KEY_RIGHTBRACE) return shift ? '}' : ']';
    if (code == KEY_BACKSLASH)  return shift ? '|' : '\\';
    if (code == KEY_GRAVE)      return shift ? '~' : '`';
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

static void menu_print_text(uint8_t ansi_row, uint8_t ansi_col, const char *text)
{
    uint8_t i;
    printf("\033[%d;%dH", (int)ansi_row, (int)ansi_col);
    for (i = 0u; text[i] && i < TEXT_COLS; i++) putchar((uint8_t)text[i]);
    printf(ANSI_RESET);
}

/* ================================================================
   window_draw: draws a box with semigraphic border and filled background.
   x_pos, y_pos: 1-based terminal column/row of the top-left corner.
   width, height: outer dimensions including the border.
   color_fg / color_bg: ANSI SGR strings (e.g. "37m" / "44m"), or NULL.
   window_text: prints text inside the window at (wx_pos, wy_pos) offset
   from the inner top-left corner (1-based, clipped to inner area).
   ================================================================ */
/* XRAM backup buffer for window_draw: stores chars under the window */
#define XRAM_WIN_BUF  0xA300u   /* max window area: 80*30 worst case, but we only need win_w*win_h */

static uint8_t win_x;   /* saved top-left column of last window_draw call */
static uint8_t win_y;   /* saved top-left row    of last window_draw call */
static uint8_t win_w;   /* saved outer width */
static uint8_t win_h;   /* saved outer height */

static void window_open(uint8_t x_pos, uint8_t y_pos,
                        uint8_t width, uint8_t height,
                        const char *color_fg, const char *color_bg, uint8_t frame)
{
    uint8_t r, c, inner_w;

    win_x = x_pos;
    win_y = y_pos;
    win_w = width;
    win_h = height;

    if (width < 2u || height < 2u) return;
    inner_w = (uint8_t)(width - 2u);

    /* save chars under window area to XRAM backup buffer */
    for (r = 0u; r < height; r++) {
        printf("\033[%d;%dH", (int)(y_pos + r), (int)x_pos);
        RIA.addr0 = XRAM_WIN_BUF + (uint16_t)r * width;
        RIA.step0 = 1;
        /* read back from XRAM text buffer (terminal content mirrors text buf for edit rows) */
        {
            uint16_t xram_row = (uint16_t)((y_pos + r) - 1u);   /* 0-based terminal row */
            uint8_t  xram_col = (uint8_t)(x_pos - 1u);          /* 0-based terminal col */
            if (xram_row >= TITLE_ROWS && xram_row < (uint16_t)(TITLE_ROWS + EDIT_ROWS)) {
                /* edit area: back up from text buffer */
                uint8_t buf_row = (uint8_t)((xram_row - TITLE_ROWS) + scroll_row);
                RIA.addr1 = TEXT_BUF_BASE + (uint16_t)buf_row * TEXT_COLS + xram_col;
                RIA.step1 = 1;
                for (c = 0u; c < width; c++) RIA.rw0 = RIA.rw1;
            } else {
                /* title/menu area: no text buffer — store placeholder spaces */
                for (c = 0u; c < width; c++) RIA.rw0 = ' ';
            }
        }
    }

    /* draw window */
    for (r = 0u; r < height; r++) {
        printf("\033[%d;%dH", (int)(y_pos + r), (int)x_pos);
        if (color_bg) { printf(color_bg); }
        if (color_fg) { printf(color_fg); }
        if (r == 0u) {
            putchar(frame == 0 ? ' ' : '\xDA');
            for (c = 0u; c < inner_w; c++) putchar(frame == 0 ? ' ' : '\xC4');
            putchar(frame == 0 ? ' ' : '\xBF');
        } else if (r == (uint8_t)(height - 1u)) {
            putchar(frame == 0 ? ' ' : '\xC0');
            for (c = 0u; c < inner_w; c++) putchar(frame == 0 ? ' ' : '\xC4');
            putchar(frame == 0 ? ' ' : '\xD9');
        } else {
            putchar(frame == 0 ? ' ' : '\xB3');
            for (c = 0u; c < inner_w; c++) putchar(' ');
            putchar(frame == 0 ? ' ' : '\xB3');
        }
    }
    printf(ANSI_RESET CSI_CURSOR_HIDE);
}

static void window_close(void)
{
    uint8_t r, c;
    if (win_w < 2u || win_h < 2u) return;
    for (r = 0u; r < win_h; r++) {
        printf("\033[%d;%dH" ANSI_NORMAL, (int)(win_y + r), (int)win_x);
        RIA.addr1 = XRAM_WIN_BUF + (uint16_t)r * win_w;
        RIA.step1 = 1;
        for (c = 0u; c < win_w; c++) {
            uint8_t ch = RIA.rw1;
            putchar(ch ? ch : ' ');
        }
    }
    printf(ANSI_RESET CSI_CURSOR_HIDE);
}

static void window_text(const char *text,
                        uint8_t wx_pos, uint8_t wy_pos,
                        const char *color_fg, const char *color_bg){

    uint8_t col, max_len, i;

    if (win_w < 2u || win_h < 2u) return;
    /* clamp to inner area */
    if (wx_pos < 1u) wx_pos = 1u;
    if (wy_pos < 1u) wy_pos = 1u;
    if (wx_pos > (uint8_t)(win_w - 2u)) return;
    if (wy_pos > (uint8_t)(win_h - 2u)) return;

    if (color_bg) { printf(color_bg); }
    if (color_fg) { printf(color_fg); }
    col     = (uint8_t)(win_x + wx_pos);   /* 1-based terminal column */
    max_len = (uint8_t)(win_w - 1u - wx_pos);
    
    printf("\033[%d;%dH", (int)(win_y + wy_pos), (int)col);
    for (i = 0u; text[i] && i < max_len; i++) putchar((uint8_t)text[i]);
    printf(ANSI_RESET);
}

/* ================================================================
   draw_title_bar: 2-row title bar at top of terminal (rows 1-3).
   Row 1: empty
   Row 2: program name, version
   Row 3: just semigraphical horizontal line
   ================================================================ */
static void draw_title_bar(void)
{
    static const char menu_line1[] = APP_MSG_TITLE;
    uint8_t i;

    for (i = 0u; menu_line1[i]; i++) putchar((uint8_t)menu_line1[i]);

    printf(CSI "2;1H");
    for (i = 0u; i < 80u; i++) putchar('\xc4');    
    printf(ANSI_NORMAL);

}

/* ================================================================
   draw_menu_bar: 4-row status bar at bottom of terminal.
   Row EDIT_ROWS+1: just semigraphical horizontal line
   Row EDIT_ROWS+2: key shortcuts
   Row EDIT_ROWS+3: [INS]/[OVR] + status/filename
   Row EDIT_ROWS+4: MARK SET indicator
   ================================================================ */

#define INFO_READY "Ready"
#define CHAR_VBAR "\xb3"
#define CHAR_HBAR "\xc4"
#define MODE_INS  "[INS]"
#define MODE_OVR  "[OVR]"
#define MODE_VIEW "[VIEW]"
#define CLIPBOARD_WITHDATA "[CLIP]"

static void draw_menu_bar(const char *status)
{
    uint8_t i, s, fn_len, line_len;
    const char *mode_str;
    const char *clip_str;
    const char *info;
    char row2[81];
    
    info = status ? status : INFO_READY;
    mode_str = view_mode ? MODE_VIEW : (insert_mode ? MODE_INS : MODE_OVR);
    clip_str = (clip_is_char > 0u || clip_lines > 0u) ? CLIPBOARD_WITHDATA : "";
    
    i = 0u;
    for (; info[0] && (i < 80u); i++, info++) row2[i] = *info;
    row2[i++] = ' ';
    for (s = 0u; mode_str[s]; s++, i++) row2[i] = mode_str[s];
    row2[i++] = ' ';
    for (s = 0u; clip_str[s]; s++, i++) row2[i] = clip_str[s];
    row2[i++] = ' ';
    row2[i] = 0;
    menu_print_row(TITLE_ROWS + EDIT_ROWS + 2u, row2);

    printf("\033[%d;1H", TITLE_ROWS + EDIT_ROWS + 1u);
    if (current_filename[0]) {
        for (fn_len = 0u; current_filename[fn_len]; fn_len++) {}
        line_len = (fn_len + 2u < 79u) ? (uint8_t)(80u - fn_len - 2u) : 1u;
        for (i = 0u; i < line_len; i++) putchar('\xc4');
        putchar('\xb4');
        putchar(doc_dirty ? '!' : ' ');
        for (i = 0u; i < fn_len; i++) putchar((uint8_t)current_filename[i]);
    } else {
        for (i = 0u; i < 80u; i++) putchar('\xc4');
    }

    printf(ANSI_NORMAL);
    
    /* unused but do not touch this

    putchar('\xb4');
    putchar(insert_mode ? '\x1d' : '\x19');
    putchar(sel_active  ? '\x1f' : '\xfe');

    menu_print_row(TITLE_ROWS + EDIT_ROWS + 2u,
        "[Ctrl+O] open [Ctrl+S] save [Ctrl+F] find [Ctrl+Q] exit");
    */

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
        uint8_t in_sel, in_char_sel;
        xrow = (uint16_t)scroll_row + r;
        in_char_sel = sel_active && sel_mode == SEL_MODE_CHAR
                      && (uint8_t)xrow == sel_row && (uint8_t)xrow == cur.row
                      && xrow < content_rows;
        in_sel = sel_active && sel_mode == SEL_MODE_LINE
                 && (uint8_t)xrow >= sel_min_row()
                 && (uint8_t)xrow <= sel_max_row()
                 && xrow < content_rows;
        printf("\033[%d;1H", (int)(r + 1u + TITLE_ROWS));
        if (in_sel)          printf(ANSI_SEL_BG);
        else if (!in_char_sel) printf(ANSI_SEL_BG_OFF);
        if (xrow < content_rows) {
            RIA.addr1 = TEXT_BUF_BASE + xrow * TEXT_COLS;
            RIA.step1 = 1;
            for (j = 0u; j < TEXT_COLS; j++) {
                char c = (char)RIA.rw1;
                g_linebuf[j] = c ? c : ' ';
            }
            if (in_char_sel) {
                uint8_t c_from = (sel_col < cur.col) ? sel_col : cur.col;
                uint8_t c_to   = (sel_col > cur.col) ? sel_col : cur.col;
                for (j = 0u; j < TEXT_COLS; j++) {
                    if (j >= c_from && j < c_to) printf(ANSI_SEL_BG);
                    else                          printf(ANSI_SEL_BG_OFF);
                    putchar((uint8_t)g_linebuf[j]);
                }
                printf(ANSI_SEL_BG_OFF);
            } else {
                for (j = 0u; j < TEXT_COLS; j++) putchar((uint8_t)g_linebuf[j]);
                if (in_sel) printf(ANSI_SEL_BG_OFF);
            }
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
   menu_confirm: Y/N prompt in bottom menu row. Returns 1=Y, 0=N/Esc.
   ================================================================ */
static int menu_confirm(const char *prompt)
{
    uint8_t prev_ks[KEYBOARD_BYTES];
    uint8_t cur_ks[KEYBOARD_BYTES];
    uint8_t k, j, code, was, now;
    int     done = 0, result = 0;
    uint8_t input_row = TITLE_ROWS + EDIT_ROWS + 2u;

    for (k = 0u; k < KEYBOARD_BYTES; k++) prev_ks[k] = keystates[k];

    printf("\033[%d;1H" ANSI_SEL_BG_QA, (int)input_row);
    printf("%s", prompt);
    printf(ANSI_SEL_BG_OFF ANSI_HIDE_CUR);

    while (!done) {
        for (k = 0u; k < KEYBOARD_BYTES; k++) {
            RIA.addr1 = XRAM_STRUCT_SYS_KEYBOARD + k;
            RIA.step1 = 0;
            cur_ks[k] = RIA.rw1;
        }
        for (k = 0u; k < KEYBOARD_BYTES && !done; k++) {
            for (j = 0u; j < 8u && !done; j++) {
                was = (prev_ks[k] >> j) & 1u;
                now = (cur_ks [k] >> j) & 1u;
                if (!was && now) {
                    code = (uint8_t)((k << 3) | j);
                    if (code == KEY_Y) { result = 1; done = 1; }
                    else if (code == KEY_N || code == KEY_ESC) { result = 0; done = 1; }
                }
            }
            prev_ks[k] = cur_ks[k];
        }
    }

    for (k = 0u; k < KEYBOARD_BYTES; k++) keystates[k] = cur_ks[k];
    return result;
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
    uint8_t input_row = TITLE_ROWS + EDIT_ROWS + 2u;
    uint8_t plen;   /* prompt length in columns */
    uint8_t field;  /* width of input field = 80 - plen */
    uint8_t rep_key;        /* key held for autorepeat (0 = none) */
    clock_t rep_start;      /* clock() when key was first pressed */
    clock_t rep_last;       /* clock() of last repeat fire */

    for (plen = 0u; prompt[plen]; plen++) {}
    field = (plen < 79u) ? (uint8_t)(80u - plen) : 1u;

    /* measure pre-filled content so caller can seed the buffer */
    for (len = 0u; buf[len] && len < (uint8_t)(maxlen - 1u); len++) {}
    pos      = len;   /* cursor at end of pre-filled text */
    done     = 0;
    result   = 0;
    rep_key  = 0u;
    rep_start = 0;
    rep_last  = 0;

    for (k = 0u; k < KEYBOARD_BYTES; k++) prev_ks[k] = keystates[k];

    /* draw prompt + input field in one row */
    printf("\033[%d;1H", (int)input_row);
    for (i = 0u; i < plen; i++) putchar((uint8_t)prompt[i]);
    printf(ANSI_SEL_BG);
    for (i = 0u; buf[i] && i < field; i++) putchar((uint8_t)buf[i]);
    for (; i < field; i++) putchar(' ');
    printf(ANSI_SEL_BG_OFF ANSI_SHOW_CUR "\033[%d;%dH",
           (int)input_row, (int)(plen + pos + 1u));

/* helper macro: execute action for key code 'c' and reposition cursor */
#define MI_ACTION(c) do { \
    uint8_t _fire = 0u; \
    if ((c) == KEY_LEFT)      { if (pos > 0u) { pos--; _fire=1u; } } \
    else if ((c) == KEY_RIGHT){ if (pos < len) { pos++; _fire=1u; } } \
    else if ((c) == KEY_BACKSPACE) { \
        if (pos > 0u) { \
            for (i = pos - 1u; i < len - 1u; i++) buf[i] = buf[i + 1u]; \
            len--; pos--; buf[len] = 0; _fire=1u; \
            printf("\033[%d;%dH" ANSI_SEL_BG, (int)input_row, (int)(plen + 1u)); \
            for (i = 0u; buf[i] && i < field; i++) putchar((uint8_t)buf[i]); \
            for (; i < field; i++) putchar(' '); \
            printf(ANSI_SEL_BG_OFF); \
        } \
    } else if ((c) == KEY_DELETE) { \
        if (pos < len) { \
            for (i = pos; i < len - 1u; i++) buf[i] = buf[i + 1u]; \
            len--; buf[len] = 0; _fire=1u; \
            printf("\033[%d;%dH" ANSI_SEL_BG, (int)input_row, (int)(plen + 1u)); \
            for (i = 0u; buf[i] && i < field; i++) putchar((uint8_t)buf[i]); \
            for (; i < field; i++) putchar(' '); \
            printf(ANSI_SEL_BG_OFF); \
        } \
    } else { \
        ch = keycode_to_char((c), shift, caps, 0u); \
        if (ch && len < (uint8_t)(maxlen - 1u) && len < field) { \
            for (i = len; i > pos; i--) buf[i] = buf[i - 1u]; \
            buf[pos++] = ch; len++; buf[len] = 0; _fire=1u; \
            printf("\033[%d;%dH" ANSI_SEL_BG, (int)input_row, (int)(plen + 1u)); \
            for (i = 0u; buf[i] && i < field; i++) putchar((uint8_t)buf[i]); \
            for (; i < field; i++) putchar(' '); \
            printf(ANSI_SEL_BG_OFF); \
        } \
    } \
    if (_fire) printf("\033[%d;%dH", (int)input_row, (int)(plen + pos + 1u)); \
} while(0)

    while (!done) {
        for (k = 0u; k < KEYBOARD_BYTES; k++) {
            RIA.addr1 = XRAM_STRUCT_SYS_KEYBOARD + k;
            RIA.step1 = 0;
            cur_ks[k] = RIA.rw1;
        }

        shift = (uint8_t)(((cur_ks[KEY_LEFTSHIFT  >> 3] >> (KEY_LEFTSHIFT  & 7)) & 1u) |
                           ((cur_ks[KEY_RIGHTSHIFT >> 3] >> (KEY_RIGHTSHIFT & 7)) & 1u));
        caps  = (uint8_t)( (cur_ks[KEY_CAPSLOCK   >> 3] >> (KEY_CAPSLOCK   & 7)) & 1u);

        /* autorepeat fire */
        if (rep_key) {
            uint8_t still = (cur_ks[rep_key >> 3] >> (rep_key & 7)) & 1u;
            if (still) {
                clock_t now_t = clock();
                clock_t rate  = (rep_key == KEY_LEFT || rep_key == KEY_RIGHT)
                                ? REPEAT_RATE_FAST : REPEAT_RATE;
                if ((now_t - rep_start) >= REPEAT_DELAY &&
                    (now_t - rep_last)  >= rate) {
                    rep_last = now_t;
                    MI_ACTION(rep_key);
                }
            } else {
                rep_key = 0u;
            }
        }

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
                    } else if (code == KEY_HOME) {
                        pos = 0u;
                        printf("\033[%d;%dH", (int)input_row, (int)(plen + pos + 1u));
                    } else if (code == KEY_END) {
                        pos = len;
                        printf("\033[%d;%dH", (int)input_row, (int)(plen + pos + 1u));
                    } else {
                        MI_ACTION(code);
                        rep_key   = code;
                        rep_start = clock();
                        rep_last  = rep_start;
                    }
                }
            }
            prev_ks[k] = cur_ks[k];
        }
    }
#undef MI_ACTION

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
    doc_dirty    = 0u;
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
    if (fd < 0) {
        fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC);
        if (fd < 0) return -1;
        close(fd);
        editor_clear();
        strncpy(current_filename, filename, 63u);
        current_filename[63] = 0;
        content_rows = 0u;
        redraw_screen();
        return 0;
    }

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
   do_delete_join: Delete at end of a row — join next row onto current.
   Mirror of do_backspace_join: next row's text is appended to current
   row starting at cur_len, then next row is removed by shifting up.
   Cursor stays at cur.col. If combined length > TEXT_COLS, do nothing.
   ================================================================ */
static void do_delete_join(void)
{
    uint8_t cur_len, next_len, j, write_col;
    char    next_line[80];

    if (cur.row >= content_rows) return;

    cur_len  = line_text_len(cur.row);
    next_len = line_text_len((uint8_t)(cur.row + 1u));

    if ((uint16_t)cur_len + next_len > (uint16_t)TEXT_COLS) return;

    if (next_len > 0u) {
        RIA.addr1 = TEXT_BUF_BASE + (uint16_t)(cur.row + 1u) * TEXT_COLS;
        RIA.step1 = 1;
        for (j = 0u; j < next_len; j++) next_line[j] = (char)RIA.rw1;
    }

    write_col = cur_len;
    for (j = 0u; j < next_len; j++, write_col++) {
        RIA.addr0 = TEXT_BUF_BASE + (uint16_t)cur.row * TEXT_COLS + write_col;
        RIA.step0 = 0;
        RIA.rw0   = (uint8_t)next_line[j];
    }

    rows_shift_up((uint8_t)(cur.row + 1u));

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
   sel_min_row / sel_max_row: first and last selected row (anchor..cursor).
   ================================================================ */
static uint8_t sel_min_row(void) { return sel_row < cur.row ? sel_row : cur.row; }
static uint8_t sel_max_row(void) { return sel_row > cur.row ? sel_row : cur.row; }

/* ================================================================
   do_copy: copies selected rows (or current row) into XRAM clipboard.
   Stores whole lines; clip_lines = number of lines saved.
   ================================================================ */
static void do_copy(void)
{
    uint8_t r, j, from, to;

    if (sel_active && sel_mode == SEL_MODE_CHAR) {
        uint8_t c_from = (sel_col < cur.col) ? sel_col : cur.col;
        uint8_t c_to   = (sel_col > cur.col) ? sel_col : cur.col;
        uint8_t len    = (uint8_t)(c_to - c_from);
        if (len == 0u) { sel_active = 0u; return; }
        clip_lines   = 1u;
        clip_is_char = 1u;
        RIA.addr0 = CLIP_BUF_BASE; RIA.step0 = 1;
        for (j = 0u; j < TEXT_COLS; j++) RIA.rw0 = ' ';
        RIA.addr1 = TEXT_BUF_BASE + (uint16_t)cur.row * TEXT_COLS + c_from;
        RIA.step1 = 1;
        for (j = 0u; j < len; j++) {
            RIA.addr0 = CLIP_BUF_BASE + j; RIA.step0 = 0;
            RIA.rw0   = RIA.rw1;
        }
        sel_active = 0u;
        redraw_screen();
        draw_menu_bar("Copied");
        return;
    }

    clip_is_char = 0u;
    from = sel_active ? sel_min_row() : cur.row;
    to   = sel_active ? sel_max_row() : cur.row;
    if (to >= (uint8_t)content_rows) to = (uint8_t)(content_rows > 0u ? content_rows - 1u : 0u);

    clip_lines = (uint8_t)(to - from + 1u);
    if (clip_lines > CLIP_MAX_LINES) clip_lines = (uint8_t)CLIP_MAX_LINES;

    for (r = 0u; r < clip_lines; r++) {
        RIA.addr1 = TEXT_BUF_BASE + (uint16_t)(from + r) * TEXT_COLS;
        RIA.step1 = 1;
        RIA.addr0 = CLIP_BUF_BASE + (uint16_t)r * TEXT_COLS;
        RIA.step0 = 0;
        for (j = 0u; j < TEXT_COLS; j++) {
            RIA.addr0 = CLIP_BUF_BASE + (uint16_t)r * TEXT_COLS + j;
            RIA.rw0   = RIA.rw1;
        }
    }

    sel_active = 0u;
    redraw_screen();
    draw_menu_bar("Copied");
}

/* ================================================================
   do_cut: copies selected rows to clipboard, then removes them.
   ================================================================ */
static void do_cut(void)
{
    uint8_t from, to, n, i;

    if (sel_active && sel_mode == SEL_MODE_CHAR) {
        uint8_t c_from = (sel_col < cur.col) ? sel_col : cur.col;
        uint8_t c_to   = (sel_col > cur.col) ? sel_col : cur.col;
        uint8_t len    = (uint8_t)(c_to - c_from);
        uint8_t j;
        if (len == 0u) { sel_active = 0u; return; }
        do_copy();   /* clears sel_active */
        for (j = 0u; j < len; j++) line_shift_left(cur.row, (uint8_t)(c_from + 1u));
        cur.col = c_from;
        redraw_screen();
        draw_menu_bar("Cut");
        return;
    }

    from = sel_active ? sel_min_row() : cur.row;
    to   = sel_active ? sel_max_row() : cur.row;
    if (to >= (uint8_t)content_rows) to = (uint8_t)(content_rows > 0u ? content_rows - 1u : 0u);
    n    = (uint8_t)(to - from + 1u);

    do_copy();   /* saves sel_active; clears it after */

    /* shift rows up n times starting from 'from' */
    for (i = 0u; i < n; i++) rows_shift_up(from);

    cur.row = from;
    if (cur.row >= (uint8_t)content_rows && content_rows > 0u)
        cur.row = (uint8_t)(content_rows - 1u);
    cur.col = 0u;
    if (cur.row < scroll_row) scroll_row = cur.row;

    redraw_screen();
    draw_menu_bar("Cut");
}

/* ================================================================
   do_paste: inserts clipboard rows before cur.row.
   ================================================================ */
static void do_paste(void)
{
    uint8_t r, j, start_row;

    if (clip_lines == 0u) return;

    if (clip_is_char) {
        uint8_t len, cur_len;
        len = 0u;
        for (j = 0u; j < TEXT_COLS; j++) {
            RIA.addr1 = CLIP_BUF_BASE + j; RIA.step1 = 0;
            if ((char)RIA.rw1 != ' ') len = (uint8_t)(j + 1u);
        }
        cur_len = line_text_len(cur.row);
        for (j = 0u; j < len; j++) {
            if ((uint16_t)cur_len + 1u <= (uint16_t)TEXT_COLS) {
                line_shift_right(cur.row, cur.col);
                { uint8_t ch;
                  RIA.addr1 = CLIP_BUF_BASE + j; RIA.step1 = 0;
                  ch = RIA.rw1;
                  RIA.addr0 = TEXT_BUF_BASE + (uint16_t)cur.row * TEXT_COLS + cur.col;
                  RIA.step0 = 0;
                  RIA.rw0   = ch;
                }
                cur.col++;
                cur_len++;
            }
        }
        if ((uint16_t)(cur.row + 1u) > content_rows)
            content_rows = (uint16_t)(cur.row + 1u);
        redraw_screen();
        draw_menu_bar("Pasted");
        return;
    }

    start_row = cur.row;
    for (r = 0u; r < clip_lines; r++) {
        if (content_rows >= 255u) break;
        rows_shift_down((uint8_t)(start_row + r));
        content_rows++;
        RIA.addr0 = TEXT_BUF_BASE + (uint16_t)(start_row + r) * TEXT_COLS;
        RIA.step0 = 1;
        for (j = 0u; j < TEXT_COLS; j++) {
            RIA.addr1 = CLIP_BUF_BASE + (uint16_t)r * TEXT_COLS + j;
            RIA.step1 = 0;
            RIA.rw0   = RIA.rw1;
        }
    }

    cur.row = start_row;
    cur.col = 0u;
    redraw_screen();
    draw_menu_bar("Pasted");
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
    uint8_t key_capslock, key_shifts, key_ctrl, key_ralt, key_lalt;
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
    sel_mode            = SEL_MODE_LINE;
    clip_lines          = 0u;
    clip_is_char        = 0u;
    repeat_key          = 0u;
    repeat_start        = 0;
    repeat_last         = 0;
    target_col          = 0u;

    xreg_ria_keyboard(XRAM_STRUCT_SYS_KEYBOARD);
    xreg_ria_mouse(XRAM_STRUCT_SYS_MOUSE);
    printf(CSI_ECHO_OFF);

    /* parse arguments: [/view] [filename] */
    {
        int fi = 0;   /* index of filename argument */
        if (argc >= 1 && strcmp(argv[0], "/view") == 0) {
            view_mode = 1u;
            fi = 1;
        }
        strncpy(current_filename, (fi < argc && argv[fi][0]) ? argv[fi] : NEW_FILENAME, 63u);
    }
    current_filename[63] = 0;
    ok = load_file(current_filename);   /* calls editor_clear() + redraw_screen() internally */
    draw_title_bar();
    draw_menu_bar(ok > 0 ? (view_mode ? "View" : "Ready") : ok == 0 ? "FILE CREATED" : EXCLAMATION "cannot open file");
    cur.row    = 0u;
    cur.col    = 0u;
    scroll_row = 0u;
    printf(OSC_CURSOR_COLOR "408040" OSC_ST ANSI_SHOW_CUR "\033[%d;1H", (int)(TITLE_ROWS + 1u));

    window_open((80u-26u)/2u, (30u-16u)/2u, 26, 16, CSI "37m", CSI "48;2;40;80;40m", 0);
    window_text(APPNAME, 2, 1, CSI "37m", CSI "48;2;40;80;40m");
    window_text("Text Editor", 2, 3, CSI "37m", CSI "48;2;40;80;40m");
    window_text("for razemOS", 2, 4, CSI "37m", CSI "48;2;40;80;40m");
    window_text("(c) 2026 by WojciechGw", 2, 14, CSI "37m", CSI "48;2;40;80;40m");
    PAUSE(300);
    window_close();
    redraw_screen();
    draw_title_bar();
    draw_menu_bar(ok > 0 ? "Ready" : ok == 0 ? "FILE CREATED" : EXCLAMATION "cannot open file");
    printf(OSC_CURSOR_COLOR "408040" OSC_ST ANSI_SHOW_CUR "\033[%d;1H", (int)(TITLE_ROWS + 1u));

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
                clock_t now  = clock();
                uint8_t rate = REPEAT_RATE;
                if (!key_shifts &&
                    (repeat_key == KEY_LEFT || repeat_key == KEY_RIGHT ||
                     repeat_key == KEY_UP   || repeat_key == KEY_DOWN))
                    rate = REPEAT_RATE_FAST;
                if ((now - repeat_start) >= (clock_t)REPEAT_DELAY &&
                    (now - repeat_last)  >= (clock_t)rate) {
                    last_key    = repeat_key;
                    handled_key = false;
                    repeat_last = now;
                }
            } else {
                repeat_key = 0u;
            }
        }

        key_capslock = (uint8_t)(key(KEY_CAPSLOCK)   ? 1u : 0u);
        key_shifts   = (uint8_t)((!view_mode && (key(KEY_LEFTSHIFT) || key(KEY_RIGHTSHIFT))) ? 1u : 0u);
        key_ctrl     = (uint8_t)((key(KEY_LEFTCTRL)  || key(KEY_RIGHTCTRL))  ? 1u : 0u);
        key_lalt     = (uint8_t)( key(KEY_LEFTALT)                           ? 1u : 0u);
        key_ralt     = (uint8_t)( key(KEY_RIGHTALT)                          ? 1u : 0u);

        if (!(keystates[0] & 1u)) {
            if (!handled_key) {

                /* --- Ctrl+key combos (no autorepeat) --- */
                if (key_ctrl && !key_shifts && key(KEY_C)) {
                    repeat_key = 0u;
                    if (!view_mode) do_copy();

                } else if (key_ctrl && key(KEY_X)) {
                    repeat_key = 0u;
                    if (!view_mode) { do_cut(); doc_dirty = 1u; }

                } else if (key_ctrl && key(KEY_V)) {
                    repeat_key = 0u;
                    if (!view_mode) { do_paste(); doc_dirty = 1u; }

                /* --- Insert / Overwrite mode toggle (no autorepeat) --- */
                } else if (key(KEY_INSERT)) {
                    repeat_key  = 0u;
                    insert_mode = insert_mode ? 0u : 1u;
                    draw_menu_bar(NULL);

                } else if (key_ctrl && key(KEY_N)) {
                    repeat_key = 0u;
                    if (!view_mode) {
                        if (doc_dirty) {
                            if (menu_confirm(" Save changes before new document? [Y/N] ")) {
                                if (menu_input("SAVE path/filename : ", current_filename, 64u)) {
                                    ok = save_file(current_filename);
                                    if (ok >= 0) doc_dirty = 0u;
                                    draw_menu_bar(ok >= 0 ? "FILE SAVED" : EXCLAMATION "cannot save file");
                                    printf(ANSI_SHOW_CUR "\033[%d;%dH",
                                           (int)((cur.row - scroll_row) + 1u + TITLE_ROWS),
                                           (int)(cur.col + 1u));
                                }
                            }
                        }
                        strncpy(current_filename, NEW_FILENAME, 63u);
                        current_filename[63] = '\0';
                        editor_clear();
                        draw_title_bar();
                        draw_menu_bar("New document");
                        printf(ANSI_SHOW_CUR "\033[%d;1H", (int)(1u + TITLE_ROWS));
                    }

                /* --- File / Search dialogs (no autorepeat) --- */
                } else if (key_ctrl && key(KEY_O)) {
                    repeat_key = 0u;
                    if (doc_dirty) {
                        if (menu_confirm(" Save changes before opening? [Y/N] ")) {
                            uint8_t ask_o = (strcmp(current_filename, NEW_FILENAME) == 0);
                            if (!ask_o || menu_input("SAVE path/filename : ", current_filename, 64u)) {
                                ok = save_file(current_filename);
                                if (ok >= 0) doc_dirty = 0u;
                                draw_menu_bar(ok >= 0 ? "FILE SAVED" : EXCLAMATION "cannot save file");
                                printf(ANSI_SHOW_CUR "\033[%d;%dH",
                                       (int)((cur.row - scroll_row) + 1u + TITLE_ROWS),
                                       (int)(cur.col + 1u));
                            }
                        }
                    }
                    if (menu_input("OPEN path/filename : ", current_filename, 64u)) {
                        ok = load_file(current_filename);
                        draw_menu_bar(ok > 0 ? "Ready" : ok == 0 ? "FILE CREATED" : EXCLAMATION "cannot open file");
                    } else {
                        redraw_screen();
                    }

                } else if (key_ctrl && key(KEY_S)) {
                    repeat_key = 0u;
                    if (!view_mode) {
                        uint8_t ask = key_shifts ||
                                      (strcmp(current_filename, NEW_FILENAME) == 0);
                        if (!ask ||
                            menu_input((key_shifts ? "SAVE AS path/filename : " : "SAVE path/filename : "), current_filename, 64u)) {
                            ok = save_file(current_filename);
                            if (ok >= 0) doc_dirty = 0u;
                            draw_title_bar();
                            draw_menu_bar(ok >= 0 ? "FILE SAVED" : EXCLAMATION "cannot save file");
                            printf(ANSI_SHOW_CUR "\033[%d;%dH",
                                   (int)((cur.row - scroll_row) + 1u + TITLE_ROWS),
                                   (int)(cur.col + 1u));
                            PAUSE(300);
                        } else {
                            redraw_screen();
                        }
                    }

                } else if (key_ctrl && key(KEY_F)) {
                    repeat_key = 0u;
                    if (menu_input("FIND pattern : ", search_pattern, 32u)) {
                        if (!find_text(search_pattern)) {
                            draw_menu_bar("TEXT NOT FOUND");
                        }
                    } else {
                        redraw_screen();
                    }

                /* --- Cursor movement --- */
                } else if (key(KEY_LEFT)) {
                    if (key_shifts) {
                        if (!sel_active) { sel_active = 1u; sel_row = cur.row; sel_col = cur.col; }
                        sel_mode = SEL_MODE_CHAR;
                        if (cur.col > 0u) {
                            cur.col--;
                        } else if (cur.row > 0u) {
                            cur.row--;
                            cur.col = line_text_len(cur.row);
                            if (cur.row != sel_row) sel_active = 0u;
                            if (cur.row < scroll_row) scroll_row = cur.row;
                        }
                        target_col = cur.col;
                        redraw_screen();
                    } else {
                        if (sel_active) { sel_active = 0u; redraw_screen(); }
                        if (cur.col > 0u) {
                            cur.col--;
                        } else if (cur.row > 0u) {
                            cur.row--;
                            cur.col = line_text_len(cur.row);
                            if (cur.row < scroll_row) { scroll_row = cur.row; redraw_screen(); }
                        }
                        target_col = cur.col;
                    }

                } else if (key(KEY_RIGHT)) {
                    if (key_shifts) {
                        if (!sel_active) { sel_active = 1u; sel_row = cur.row; sel_col = cur.col; }
                        sel_mode = SEL_MODE_CHAR;
                        { uint8_t lim = line_text_len(cur.row);
                          if (lim > TEXT_COLS - 1u) lim = TEXT_COLS - 1u;
                          if (cur.col < lim) {
                              cur.col++;
                          } else if ((uint16_t)cur.row < content_rows) {
                              cur.row++;
                              cur.col = 0u;
                              if (cur.row != sel_row) sel_active = 0u;
                              if ((uint8_t)(cur.row - scroll_row) >= EDIT_ROWS)
                                  scroll_row = (uint8_t)(cur.row - EDIT_ROWS + 1u);
                          }
                        }
                        target_col = cur.col;
                        redraw_screen();
                    } else {
                        if (sel_active) { sel_active = 0u; redraw_screen(); }
                        { uint8_t lim = line_text_len(cur.row);
                          if (lim > TEXT_COLS - 1u) lim = TEXT_COLS - 1u;
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
                    }

                } else if (key(KEY_UP)) {
                    if (key_shifts) {
                        if (!sel_active) { sel_active = 1u; sel_row = cur.row; }
                        sel_mode = SEL_MODE_LINE;
                        if (repeat_key == KEY_UP && last_key == KEY_UP) {
                        } else { target_col = cur.col; }
                        if (cur.row > 0u) {
                            cur.row--;
                            { uint8_t lim = line_text_len(cur.row);
                              cur.col = (target_col <= lim) ? target_col : lim;
                            }
                            if (cur.row < scroll_row) scroll_row = cur.row;
                        }
                        redraw_screen();
                    } else {
                        if (sel_active) { sel_active = 0u; redraw_screen(); }
                        if (repeat_key == KEY_UP && last_key == KEY_UP) {
                        } else { target_col = cur.col; }
                        if (cur.row > 0u) {
                            cur.row--;
                            { uint8_t lim = line_text_len(cur.row);
                              cur.col = (target_col <= lim) ? target_col : lim;
                            }
                            if (cur.row < scroll_row) { scroll_row = cur.row; redraw_screen(); }
                        }
                    }

                } else if (key(KEY_DOWN)) {
                    if (key_shifts) {
                        if (!sel_active) { sel_active = 1u; sel_row = cur.row; }
                        sel_mode = SEL_MODE_LINE;
                        if (repeat_key == KEY_DOWN && last_key == KEY_DOWN) {
                        } else { target_col = cur.col; }
                        if ((uint16_t)cur.row < content_rows) {
                            cur.row++;
                            { uint8_t lim = (cur.row < content_rows)
                                            ? line_text_len(cur.row) : 0u;
                              cur.col = (target_col <= lim) ? target_col : lim;
                            }
                            if ((uint8_t)(cur.row - scroll_row) >= EDIT_ROWS)
                                scroll_row = (uint8_t)(cur.row - EDIT_ROWS + 1u);
                        }
                        redraw_screen();
                    } else {
                        if (sel_active) { sel_active = 0u; redraw_screen(); }
                        if (repeat_key == KEY_DOWN && last_key == KEY_DOWN) {
                        } else { target_col = cur.col; }
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
                    if (!view_mode) { do_enter(); doc_dirty = 1u; }

                /* --- Backspace --- */
                } else if (key(KEY_BACKSPACE)) {
                    if (view_mode) { /* no-op */ } else
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
                    doc_dirty = 1u;

                /* --- Delete --- */
                } else if (key(KEY_DELETE)) {
                    if (!view_mode) {
                        uint8_t cur_text_len = line_text_len(cur.row);
                        if (cur.col < cur_text_len) {
                            /* delete char at cursor: shift row left from col+1 */
                            line_shift_left(cur.row, (uint8_t)(cur.col + 1u));
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
                        } else if (cur.row < content_rows) {
                            /* at end of line: join next row onto current */
                            do_delete_join();
                        }
                        doc_dirty = 1u;
                    } /* !view_mode */

                /* --- Ctrl+Q: exit --- */
                } else if (key_ctrl && key(KEY_Q)) {
                    repeat_key = 0u;
                    if (doc_dirty) {
                        if (menu_confirm(" Save changes before exit? [Y/N] ")) {
                            uint8_t ask2 = (strcmp(current_filename, NEW_FILENAME) == 0);
                            if (!ask2 ||
                                menu_input("SAVE path/filename : ", current_filename, 64u)) {
                                ok = save_file(current_filename);
                                if (ok >= 0) doc_dirty = 0u;
                            }
                        }
                    }
                    flush_rx();

                    break;

                /* --- F4: toggle view/edit mode --- */
                } else if (key(KEY_F4)) {
                    repeat_key = 0u;
                    view_mode = view_mode ? 0u : 1u;
                    if (view_mode && sel_active) { sel_active = 0u; redraw_screen(); }
                    draw_menu_bar(view_mode ? "View" : "Edit");

                /* --- Ctrl+Shift+T: insert current date and time (YYYY-MM-DD HH:MM) --- */
                } else if (key_ctrl && key_shifts && key(KEY_T)) {
                    repeat_key = 0u;
                    if (!view_mode) {
                        time_t dt_t;
                        struct tm *dt_tm;
                        char dt_buf[17];
                        uint8_t di;
                        if (time(&dt_t) != (time_t)-1 && (dt_tm = localtime(&dt_t)) != NULL) {
                            sprintf(dt_buf, "%04d-%02d-%02d %02d:%02d",
                                    dt_tm->tm_year + 1900, dt_tm->tm_mon + 1,
                                    dt_tm->tm_mday, dt_tm->tm_hour, dt_tm->tm_min);
                            for (di = 0u; dt_buf[di]; di++) {
                                uint8_t dch = (uint8_t)dt_buf[di];
                                if (insert_mode && cur.col < (uint8_t)(TEXT_COLS - 1u))
                                    line_shift_right(cur.row, cur.col);
                                RIA.addr0 = TEXT_BUF_BASE + (uint16_t)cur.row * TEXT_COLS + cur.col;
                                RIA.step0 = 0;
                                RIA.rw0   = dch;
                                printf("\033[%d;%dH%c",
                                       (int)((cur.row - scroll_row) + 1u + TITLE_ROWS),
                                       (int)(cur.col + 1u), (char)dch);
                                if (cur.col < (uint8_t)(TEXT_COLS - 1u)) {
                                    cur.col++;
                                    if ((uint16_t)(cur.row + 1u) > content_rows)
                                        content_rows = (uint16_t)(cur.row + 1u);
                                }
                            }
                            doc_dirty = 1u;
                            if (insert_mode) redraw_screen();
                        }
                    }

                /* --- Ctrl+Shift+C/R/L: center / right-align / strip leading spaces --- */
                } else if (key_ctrl && key_shifts && key(KEY_C)) {
                    repeat_key = 0u;
                    if (!view_mode) {
                        uint8_t tlen, pad, i;
                        tlen = line_text_len(cur.row);
                        if (tlen > 0u && tlen <= TEXT_COLS) {
                            RIA.addr1 = TEXT_BUF_BASE + (uint16_t)cur.row * TEXT_COLS;
                            RIA.step1 = 1;
                            for (i = 0u; i < TEXT_COLS; i++) line_tmp[i] = (char)RIA.rw1;
                            /* strip leading spaces into line_tmp[0..tlen-1] */
                            { uint8_t s = 0u;
                              while (s < tlen && line_tmp[s] == ' ') s++;
                              tlen = (uint8_t)(tlen - s);
                              if (tlen > 0u && s > 0u) {
                                  for (i = 0u; i < tlen; i++) line_tmp[i] = line_tmp[i + s];
                              }
                            }
                            pad = (uint8_t)((TEXT_COLS - tlen) / 2u);
                            RIA.addr0 = TEXT_BUF_BASE + (uint16_t)cur.row * TEXT_COLS;
                            RIA.step0 = 1;
                            for (i = 0u; i < pad; i++)   RIA.rw0 = ' ';
                            for (i = 0u; i < tlen; i++)  RIA.rw0 = (uint8_t)line_tmp[i];
                            for (i = (uint8_t)(pad + tlen); i < TEXT_COLS; i++) RIA.rw0 = ' ';
                            doc_dirty = 1u;
                            redraw_screen();
                        }
                    }

                } else if (key_ctrl && key_shifts && key(KEY_R)) {
                    repeat_key = 0u;
                    if (!view_mode) {
                        uint8_t tlen, pad, i;
                        tlen = line_text_len(cur.row);
                        if (tlen > 0u && tlen <= TEXT_COLS) {
                            RIA.addr1 = TEXT_BUF_BASE + (uint16_t)cur.row * TEXT_COLS;
                            RIA.step1 = 1;
                            for (i = 0u; i < TEXT_COLS; i++) line_tmp[i] = (char)RIA.rw1;
                            /* strip leading spaces */
                            { uint8_t s = 0u;
                              while (s < tlen && line_tmp[s] == ' ') s++;
                              tlen = (uint8_t)(tlen - s);
                              if (tlen > 0u && s > 0u) {
                                  for (i = 0u; i < tlen; i++) line_tmp[i] = line_tmp[i + s];
                              }
                            }
                            pad = (uint8_t)(TEXT_COLS - tlen);
                            RIA.addr0 = TEXT_BUF_BASE + (uint16_t)cur.row * TEXT_COLS;
                            RIA.step0 = 1;
                            for (i = 0u; i < pad; i++)  RIA.rw0 = ' ';
                            for (i = 0u; i < tlen; i++) RIA.rw0 = (uint8_t)line_tmp[i];
                            doc_dirty = 1u;
                            redraw_screen();
                        }
                    }

                } else if (key_ctrl && key_shifts && key(KEY_L)) {
                    repeat_key = 0u;
                    if (!view_mode) {
                        uint8_t tlen, s, i;
                        tlen = line_text_len(cur.row);
                        if (tlen > 0u) {
                            RIA.addr1 = TEXT_BUF_BASE + (uint16_t)cur.row * TEXT_COLS;
                            RIA.step1 = 1;
                            for (i = 0u; i < TEXT_COLS; i++) line_tmp[i] = (char)RIA.rw1;
                            s = 0u;
                            while (s < tlen && line_tmp[s] == ' ') s++;
                            if (s > 0u) {
                                tlen = (uint8_t)(tlen - s);
                                RIA.addr0 = TEXT_BUF_BASE + (uint16_t)cur.row * TEXT_COLS;
                                RIA.step0 = 1;
                                for (i = 0u; i < tlen; i++) RIA.rw0 = (uint8_t)line_tmp[i + s];
                                for (i = tlen; i < TEXT_COLS; i++) RIA.rw0 = ' ';
                                if (cur.col < s) cur.col = 0u;
                                else cur.col = (uint8_t)(cur.col - s);
                                doc_dirty = 1u;
                                redraw_screen();
                            }
                        }
                    }

                /* --- Ctrl+Shift+Alt+A: ASCII character table popup (17x17, hex labels) --- */
                } else if (key_ctrl && key_shifts && key_lalt && key(KEY_A)) {
                    repeat_key = 0u;
                    {
                        /* inner area: 3 + 16*3 = 51 cols, 1 header + 16 rows = 17 rows
                           outer (with frame): 53 wide, 19 tall
                           centered on 80x30: x=(80-53)/2+1=14, y=(30-19)/2+1=6 */
                        static const char hex[16] = {'0','1','2','3','4','5','6','7',
                                                     '8','9','A','B','C','D','E','F'};
                        char rowbuf[55];
                        uint8_t r, c;

                        window_open(29u, 6u, 22u, 19u, CSI "37m", CSI "48;2;60;60;60m", 0);

                        /* header row: "   " then " 0  1  2 ... F" */
                        rowbuf[0]=' '; rowbuf[1]=' ';
                        for (c = 0u; c < 16u; c++) {
                            rowbuf[2u + c] = hex[c];
                        }
                        rowbuf[18] = '\0';
                        window_text(rowbuf, 2u, 1u, CSI "37m", CSI "48;2;60;60;60m");

                        /* 16 data rows */
                        for (r = 0u; r < 16u; r++) {
                            /* row label: "X " */
                            rowbuf[0u] = hex[r];
                            rowbuf[1u] = ' ';
                            for (c = 0u; c < 16u; c++) {
                                uint8_t code = (uint8_t)(r * 16u + c);
                                // char ch = (code >= 0x20u && code != 0x7Fu) ? (char)code : '.';
                                char ch = (code >= 0x0Eu && code != 0x18u && code != 0x1Bu) ? (char)code : '.';
                                rowbuf[1u + c + 1u] = ch;
                            }
                            rowbuf[18] = '\0';
                            window_text(rowbuf, 2u, (uint8_t)(r + 2u), CSI "37m", CSI "48;2;60;60;60m");
                        }

                        /* wait for any new key-down (edge detect, ignores currently held keys) */
                        {
                            uint8_t prev_ks[KEYBOARD_BYTES];
                            uint8_t cur_ks[KEYBOARD_BYTES];
                            uint8_t ki, ji, was, now2;
                            int     got_key = 0;
                            for (ki = 0u; ki < KEYBOARD_BYTES; ki++) prev_ks[ki] = keystates[ki];
                            while (!got_key) {
                                for (ki = 0u; ki < KEYBOARD_BYTES; ki++) {
                                    RIA.addr1 = XRAM_STRUCT_SYS_KEYBOARD + ki;
                                    RIA.step1 = 0;
                                    cur_ks[ki] = RIA.rw1;
                                }
                                for (ki = 0u; ki < KEYBOARD_BYTES && !got_key; ki++) {
                                    for (ji = 0u; ji < 8u && !got_key; ji++) {
                                        was  = (prev_ks[ki] >> ji) & 1u;
                                        now2 = (cur_ks [ki] >> ji) & 1u;
                                        if (!was && now2) got_key = 1;
                                    }
                                    prev_ks[ki] = cur_ks[ki];
                                }
                            }
                            for (ki = 0u; ki < KEYBOARD_BYTES; ki++) keystates[ki] = cur_ks[ki];
                        }
                        window_close();
                        redraw_screen();
                    }

                /* --- Alt+P: insert paragraph sign § (0x15) --- */
                } else if (key_lalt && key(KEY_P)) {
                    repeat_key = 0u;
                    if (!view_mode) {
                        uint8_t dch = 0x15u;
                        if (insert_mode && cur.col < (uint8_t)(TEXT_COLS - 1u))
                            line_shift_right(cur.row, cur.col);
                        RIA.addr0 = TEXT_BUF_BASE + (uint16_t)cur.row * TEXT_COLS + cur.col;
                        RIA.step0 = 0;
                        RIA.rw0   = dch;
                        printf("\033[%d;%dH%c",
                               (int)((cur.row - scroll_row) + 1u + TITLE_ROWS),
                               (int)(cur.col + 1u), (char)dch);
                        if (cur.col < (uint8_t)(TEXT_COLS - 1u)) {
                            cur.col++;
                            if ((uint16_t)(cur.row + 1u) > content_rows)
                                content_rows = (uint16_t)(cur.row + 1u);
                        }
                        doc_dirty = 1u;
                        if (insert_mode) redraw_screen();
                    }

                /* --- Generic character input --- */
                } else {
                    ch = keycode_to_char(last_key, (uint8_t)(key_shifts && !key_ctrl),
                                         key_capslock, key_ralt);
                    if (ch && !view_mode) {
                        if (insert_mode && cur.col < (uint8_t)(TEXT_COLS - 1u)) {
                            line_shift_right(cur.row, cur.col);
                        }
                        RIA.addr0 = TEXT_BUF_BASE + (uint16_t)cur.row * TEXT_COLS + cur.col;
                        RIA.step0 = 0;
                        RIA.rw0   = (uint8_t)ch;
                        doc_dirty = 1u;

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
                // show current cursor position
                {
                    char pos_buf[17];
                    sprintf(pos_buf, "row:%03d col:%03d", (int)(cur.row + 1), (int)(cur.col + 1));
                    printf(CSI "s");
                    draw_menu_bar(NULL);
                    menu_print_text(TITLE_ROWS + EDIT_ROWS + 2u, 80u - 14u, pos_buf);
                    printf(CSI "u");
                }

            }
        } else {
            handled_key = false;
        }
    }

    xreg_ria_keyboard(0xFFFF);
    xreg_ria_mouse(0xFFFF);
    flush_rx();

    xreg_vga_canvas(3);
    xreg(1, 0, 1, 0);
    printf(CSI_CLS CSI_ECHO_ON CSI_CURSOR_SHOW CSI_CURSOR_HOME);
    return 0;

}
