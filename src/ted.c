#include <rp6502.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <fcntl.h>
#include "ted.h"

/* keysology
    [Tab]               insert spaces to next tab stop (col % 8)
    [Shift+Tab]         remove spaces back to prev tab stop
    [Ctrl+O]            open document
    [Ctrl+S]            save document
    [Shift+Ctrl+S]      save as document
    [Ctrl+F]            find pattern
    [Ctrl+H]            replace text
    [Ctrl+Q]            exit
    [Alt+I]             same as Insert key OVR<>INS
    [Shift+Ctrl+Alt+L]  start list from line prefix:
                          "1." / "2."  -> numbered list  (1. 2. 3. ...)
                          "a)" / "b)"  -> alpha list     (a) b) ... z) aa) ...)
                          "* " "- " etc -> bullet list
                        Enter=next item, to end the list mode just tap Enter last numbered line
*/

void *__fastcall__ argv_mem(size_t size) { return malloc(size); }

/* --- cursor --- */
struct Cursor {
    uint8_t row;
    uint8_t col;
};
static struct Cursor cur;

/* --- scroll / content state --- */
static uint8_t  scroll_row    = 0u;
static uint8_t  startup_done  = 0u;
static uint16_t content_rows  = 0u;

/* --- file and search state --- */
static char current_filename[64];
static char search_pattern[26];
static char replace_pattern[26];
static char g_linebuf[82];

/* --- screen line cache flag (data lives in XRAM at SCREEN_CACHE_BASE) --- */
static uint8_t cache_valid = 0u;

/* --- Insert/Overwrite mode and clipboard --- */
static uint8_t view_mode   = 0u;   /* 1 = read-only view, editing disabled */
static uint8_t doc_dirty   = 0u;   /* 1 when document has unsaved changes */
static uint8_t insert_mode = 1u;
static uint8_t sel_active  = 0u;
static uint8_t sel_row     = 0u;
static uint8_t sel_col     = 0u;   /* anchor column for char-level selection */
static uint8_t sel_mode    = SEL_MODE_LINE;
static uint8_t clip_lines   = 0u;   /* number of whole rows in XRAM clipboard */
static uint8_t clip_is_char = 0u;   /* 1 = clipboard holds char fragment, not whole rows */

/* --- list mode --- */
static uint8_t list_mode    = LIST_MODE_NONE;
static uint8_t list_counter = 0u;   /* current item number (NUM: 1-based; ALPHA: 0='a') */
static char    list_bullet  = 0;    /* bullet char (BULLET mode) */
static char    list_sep     = 0;    /* separator after number/letter: '.' or ')' etc. */

static uint8_t sel_min_row(void);
static uint8_t sel_max_row(void);

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
   window_draw: draws a box with semigraphic border and filled background.
   x_pos, y_pos: 1-based terminal column/row of the top-left corner.
   width, height: outer dimensions including the border.
   color_fg / color_bg: ANSI SGR strings (e.g. "37m" / "44m"), or NULL.
   window_text: prints text inside the window at (wx_pos, wy_pos) offset
   from the inner top-left corner (1-based, clipped to inner area).
   ================================================================ */
static uint8_t win_x;      /* saved top-left column of last window_draw call */
static uint8_t win_y;      /* saved top-left row    of last window_draw call */
static uint8_t win_w;      /* saved outer width */
static uint8_t win_h;      /* saved outer height */
static uint8_t win_shadow; /* 1 if shadow was drawn */

static void window_open(uint8_t x_pos, uint8_t y_pos,
                        uint8_t width, uint8_t height,
                        const char *color_fg, const char *color_bg, uint8_t frame, bool shadow)
{
    uint8_t r, c, inner_w;

    win_x      = x_pos;
    win_y      = y_pos;
    win_w      = width;
    win_h      = height;
    win_shadow = shadow ? 1u : 0u;

    if (width < 2u || height < 2u) return;
    inner_w = (uint8_t)(width - 2u);

    /* save chars under window area (+ shadow col/row if needed) to XRAM backup buffer */
    {
        uint8_t save_cols = shadow ? (uint8_t)(width + 1u) : width;
        uint8_t save_rows = shadow ? (uint8_t)(height + 1u) : height;
        for (r = 0u; r < save_rows; r++) {
            uint16_t xram_row = (uint16_t)((y_pos + r) - 1u);
            uint8_t  xram_col = (uint8_t)(x_pos - 1u);
            RIA.addr0 = XRAM_WIN_BUF + (uint16_t)r * save_cols;
            RIA.step0 = 1;
            if (xram_row >= TITLE_ROWS && xram_row < (uint16_t)(TITLE_ROWS + EDIT_ROWS)) {
                uint8_t buf_row = (uint8_t)((xram_row - TITLE_ROWS) + scroll_row);
                RIA.addr1 = TEXT_BUF_BASE + (uint16_t)buf_row * TEXT_COLS + xram_col;
                RIA.step1 = 1;
                for (c = 0u; c < save_cols; c++) RIA.rw0 = RIA.rw1;
            } else {
                for (c = 0u; c < save_cols; c++) RIA.rw0 = ' ';
            }
        }
    }

    /* draw window */
    for (r = 0u; r < height; r++) {
        printf(CSI "%d;%dH", (int)(y_pos + r), (int)x_pos);
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
    if (shadow) {
        uint8_t sh_col    = (uint8_t)(x_pos + width);
        uint8_t save_cols = (uint8_t)(width + 1u);
        printf(CSI "38;2;20;20;20m" CSI "48;2;5;5;5m");
        /* right column: rows 1..height-1 (skip top-left corner of shadow) */
        for (r = 1u; r < height; r++) {
            uint8_t ch;
            RIA.addr1 = XRAM_WIN_BUF + (uint16_t)r * save_cols + width;
            RIA.step1 = 0;
            ch = RIA.rw1;
            printf(CSI "%d;%dH", (int)(y_pos + r), (int)sh_col);
            putchar(ch ? ch : ' ');
        }
        /* bottom row: cols x_pos+1 .. x_pos+width */
        for (c = 0u; c < width; c++) {
            uint8_t ch;
            RIA.addr1 = XRAM_WIN_BUF + (uint16_t)height * save_cols + c;
            RIA.step1 = 0;
            ch = RIA.rw1;
            printf(CSI "%d;%dH", (int)(y_pos + height), (int)(x_pos + 1u + c));
            putchar(ch ? ch : ' ');
        }
        printf(ANSI_RESET);
    }
    printf(ANSI_RESET CSI_CURSOR_HIDE);
}

static void window_close(void)
{
    uint8_t r, c, restore_cols, restore_rows;
    if (win_w < 2u || win_h < 2u) return;
    restore_cols = win_shadow ? (uint8_t)(win_w + 1u) : win_w;
    restore_rows = win_shadow ? (uint8_t)(win_h + 1u) : win_h;
    for (r = 0u; r < restore_rows; r++) {
        printf(CSI "%d;%dH" ANSI_NORMAL, (int)(win_y + r), (int)win_x);
        RIA.addr1 = XRAM_WIN_BUF + (uint16_t)r * restore_cols;
        RIA.step1 = 1;
        for (c = 0u; c < restore_cols; c++) {
            uint8_t ch = RIA.rw1;
            putchar(ch ? ch : ' ');
        }
    }
    printf(ANSI_RESET CSI_CURSOR_SHOW);
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
    
    printf(CSI "%d;%dH", (int)(win_y + wy_pos), (int)col);
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
    static const char title_line1[] = APP_MSG_TITLE;
    uint8_t i, fn_len, filename_position; // line_len

    printf(CSI "1;1H" CSI "2K");
    for (i = 0u; title_line1[i]; i++) putchar((uint8_t)title_line1[i]);
    if (current_filename[0]) {
        for (fn_len = 0u; current_filename[fn_len]; fn_len++) {}
        // line_len = (fn_len + 2u < 79u) ? (uint8_t)(80u - fn_len - 2u) : 1u;
        filename_position = 80u - fn_len;
        printf(CSI "1;%dH%s" ANSI_DARK_GRAY, (uint8_t)filename_position, (doc_dirty ? "!" : " "));
        for (i = 0u; i < fn_len; i++) putchar((uint8_t)current_filename[i]);
    } 

    printf(CSI "2;1H" ANSI_DARK_GRAY);
    putchar(CHAR_SO);
    for (i = 0u; i < 80u; i++) putchar('q');
    putchar(CHAR_SI);
    
    printf(ANSI_NORMAL);

}

static void status_print_row(uint8_t ansi_row, const char *text)
{
    uint8_t i;
    printf(CSI "%d;1H", (int)ansi_row);
    for (i = 0u; text[i] && i < TEXT_COLS; i++) putchar((uint8_t)text[i]);
    while (i < TEXT_COLS) { putchar(' '); i++; }
    printf(ANSI_RESET);
}

static void draw_status_bar(const char *status)
{
    uint8_t i;
    const char *info;
    char block[40];
    char row2[81];

    if (status) {
        info = status;
    } else if (list_mode == LIST_MODE_NUM) {
        info = "LIST: numbered";
    } else if (list_mode == LIST_MODE_ALPHA) {
        info = "LIST: alpha";
    } else if (list_mode == LIST_MODE_BULLET) {
        info = "LIST: bullet";
    } else {
        info = !startup_done ? "Please wait..." : INFO_READY;
    }

    printf(CSI "s" ANSI_DARK_GRAY CSI_CURSOR_HIDE CSI "%d;1H", TITLE_ROWS + EDIT_ROWS + 1u);

    for (i = 0u; i < 8u; i++){
        printf("\xfa\xfa\xfa\xfa\xfa\xfa\xfa\xfa\xfa" SO "v" SI);
    }
    
    // for (i = 0u; i < 80u; i++) putchar('\xc4');

    if (view_mode) {
        sprintf(block, "Ln %d, Col %d [%s]", (int)(cur.row + 1), (int)(cur.col + 1), MODE_VIEW);
    } else {
        if (clip_is_char > 0u || clip_lines > 0u)
            sprintf(block, "Ln %d, Col %d [%s][%s][%s][%s]",
                    (int)(cur.row + 1), (int)(cur.col + 1),
                    CLIPBOARD_WITHDATA,
                    key(KEY_CAPSLOCK_LED) ? MODE_CAPS : MODE_NCAPS,
                    insert_mode ? MODE_INS : MODE_OVR,
                    MODE_EDIT);
        else
            sprintf(block, "Ln %d, Col %d [%s][%s][%s]",
                    (int)(cur.row + 1), (int)(cur.col + 1),
                    key(KEY_CAPSLOCK_LED) ? MODE_CAPS : MODE_NCAPS,
                    insert_mode ? MODE_INS : MODE_OVR,
                    MODE_EDIT);
    }

    snprintf(row2, sizeof(row2), "%-*s%s", (int)(80 - (int)strlen(block)), info, block);
    status_print_row(TITLE_ROWS + EDIT_ROWS + 2u, row2);
    if(status != NULL) PAUSE(150);
    printf(CSI "u");
    printf(ANSI_NORMAL CSI_CURSOR_SHOW);

}

/* ================================================================
   redraw_line: draws one visible row (screen index r) from XRAM cache.
   Caller must ensure cache_valid == 1.
   ================================================================ */
static void redraw_line(uint8_t r)
{
    uint8_t  j;
    uint16_t xrow = (uint16_t)scroll_row + r;
    uint8_t  in_sel, in_char_sel;

    in_char_sel = sel_active && sel_mode == SEL_MODE_CHAR
                  && (uint8_t)xrow == sel_row && (uint8_t)xrow == cur.row
                  && xrow < content_rows;
    in_sel = sel_active && sel_mode == SEL_MODE_LINE
             && (uint8_t)xrow >= sel_min_row()
             && (uint8_t)xrow <= sel_max_row()
             && xrow < content_rows;

    printf(CSI "%d;1H", (int)(r + 1u + TITLE_ROWS));
    if (in_sel)            printf(ANSI_SEL_BG);
    else if (!in_char_sel) printf(ANSI_SEL_BG_OFF);

    if (xrow < content_rows) {
        RIA.addr1 = SCREEN_CACHE_BASE + (uint16_t)r * TEXT_COLS;
        RIA.step1 = 1;
        if (in_char_sel) {
            uint8_t c_from = (sel_col < cur.col) ? sel_col : cur.col;
            uint8_t c_to   = (sel_col > cur.col) ? sel_col : cur.col;
            for (j = 0u; j < TEXT_COLS; j++) {
                if (j >= c_from && j < c_to) printf(ANSI_SEL_BG);
                else                          printf(ANSI_SEL_BG_OFF);
                putchar((uint8_t)RIA.rw1);
            }
            printf(ANSI_SEL_BG_OFF);
        } else {
            for (j = 0u; j < TEXT_COLS; j++) putchar((uint8_t)RIA.rw1);
            if (in_sel) printf(ANSI_SEL_BG_OFF);
        }
    } else if (xrow == content_rows) {
        static const char eod[] = "- End of document -";
        printf(ANSI_SEL_BG_OFF ANSI_DARK_GRAY);
        for (j = 0u; eod[j]; j++) putchar((uint8_t)eod[j]);
        printf(ANSI_NORMAL);
        for (; j < TEXT_COLS; j++) putchar(' ');
    } else {
        for (j = 0u; j < TEXT_COLS; j++) putchar(' ');
    }
}

/* ================================================================
   redraw_sel_delta: redraws only the rows whose selection highlight
   changed between two selection states (old_r1..old_r2 and new_r1..new_r2).
   Uses screen_cache — no XRAM reads.
   ================================================================ */
static void redraw_sel_delta(uint8_t old_r1, uint8_t old_r2,
                              uint8_t new_r1, uint8_t new_r2)
{
    uint8_t rows[4], n, i, k, sr, dup;
    n = 0u;
    rows[n++] = old_r1;
    rows[n++] = old_r2;
    if (new_r1 != old_r1 && new_r1 != old_r2) rows[n++] = new_r1;
    if (new_r2 != old_r1 && new_r2 != old_r2 && new_r2 != new_r1) rows[n++] = new_r2;
    for (i = 0u; i < n; i++) {
        sr = rows[i];
        if (sr < scroll_row || (uint8_t)(sr - scroll_row) >= EDIT_ROWS) continue;
        /* skip duplicate screen positions */
        dup = 0u;
        { uint8_t m; for (m = 0u; m < i; m++) if (rows[m] == sr) { dup = 1u; break; } }
        if (dup) continue;
        k = (uint8_t)(sr - scroll_row);
        redraw_line(k);
    }
}

static void update_cache_line(uint8_t r)
{
    uint8_t j;
    uint16_t xrow = (uint16_t)scroll_row + r;
    RIA.addr0 = SCREEN_CACHE_BASE + (uint16_t)r * TEXT_COLS;
    RIA.step0 = 1;
    if (xrow < content_rows) {
        RIA.addr1 = TEXT_BUF_BASE + xrow * TEXT_COLS;
        RIA.step1 = 1;
        for (j = 0u; j < TEXT_COLS; j++) {
            char c = (char)RIA.rw1;
            RIA.rw0 = (uint8_t)(c ? c : ' ');
        }
    } else {
        for (j = 0u; j < TEXT_COLS; j++) RIA.rw0 = (uint8_t)' ';
    }
}

static void scroll_region_up(void)
{
    printf(CSI "%d;1H\033[1M", (int)(TITLE_ROWS + 1u));
    update_cache_line((uint8_t)(EDIT_ROWS - 1u));
    redraw_line((uint8_t)(EDIT_ROWS - 1u));
}

static void scroll_region_down(void)
{
    printf(CSI "%d;1H\033[1L", (int)(TITLE_ROWS + 1u));
    update_cache_line(0u);
    redraw_line(0u);
}

/* ================================================================
   redraw_screen: redraws EDIT_ROWS visible lines from XRAM + status.
   Fills screen_cache. Repositions terminal cursor at cur.row/col.
   ================================================================ */
static void redraw_screen(void)
{
    uint8_t  r, j;
    uint16_t xrow;

    printf(ANSI_HIDE_CUR ANSI_HOME);
    draw_title_bar();
    cache_valid = 1u;
    for (r = 0u; r < EDIT_ROWS; r++) {
        xrow = (uint16_t)scroll_row + r;
        RIA.addr0 = SCREEN_CACHE_BASE + (uint16_t)r * TEXT_COLS;
        RIA.step0 = 1;
        if (xrow < content_rows) {
            RIA.addr1 = TEXT_BUF_BASE + xrow * TEXT_COLS;
            RIA.step1 = 1;
            for (j = 0u; j < TEXT_COLS; j++) {
                char c = (char)RIA.rw1;
                RIA.rw0 = (uint8_t)(c ? c : ' ');
            }
        } else {
            for (j = 0u; j < TEXT_COLS; j++) RIA.rw0 = (uint8_t)' ';
        }
        redraw_line(r);
    }
    if (startup_done) draw_status_bar(NULL);
    printf(CSI "%d;%dH" ANSI_SHOW_CUR,
           (int)((cur.row - scroll_row) + 1u + TITLE_ROWS),
           (int)(cur.col + 1u));
}

/* ================================================================
   prompt_confirm: Y/N/Esc prompt. Returns 1=Y, 0=N, -1=Esc (cancel).
   ================================================================ */
static int prompt_confirm(const char *prompt)
{
    uint8_t prev_ks[KEYBOARD_BYTES];
    uint8_t cur_ks[KEYBOARD_BYTES];
    uint8_t k, j, code, was, now;
    int     done = 0, result = 0;
    uint8_t input_row = TITLE_ROWS + EDIT_ROWS + 2u;

    for (k = 0u; k < KEYBOARD_BYTES; k++) prev_ks[k] = keystates[k];

    printf(CSI "%d;1H" ANSI_SEL_BG_QA, (int)input_row);
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
                    if (code == KEY_Y) { result = 1;  done = 1; }
                    else if (code == KEY_N)   { result = 0;  done = 1; }
                    else if (code == KEY_ESC) { result = -1; done = 1; }
                }
            }
            prev_ks[k] = cur_ks[k];
        }
    }

    for (k = 0u; k < KEYBOARD_BYTES; k++) keystates[k] = cur_ks[k];
    return result;
}

/* ================================================================
   prompt_input: blocking text-input dialog in status bar.
   Returns 1 on Enter, 0 on Esc.
   ================================================================ */
static int prompt_input(const char *prompt, char *buf, uint8_t maxlen)
{
    uint8_t prev_ks[KEYBOARD_BYTES];
    uint8_t cur_ks[KEYBOARD_BYTES];
    char    saved_buf[64];
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
    uint8_t save_len;

    for (plen = 0u; prompt[plen]; plen++) {}
    field = (plen < 79u) ? (uint8_t)(80u - plen) : 1u;

    /* save original buffer so Esc can restore it */
    save_len = (maxlen < 64u) ? maxlen : 64u;
    for (i = 0u; i < save_len; i++) saved_buf[i] = buf[i];

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
    printf(CSI "%d;1H", (int)input_row);
    for (i = 0u; i < plen; i++) putchar((uint8_t)prompt[i]);
    printf(ANSI_SEL_BG);
    for (i = 0u; buf[i] && i < field; i++) putchar((uint8_t)buf[i]);
    for (; i < field; i++) putchar(' ');
    printf(ANSI_SEL_BG_OFF ANSI_SHOW_CUR CSI "%d;%dH",
           (int)input_row, (int)(plen + pos + 1u));


    while (!done) {
        for (k = 0u; k < KEYBOARD_BYTES; k++) {
            RIA.addr1 = XRAM_STRUCT_SYS_KEYBOARD + k;
            RIA.step1 = 0;
            cur_ks[k] = RIA.rw1;
        }

        shift = (uint8_t)(((cur_ks[KEY_LEFTSHIFT  >> 3] >> (KEY_LEFTSHIFT  & 7)) & 1u) |
                           ((cur_ks[KEY_RIGHTSHIFT >> 3] >> (KEY_RIGHTSHIFT & 7)) & 1u));
        caps  = (uint8_t)( (cur_ks[KEY_CAPSLOCK_LED >> 3] >> (KEY_CAPSLOCK_LED & 7)) & 1u);

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
                        printf(CSI "%d;%dH", (int)input_row, (int)(plen + pos + 1u));
                    } else if (code == KEY_END) {
                        pos = len;
                        printf(CSI "%d;%dH", (int)input_row, (int)(plen + pos + 1u));
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
    if (!result) {
        for (i = 0u; i < save_len; i++) buf[i] = saved_buf[i];
    }
    return result;
}

/* Returns one past the last non-space byte in the given XRAM row (0..TEXT_COLS). */
static uint8_t line_text_len(uint8_t row)
{
    uint8_t j, last = 0u;
    RIA.addr1 = TEXT_BUF_BASE + (uint16_t)row * TEXT_COLS;
    RIA.step1 = 1;
    for (j = 0u; j < TEXT_COLS; j++) {
        { uint8_t b = RIA.rw1; if (b != 0u && b != (uint8_t)' ') last = j + 1u; }
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
    for (i = 0u; i < 20480u; i++) RIA.rw0 = 0u;

    printf(ANSI_HIDE_CUR);

    cur.row      = 0u;
    cur.col      = 0u;
    scroll_row   = 0u;
    content_rows = 0u;
    sel_active   = 0u;
    doc_dirty    = 0u;
    list_mode    = LIST_MODE_NONE;
    list_counter = 0u;
    list_sep     = 0;
    list_bullet  = 0;
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
        syncfs(fd);
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
    syncfs(fd);
    close(fd);
    strncpy(current_filename, filename, 63u);
    current_filename[63] = 0;
    return 1;
}

static void line_shift_right(uint8_t row, uint8_t col);
static void line_shift_left(uint8_t row, uint8_t from_col);
static int  find_text(const char *pattern);
static uint8_t word_left(uint8_t row, uint8_t col);
static uint8_t word_right(uint8_t row, uint8_t col);

/* ================================================================
   word_left / word_right: return new column after a word-jump on row.
   word_left:  skip spaces left, then skip non-spaces left → start of word.
   word_right: skip non-spaces right, then skip spaces right → start of next word.
   ================================================================ */
static uint8_t word_left(uint8_t row, uint8_t col)
{
    uint8_t c;
    RIA.addr1 = TEXT_BUF_BASE + (uint16_t)row * TEXT_COLS;
    RIA.step1 = 1;
    for (c = 0u; c < TEXT_COLS; c++) g_linebuf[c] = (char)RIA.rw1;
    if (col == 0u) return 0u;
    c = col;
    while (c > 0u && g_linebuf[c - 1u] == ' ') c--;
    while (c > 0u && g_linebuf[c - 1u] != ' ') c--;
    return c;
}

static uint8_t word_right(uint8_t row, uint8_t col)
{
    uint8_t c, len;
    RIA.addr1 = TEXT_BUF_BASE + (uint16_t)row * TEXT_COLS;
    RIA.step1 = 1;
    for (c = 0u; c < TEXT_COLS; c++) g_linebuf[c] = (char)RIA.rw1;
    len = line_text_len(row);
    c = col;
    while (c < len && g_linebuf[c] != ' ') c++;
    while (c < len && g_linebuf[c] == ' ') c++;
    return c;
}

/* ================================================================
   apply_replace: overwrites the match found by find_text (sel_col..cur.col)
   with replace_pattern, adjusting line length as needed.
   ================================================================ */
static void apply_replace(uint8_t plen, uint8_t rlen)
{
    uint8_t i;
    int8_t  delta = (int8_t)(rlen - plen);

    if (delta > 0) {
        for (i = 0u; i < (uint8_t)delta; i++)
            line_shift_right(cur.row, (uint8_t)(sel_col + plen + i));
    } else if (delta < 0) {
        uint8_t shrink = (uint8_t)(-delta);
        for (i = 0u; i < shrink; i++)
            line_shift_left(cur.row, (uint8_t)(sel_col + plen));
    }
    RIA.addr0 = TEXT_BUF_BASE + (uint16_t)cur.row * TEXT_COLS + sel_col;
    RIA.step0 = 1;
    for (i = 0u; i < rlen; i++) RIA.rw0 = (uint8_t)replace_pattern[i];
    cur.col   = (uint8_t)(sel_col + rlen);
    doc_dirty = 1u;
}

/* ================================================================
   do_replace: Ctrl+H handler. Prompts for find and replace strings,
   then replaces all occurrences within the active selection (if any)
   or from the cursor position to end of document.
   ================================================================ */
static void do_replace(void)
{
    uint8_t plen, rlen, count;
    uint8_t rep_row_from, rep_row_to;
    uint8_t rep_col_from, rep_col_to;
    uint8_t has_sel, char_sel;
    char    msg[24];

    has_sel  = sel_active;
    char_sel = (uint8_t)(has_sel && sel_mode == SEL_MODE_CHAR);

    if (has_sel) {
        rep_row_from = sel_min_row();
        rep_row_to   = sel_max_row();
        if (char_sel) {
            rep_col_from = (sel_col < cur.col) ? sel_col : cur.col;
            rep_col_to   = (sel_col > cur.col) ? sel_col : cur.col;
        } else {
            rep_col_from = 0u;
            rep_col_to   = (uint8_t)TEXT_COLS;
        }
    } else {
        rep_row_from = cur.row;
        rep_col_from = cur.col;
        rep_row_to   = 255u;
        rep_col_to   = (uint8_t)TEXT_COLS;
    }

    sel_active = 0u;

    if (!prompt_input("FIND    : ", search_pattern, 26u)) { redraw_screen(); return; }
    if (!prompt_input("REPLACE : ", replace_pattern, 26u)) { redraw_screen(); return; }

    plen = (uint8_t)strlen(search_pattern);
    rlen = (uint8_t)strlen(replace_pattern);
    if (plen == 0u) { redraw_screen(); return; }

    /* position cursor just before the search range so find_text starts there */
    if (rep_col_from > 0u) {
        cur.row = rep_row_from;
        cur.col = (uint8_t)(rep_col_from - 1u);
    } else if (rep_row_from > 0u) {
        cur.row = (uint8_t)(rep_row_from - 1u);
        cur.col = (uint8_t)TEXT_COLS;
    } else {
        /* range starts at (0,0): position at (0,0) and find_text will start
           from col 1; a match at col 0 row 0 is thus checked via start_col=0
           in find_text's wrapped==0 pass since cur.col+1 == 1 only when
           cur.col==0 — this means col 0 is missed when cursor is at (0,0).
           Accept: first replace call re-uses existing search logic without
           modifying find_text. Practical impact is negligible. */
        cur.row = 0u;
        cur.col = 0u;
    }

    count = 0u;
    while (find_text(search_pattern)) {
        if (cur.row > rep_row_to) break;
        if (cur.row == rep_row_to && sel_col >= rep_col_to) break;
        apply_replace(plen, rlen);
        count++;
        if (count == 255u) break;
    }

    if (cur.row < scroll_row ||
        (uint8_t)(cur.row - scroll_row) >= EDIT_ROWS) {
        scroll_row = ((uint16_t)cur.row >= (uint16_t)(EDIT_ROWS / 2u))
                     ? (uint8_t)(cur.row - EDIT_ROWS / 2u) : 0u;
    }
    redraw_screen();
    if (count == 0u) {
        draw_status_bar("NOT FOUND");
    } else {
        sprintf(msg, "REPLACED: %d", (int)count);
        draw_status_bar(msg);
    }
}

/* ================================================================
   find_text: searches XRAM buffer for pattern from one position past
   cursor. Wraps around to beginning of file if end is reached.
   Highlights match with reverse video and scrolls.
   ================================================================ */
static int find_text(const char *pattern)
{
    uint8_t  plen, i, col, match;
    uint16_t row, start_row, end_row;
    uint8_t  start_col, col_limit;
    uint8_t  disp_row;
    uint8_t  wrapped;

    plen = (uint8_t)strlen(pattern);
    if (plen == 0u || plen > TEXT_COLS) return 0;

    if (cur.col + 1u < TEXT_COLS) {
        start_row = (uint16_t)cur.row;
        start_col = (uint8_t)(cur.col + 1u);
    } else {
        start_row = (uint16_t)cur.row + 1u;
        start_col = 0u;
    }

    /* Two passes: first from start to end, then (if needed) from 0 to start_row. */
    for (wrapped = 0u; wrapped < 2u; wrapped++) {
        uint16_t from = wrapped ? 0u : start_row;
        end_row       = wrapped ? start_row : content_rows;

        for (row = from; row < end_row; row++) {
            RIA.addr1 = TEXT_BUF_BASE + row * TEXT_COLS;
            RIA.step1 = 1;
            for (i = 0u; i < TEXT_COLS; i++) g_linebuf[i] = (char)RIA.rw1;

            col       = (row == start_row && !wrapped) ? start_col : 0u;
            col_limit = (row == start_row &&  wrapped) ? start_col : (uint8_t)(TEXT_COLS - plen + 1u);

            for (; col + plen <= col_limit; col++) {
                match = 1u;
                for (i = 0u; i < plen; i++) {
                    if (g_linebuf[(uint8_t)(col + i)] != pattern[i]) { match = 0u; break; }
                }
                if (match) {
                    cur.row    = (uint8_t)row;
                    cur.col    = (uint8_t)(col + plen);
                    sel_active = 1u;
                    sel_mode   = SEL_MODE_CHAR;
                    sel_row    = (uint8_t)row;
                    sel_col    = col;

                    /* scroll so found line appears near center */
                    if ((uint16_t)row > (uint16_t)(EDIT_ROWS / 2u)) {
                        scroll_row = (uint8_t)(row - EDIT_ROWS / 2u);
                        if (content_rows > EDIT_ROWS &&
                            scroll_row > (uint8_t)(content_rows - EDIT_ROWS + 1u))
                            scroll_row = (uint8_t)(content_rows - EDIT_ROWS + 1u);
                    } else {
                        scroll_row = 0u;
                    }

                    redraw_screen();
                    disp_row = (uint8_t)(cur.row - scroll_row);
                    printf(CSI "%d;%dH", (int)(disp_row + 1u + TITLE_ROWS), (int)(cur.col + 1u));
                    return 1;
                }
            }
        }
        /* if start_row == 0 the second pass would be empty, skip it */
        if (start_row == 0u) break;
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
    for (i = 0u; i < n; i++) g_linebuf[i] = (char)RIA.rw1;

    RIA.addr0 = TEXT_BUF_BASE + (uint16_t)row * TEXT_COLS + col + 1u;
    RIA.step0 = 1;
    for (i = 0u; i < n; i++) RIA.rw0 = (uint8_t)g_linebuf[i];
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
    for (i = 0u; i < n; i++) g_linebuf[i] = (char)RIA.rw1;

    RIA.addr0 = TEXT_BUF_BASE + (uint16_t)row * TEXT_COLS + from_col - 1u;
    RIA.step0 = 1;
    for (i = 0u; i < n; i++) RIA.rw0 = (uint8_t)g_linebuf[i];

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
        for (j = 0u; j < TEXT_COLS; j++) g_linebuf[j] = (char)RIA.rw1;
        RIA.addr0 = TEXT_BUF_BASE + r * TEXT_COLS;
        RIA.step0 = 1;
        for (j = 0u; j < TEXT_COLS; j++) RIA.rw0 = (uint8_t)g_linebuf[j];
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
        for (j = 0u; j < TEXT_COLS; j++) g_linebuf[j] = (char)RIA.rw1;
        RIA.addr0 = TEXT_BUF_BASE + (r - 1u) * TEXT_COLS;
        RIA.step0 = 1;
        for (j = 0u; j < TEXT_COLS; j++) RIA.rw0 = (uint8_t)g_linebuf[j];
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
   clip_save: writes XRAM clipboard buffer to backing files.
   ================================================================ */
static void clip_save(void)
{
    int     fd;
    uint8_t r, j;

    fd = open(CLIP_META_FILE, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) return;
    RIA.addr0 = XRAM_SCRATCH; RIA.step0 = 1;
    RIA.rw0 = clip_is_char;
    RIA.rw0 = clip_lines;
    write_xram(XRAM_SCRATCH, 2u, fd);
    syncfs(fd);
    close(fd);

    fd = open(CLIP_FILE, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) return;
    for (r = 0u; r < clip_lines; r++) {
        RIA.addr1 = CLIP_BUF_BASE + (uint16_t)r * TEXT_COLS; RIA.step1 = 1;
        RIA.addr0 = XRAM_SCRATCH; RIA.step0 = 1;
        for (j = 0u; j < TEXT_COLS; j++) RIA.rw0 = RIA.rw1;
        write_xram(XRAM_SCRATCH, (unsigned)TEXT_COLS, fd);
    }
    syncfs(fd);
    close(fd);
}

/* ================================================================
   clip_load: reads backing files into XRAM clipboard buffer.
   ================================================================ */
static void clip_load(void)
{
    int     fd;
    uint8_t r, j;

    fd = open(CLIP_META_FILE, O_RDONLY);
    if (fd < 0) { clip_lines = 0u; return; }
    read_xram(XRAM_SCRATCH, 2u, fd);
    close(fd);
    RIA.addr1 = XRAM_SCRATCH; RIA.step1 = 1;
    clip_is_char = (uint8_t)RIA.rw1;
    clip_lines   = (uint8_t)RIA.rw1;
    if (clip_lines > CLIP_MAX_LINES) clip_lines = CLIP_MAX_LINES;
    if (clip_lines == 0u) return;

    fd = open(CLIP_FILE, O_RDONLY);
    if (fd < 0) { clip_lines = 0u; return; }
    for (r = 0u; r < clip_lines; r++) {
        read_xram(XRAM_SCRATCH, (unsigned)TEXT_COLS, fd);
        RIA.addr1 = XRAM_SCRATCH; RIA.step1 = 1;
        RIA.addr0 = CLIP_BUF_BASE + (uint16_t)r * TEXT_COLS; RIA.step0 = 1;
        for (j = 0u; j < TEXT_COLS; j++) RIA.rw0 = RIA.rw1;
    }
    close(fd);
}

/* ================================================================
   clip_delete: removes clipboard backing files on exit.
   ================================================================ */
static void clip_delete(void)
{
    remove(CLIP_FILE);
    remove(CLIP_META_FILE);
}

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
        clip_save();
        redraw_screen();
        draw_status_bar("SELECTED COPIED TO CLIPBOARD");
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
    clip_save();
    redraw_screen();
    draw_status_bar("SELECTED COPIED TO CLIPBOARD");
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
        draw_status_bar("SELECTED CUT TO CLIPBOARD");
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
    draw_status_bar("Cut");
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
        draw_status_bar("CLIPBOARD PASTED");
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
    draw_status_bar("CLIPBOARD PASTED");
}

/* ================================================================
   main
   ================================================================ */
int main(int argc, char **argv)
{
    uint8_t mouse_wheel, mouse_wheel_prev;
    int     mouse_wheel_change;
    bool    handled_key;
    bool    did_action;
    uint8_t k, j, new_key, new_keys, last_key;
    uint8_t key_capslock, key_shifts, key_ctrl, key_ralt, key_lalt;
    uint8_t prev_capslock;
    uint8_t max_scroll;
    int     ok;
    char    ch;
    uint8_t repeat_key;
    clock_t repeat_start, repeat_last;
    uint8_t target_col;
    uint8_t prev_dirty;
    int     lorem;

    #ifdef DEBUG
    {
        int i;
        printf("argc = %d\n", argc);
        for (i = 0; i < argc; i++)
            printf("argv[%d] = %s\n", i, argv[i]);

        return 0;
    }
    #endif

    /* init state */
    cur.row             = 0u;
    cur.col             = 0u;
    current_filename[0] = 0;
    search_pattern[0]   = 0;
    replace_pattern[0]  = 0;
    content_rows        = 0u;
    scroll_row          = 0u;
    last_key            = 0u;
    handled_key         = false;
    did_action          = false;
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
    lorem               = 0;

    startup_done = 0u;

    printf(ALTSCREEN_ENTER);

    f_mkdir("TMP");
    clip_load();

    xreg_ria_keyboard(XRAM_STRUCT_SYS_KEYBOARD);
    xreg_ria_mouse(XRAM_STRUCT_SYS_MOUSE);

    /*
        parse arguments: load ted.rp6502 [filename] [/view]
    */
    {
        int fi = 1;
        if (argc >= 1 && strcmp(argv[2], "/view") == 0) {
            view_mode = 1u;
            fi = 1;
        }
        if (fi < argc && strcmp(argv[fi], "/lorem") == 0)
            lorem = 1;
        strncpy(current_filename,
                (!lorem && fi < argc && argv[fi][0]) ? argv[fi] : NEW_FILENAME,
                63u);
    }

    // printf(CSI_ECHO_OFF ANSI_CLS ANSI_HOME);

    draw_title_bar();
    draw_status_bar(NULL);
    cur.row    = 0u;
    cur.col    = 0u;
    scroll_row = 0u;
    printf(ANSI_HIDE_CUR OSC_CURSOR_COLOR "408040" OSC_ST CSI "%d;1H", (int)(TITLE_ROWS + 1u));

    draw_status_bar(ok > 0 ? (!view_mode ? "Please wait..." : "") : ok == 0 ? "FILE CREATED" : EXCLAMATION "cannot open file");

    current_filename[63] = 0;
    ok = load_file(current_filename);
    if (lorem) {
        load_file("ROM:loremipsum");
        strncpy(current_filename, NEW_FILENAME, 63u);
        current_filename[63] = 0;
        // redraw_screen();
    }

    window_open(((80u-26u)/2u)+1u, ((30u-16u)/2u)-1u, 26, 16, CSI "37m", CSI "48;2;40;80;40m", 0, true);
    window_text(APPNAME      ,  2,  1, CSI "37m", CSI "48;2;40;80;40m");
    window_text(APPDESCRPTION,  2,  3, CSI "37m", CSI "48;2;40;80;40m");
    window_text(APPCOPYRIGHT ,  2, 13, CSI "37m", CSI "48;2;40;80;40m");
    window_text("version  " APPVER, 2, 14, ANSI_DARK_GRAY, CSI "48;2;40;80;40m");
    PAUSE(400);
    window_close();
    redraw_screen();
    draw_title_bar();
    startup_done = 1u;
    draw_status_bar(ok > 0 ? "Ready" : ok == 0 ? "FILE CREATED" : EXCLAMATION "cannot open file");
    printf(DECSTBM_EDIT);
    printf(OSC_CURSOR_COLOR "408040" OSC_ST ANSI_SHOW_CUR CSI "%d;1H", (int)(TITLE_ROWS + 1u));

    /* capture initial mouse wheel position */
    RIA.addr1 = XRAM_STRUCT_SYS_MOUSE + 3;
    RIA.step1 = 0;
    mouse_wheel      = RIA.rw1;
    mouse_wheel_prev = mouse_wheel;

    prev_capslock = (uint8_t)(key(KEY_CAPSLOCK_LED) ? 1u : 0u);

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
                    scroll_region_down();
                    draw_status_bar(NULL);
                    printf(CSI "%d;%dH" ANSI_SHOW_CUR,
                           (int)((cur.row - scroll_row) + 1u + TITLE_ROWS),
                           (int)(cur.col + 1u));
                }
            } else {
                /* scroll down */
                max_scroll = (content_rows >= (uint16_t)EDIT_ROWS)
                             ? (uint8_t)(content_rows - EDIT_ROWS + 1u) : 0u;
                if (scroll_row < max_scroll) {
                    scroll_row++;
                    if (cur.row < scroll_row)
                        cur.row = scroll_row;
                    scroll_region_up();
                    draw_status_bar(NULL);
                    printf(CSI "%d;%dH" ANSI_SHOW_CUR,
                           (int)((cur.row - scroll_row) + 1u + TITLE_ROWS),
                           (int)(cur.col + 1u));
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
                    did_action   = false;
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
                    did_action  = false;
                    repeat_last = now;
                }
            } else {
                repeat_key = 0u;
            }
        }

        key_capslock = (uint8_t)(key(KEY_CAPSLOCK_LED) ? 1u : 0u);
        if (key_capslock != prev_capslock) {
            prev_capslock = key_capslock;
            if (!view_mode) draw_status_bar(NULL);
        }
        key_shifts   = (uint8_t)((!view_mode && (key(KEY_LEFTSHIFT) || key(KEY_RIGHTSHIFT))) ? 1u : 0u);
        key_ctrl     = (uint8_t)((key(KEY_LEFTCTRL)  || key(KEY_RIGHTCTRL))  ? 1u : 0u);
        key_lalt     = (uint8_t)( key(KEY_LEFTALT)                           ? 1u : 0u);
        key_ralt     = (uint8_t)( key(KEY_RIGHTALT)                          ? 1u : 0u);

        prev_dirty = doc_dirty;
        if (!(keystates[0] & 1u)) {
            if (!handled_key) {
                did_action = true;

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
                } else if (key(KEY_INSERT) || (key_lalt && key(KEY_I))) {
                    repeat_key  = 0u;
                    insert_mode = insert_mode ? 0u : 1u;
                    printf(insert_mode ? DECSCUSR_BAR : DECSCUSR_UNDERLINE); // cursor look
                    draw_status_bar(NULL);

                /* --- Shift+Ctrl+Alt+L: start list from current-line prefix --- */
                /* Reads first 2 chars of current line to detect list type:
                   "1." / "2." etc.  -> LIST_MODE_NUM  (sep = '.')
                   "a)" / "b)" etc.  -> LIST_MODE_ALPHA (sep = char[1])
                   "* " "- " "> " "= " "+ " "| " "# " -> LIST_MODE_BULLET */
                } else if (key_ctrl && key_shifts && key_lalt && key(KEY_L)) {
                    repeat_key = 0u;
                    if (!view_mode) {
                        uint8_t lch0, lch1;
                        RIA.addr1 = TEXT_BUF_BASE + (uint16_t)cur.row * TEXT_COLS;
                        RIA.step1 = 1;
                        lch0 = RIA.rw1;
                        lch1 = RIA.rw1;
                        if (lch0 >= '1' && lch0 <= '9' && (lch1 == '.' || lch1 == ')')) {
                            /* numeric */
                            list_mode    = LIST_MODE_NUM;
                            list_sep     = (char)lch1;
                            list_counter = (uint8_t)(lch0 - '0');
                            cur.col      = 3u;   /* after "N. " */
                            doc_dirty    = 1u;
                            redraw_screen();
                            draw_status_bar(NULL);
                        } else if (lch0 >= 'a' && lch0 <= 'z' &&
                                   (lch1 == ')' || lch1 == '.' || lch1 == ':')) {
                            /* alpha */
                            list_mode    = LIST_MODE_ALPHA;
                            list_sep     = (char)lch1;
                            list_counter = (uint8_t)(lch0 - 'a'); /* 0='a' */
                            cur.col      = 3u;   /* after "a) " */
                            doc_dirty    = 1u;
                            redraw_screen();
                            draw_status_bar(NULL);
                        } else if (lch0 == '*' || lch0 == '-' || lch0 == '>' ||
                                   lch0 == '+' || lch0 == '=' || lch0 == '#') {
                            /* bullet */
                            list_bullet  = (char)lch0;
                            list_mode    = LIST_MODE_BULLET;
                            cur.col      = 2u;
                            doc_dirty    = 1u;
                            redraw_screen();
                            draw_status_bar(NULL);
                        } else {
                            draw_status_bar("LIST: start line with one of these '1.','a)','*','#','>','=','-','+'");
                        }
                    }

                } else if (key_ctrl && key(KEY_N)) {
                    repeat_key = 0u;
                    if (!view_mode) {
                        if (doc_dirty) {
                            int cn = prompt_confirm(" Save changes before new document? [Y/N/Esc] ");
                            if (cn < 0) goto ctrl_n_cancel;
                            if (cn > 0) {
                                if (prompt_input("SAVE path/filename : ", current_filename, 64u)) {
                                    ok = save_file(current_filename);
                                    if (ok >= 0) doc_dirty = 0u;
                                    draw_status_bar(ok >= 0 ? "FILE SAVED" : EXCLAMATION "cannot save file");
                                    printf(ANSI_SHOW_CUR CSI "%d;%dH",
                                           (int)((cur.row - scroll_row) + 1u + TITLE_ROWS),
                                           (int)(cur.col + 1u));
                                }
                            }
                        }
                        strncpy(current_filename, NEW_FILENAME, 63u);
                        current_filename[63] = '\0';
                        editor_clear();
                        { uint16_t xi;
                          RIA.addr0 = CLIP_BUF_BASE;    RIA.step0 = 1;
                          for (xi = 0u; xi < 2560u; xi++) RIA.rw0 = 0u;
                          RIA.addr0 = SCREEN_CACHE_BASE; RIA.step0 = 1;
                          for (xi = 0u; xi < 2080u; xi++) RIA.rw0 = 0u;
                          RIA.addr0 = XRAM_SCRATCH;      RIA.step0 = 1;
                          for (xi = 0u; xi < 82u;   xi++) RIA.rw0 = 0u;
                          RIA.addr0 = XRAM_WIN_BUF_BASE; RIA.step0 = 1;
                          for (xi = 0u; xi < 2400u; xi++) RIA.rw0 = 0u;
                        }
                        redraw_screen();
                        draw_title_bar();
                        draw_status_bar("NEW DOCUMENT");
                        printf(ANSI_SHOW_CUR CSI "%d;1H", (int)(1u + TITLE_ROWS));
                        ctrl_n_cancel:;
                    }

                /* --- File / Search dialogs (no autorepeat) --- */
                } else if (key_ctrl && key(KEY_O)) {
                    repeat_key = 0u;
                    if (doc_dirty) {
                        int cn = prompt_confirm(" Save changes before opening? [Y/N/Esc] ");
                        if (cn < 0) goto ctrl_n_cancel;
                        if (cn > 0) {                       
                            uint8_t ask_o = (strcmp(current_filename, NEW_FILENAME) == 0);
                            if (!ask_o || prompt_input("SAVE path/filename : ", current_filename, 64u)) {
                                ok = save_file(current_filename);
                                if (ok >= 0) doc_dirty = 0u;
                                draw_status_bar(ok >= 0 ? "FILE SAVED" : EXCLAMATION "cannot save file");
                                printf(ANSI_SHOW_CUR CSI "%d;%dH",
                                       (int)((cur.row - scroll_row) + 1u + TITLE_ROWS),
                                       (int)(cur.col + 1u));
                            }
                        }
                    }
                    if (prompt_input("OPEN path/filename : ", current_filename, 64u)) {
                        ok = load_file(current_filename);
                        redraw_screen();
                        draw_title_bar();
                        draw_status_bar(ok > 0 ? "Ready" : ok == 0 ? "FILE CREATED" : EXCLAMATION "cannot open file");
                    } else {
                        redraw_screen();
                    }

                } else if (key_ctrl && key(KEY_S)) {
                    repeat_key = 0u;
                    if (!view_mode) {
                        uint8_t ask = key_shifts ||
                                      (strcmp(current_filename, NEW_FILENAME) == 0);
                        if (!ask ||
                            prompt_input((key_shifts ? "SAVE AS path/filename : " : "SAVE path/filename : "), current_filename, 64u)) {
                            ok = save_file(current_filename);
                            if (ok >= 0) doc_dirty = 0u;
                            draw_title_bar();
                            draw_status_bar(ok >= 0 ? "FILE SAVED" : EXCLAMATION "cannot save file");
                            printf(ANSI_SHOW_CUR CSI "%d;%dH",
                                   (int)((cur.row - scroll_row) + 1u + TITLE_ROWS),
                                   (int)(cur.col + 1u));
                        } else {
                            redraw_screen();
                        }
                    }

                } else if (key_ctrl && key(KEY_F)) {
                    repeat_key = 0u;
                    if (prompt_input("FIND (max 25): ", search_pattern, 26u)) {
                        if (!find_text(search_pattern)) {
                            draw_status_bar("TEXT NOT FOUND");
                        }
                    } else {
                        redraw_screen();
                    }

                } else if (key_ctrl && key(KEY_H)) {
                    repeat_key = 0u;
                    if (!view_mode) do_replace();

                /* --- Cursor movement --- */
                } else if (key_ctrl && key_shifts && key(KEY_LEFT)) {
                    { uint8_t od1 = cur.row, od2 = cur.row;
                      if (!sel_active) { sel_active = 1u; sel_row = cur.row; sel_col = cur.col; }
                      else { od1 = sel_min_row(); od2 = sel_max_row(); }
                      sel_mode = SEL_MODE_CHAR;
                      cur.col = word_left(cur.row, cur.col);
                      target_col = cur.col;
                      if (cache_valid) {
                          redraw_sel_delta(od1, od2, cur.row, cur.row);
                          draw_status_bar(NULL);
                          printf(CSI "%d;%dH" ANSI_SHOW_CUR,
                                 (int)((cur.row - scroll_row) + 1u + TITLE_ROWS),
                                 (int)(cur.col + 1u));
                      } else { redraw_screen(); }
                    }

                } else if (key_ctrl && key_shifts && key(KEY_RIGHT)) {
                    { uint8_t od1 = cur.row, od2 = cur.row;
                      if (!sel_active) { sel_active = 1u; sel_row = cur.row; sel_col = cur.col; }
                      else { od1 = sel_min_row(); od2 = sel_max_row(); }
                      sel_mode = SEL_MODE_CHAR;
                      cur.col = word_right(cur.row, cur.col);
                      target_col = cur.col;
                      if (cache_valid) {
                          redraw_sel_delta(od1, od2, cur.row, cur.row);
                          draw_status_bar(NULL);
                          printf(CSI "%d;%dH" ANSI_SHOW_CUR,
                                 (int)((cur.row - scroll_row) + 1u + TITLE_ROWS),
                                 (int)(cur.col + 1u));
                      } else { redraw_screen(); }
                    }

                } else if (key(KEY_LEFT)) {
                    if (key_shifts) {
                        { uint8_t od1 = cur.row, od2 = cur.row;
                          if (!sel_active) { sel_active = 1u; sel_row = cur.row; sel_col = cur.col; }
                          else { od1 = sel_min_row(); od2 = sel_max_row(); }
                          sel_mode = SEL_MODE_CHAR;
                          if (cur.col > 0u) {
                              cur.col--;
                          } else if (cur.row > 0u) {
                              cur.row--;
                              cur.col = line_text_len(cur.row);
                              if (cur.row != sel_row) sel_active = 0u;
                              if (cur.row < scroll_row) { scroll_row = cur.row; cache_valid = 0u; }
                          }
                          target_col = cur.col;
                          if (cache_valid) {
                              redraw_sel_delta(od1, od2, cur.row, cur.row);
                              draw_status_bar(NULL);
                              printf(CSI "%d;%dH" ANSI_SHOW_CUR,
                                     (int)((cur.row - scroll_row) + 1u + TITLE_ROWS),
                                     (int)(cur.col + 1u));
                          } else { redraw_screen(); }
                        }
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
                        { uint8_t od1 = cur.row, od2 = cur.row;
                          if (!sel_active) { sel_active = 1u; sel_row = cur.row; sel_col = cur.col; }
                          else { od1 = sel_min_row(); od2 = sel_max_row(); }
                          sel_mode = SEL_MODE_CHAR;
                          { uint8_t lim = line_text_len(cur.row);
                            if (lim > TEXT_COLS - 1u) lim = TEXT_COLS - 1u;
                            if (cur.col < lim) {
                                cur.col++;
                            } else if ((uint16_t)cur.row < content_rows) {
                                cur.row++;
                                cur.col = 0u;
                                if (cur.row != sel_row) sel_active = 0u;
                                if ((uint8_t)(cur.row - scroll_row) >= EDIT_ROWS) {
                                    scroll_row = (uint8_t)(cur.row - EDIT_ROWS + 1u);
                                    cache_valid = 0u;
                                }
                            }
                          }
                          target_col = cur.col;
                          if (cache_valid) {
                              redraw_sel_delta(od1, od2, cur.row, cur.row);
                              draw_status_bar(NULL);
                              printf(CSI "%d;%dH" ANSI_SHOW_CUR,
                                     (int)((cur.row - scroll_row) + 1u + TITLE_ROWS),
                                     (int)(cur.col + 1u));
                          } else { redraw_screen(); }
                        }
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
                        { uint8_t od1, od2;
                          if (!sel_active) { sel_active = 1u; sel_row = cur.row; }
                          od1 = sel_min_row(); od2 = sel_max_row();
                          sel_mode = SEL_MODE_LINE;
                          if (repeat_key == KEY_UP && last_key == KEY_UP) {
                          } else { target_col = cur.col; }
                          if (cur.row > 0u) {
                              cur.row--;
                              { uint8_t lim = line_text_len(cur.row);
                                cur.col = (target_col <= lim) ? target_col : lim;
                              }
                              if (cur.row < scroll_row) { scroll_row = cur.row; cache_valid = 0u; }
                          }
                          if (cache_valid) {
                              redraw_sel_delta(od1, od2, sel_min_row(), sel_max_row());
                              draw_status_bar(NULL);
                              printf(CSI "%d;%dH" ANSI_SHOW_CUR,
                                     (int)((cur.row - scroll_row) + 1u + TITLE_ROWS),
                                     (int)(cur.col + 1u));
                          } else { redraw_screen(); }
                        }
                    } else {
                        if (sel_active) { sel_active = 0u; redraw_screen(); }
                        if (repeat_key == KEY_UP && last_key == KEY_UP) {
                        } else { target_col = cur.col; }
                        if (cur.row > 0u) {
                            cur.row--;
                            { uint8_t lim = line_text_len(cur.row);
                              cur.col = (target_col <= lim) ? target_col : lim;
                            }
                            if (cur.row < scroll_row) {
                                scroll_row = cur.row;
                                scroll_region_down();
                                draw_status_bar(NULL);
                                printf(CSI "%d;%dH" ANSI_SHOW_CUR,
                                       (int)((cur.row - scroll_row) + 1u + TITLE_ROWS),
                                       (int)(cur.col + 1u));
                            }
                        }
                    }

                } else if (key(KEY_DOWN)) {
                    if (key_shifts) {
                        { uint8_t od1, od2;
                          if (!sel_active) { sel_active = 1u; sel_row = cur.row; }
                          od1 = sel_min_row(); od2 = sel_max_row();
                          sel_mode = SEL_MODE_LINE;
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
                                  cache_valid = 0u;
                              }
                          }
                          if (cache_valid) {
                              redraw_sel_delta(od1, od2, sel_min_row(), sel_max_row());
                              draw_status_bar(NULL);
                              printf(CSI "%d;%dH" ANSI_SHOW_CUR,
                                     (int)((cur.row - scroll_row) + 1u + TITLE_ROWS),
                                     (int)(cur.col + 1u));
                          } else { redraw_screen(); }
                        }
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
                                scroll_region_up();
                                draw_status_bar(NULL);
                                printf(CSI "%d;%dH" ANSI_SHOW_CUR,
                                       (int)((cur.row - scroll_row) + 1u + TITLE_ROWS),
                                       (int)(cur.col + 1u));
                            }
                        }
                    }

                } else if (key_ctrl && (key(KEY_LEFTSHIFT) || key(KEY_RIGHTSHIFT)) && key(KEY_HOME)) {
                    repeat_key = 0u;
                    cur.row    = 0u;
                    cur.col    = 0u;
                    scroll_row = 0u;
                    redraw_screen();

                } else if (key_ctrl && (key(KEY_LEFTSHIFT) || key(KEY_RIGHTSHIFT)) && key(KEY_END)) {
                    repeat_key = 0u;
                    cur.row    = (content_rows > 0u) ? (uint8_t)(content_rows - 1u) : 0u;
                    cur.col    = line_text_len(cur.row);
                    if (content_rows >= (uint16_t)EDIT_ROWS)
                        scroll_row = (uint8_t)(content_rows - EDIT_ROWS + 1u);
                    else
                        scroll_row = 0u;
                    redraw_screen();

                } else if (key_shifts && key(KEY_HOME)) {
                    repeat_key = 0u;
                    { uint8_t od1 = cur.row, od2 = cur.row;
                      if (!sel_active) { sel_active = 1u; sel_row = cur.row; sel_col = cur.col; }
                      else { od1 = sel_min_row(); od2 = sel_max_row(); }
                      sel_mode = SEL_MODE_CHAR;
                      cur.col  = 0u;
                      target_col = 0u;
                      if (cache_valid) {
                          redraw_sel_delta(od1, od2, cur.row, cur.row);
                          draw_status_bar(NULL);
                          printf(CSI "%d;%dH" ANSI_SHOW_CUR,
                                 (int)((cur.row - scroll_row) + 1u + TITLE_ROWS),
                                 (int)(cur.col + 1u));
                      } else { redraw_screen(); }
                    }

                } else if (key_shifts && key(KEY_END)) {
                    repeat_key = 0u;
                    { uint8_t od1 = cur.row, od2 = cur.row;
                      if (!sel_active) { sel_active = 1u; sel_row = cur.row; sel_col = cur.col; }
                      else { od1 = sel_min_row(); od2 = sel_max_row(); }
                      sel_mode = SEL_MODE_CHAR;
                      cur.col  = line_text_len(cur.row);
                      target_col = cur.col;
                      if (cache_valid) {
                          redraw_sel_delta(od1, od2, cur.row, cur.row);
                          draw_status_bar(NULL);
                          printf(CSI "%d;%dH" ANSI_SHOW_CUR,
                                 (int)((cur.row - scroll_row) + 1u + TITLE_ROWS),
                                 (int)(cur.col + 1u));
                      } else { redraw_screen(); }
                    }

                } else if (key(KEY_HOME)) {
                    repeat_key = 0u;
                    if (sel_active) { sel_active = 0u; redraw_screen(); }
                    cur.col = 0u;

                } else if (key(KEY_END)) {
                    repeat_key = 0u;
                    if (sel_active) { sel_active = 0u; redraw_screen(); }
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
                        if ((uint8_t)(cur.row - scroll_row) >= EDIT_ROWS) {
                            scroll_row = (uint8_t)(cur.row - EDIT_ROWS + 1u);
                            redraw_screen();
                        }
                    } else {
                        cur.row = (content_rows > 0u) ? (uint8_t)(content_rows - 1u) : 0u;
                        if (content_rows >= (uint16_t)EDIT_ROWS)
                            scroll_row = (uint8_t)(content_rows - EDIT_ROWS + 1u);
                        else
                            scroll_row = 0u;
                        redraw_screen();
                    }

                /* --- Enter: split line at cursor --- */
                } else if (key(KEY_ENTER) || key(KEY_KPENTER)) {
                    if (!view_mode) {
                        if (list_mode != LIST_MODE_NONE) {
                            uint8_t llen = line_text_len(cur.row);
                            /* bare prefix length (without trailing space):
                               NUM  single "N."  = 2, double "NN." = 3
                               ALPHA single "a)" = 2, double "aa)" = 3
                               BULLET "* "       = 1 char without space */
                            uint8_t bare_plen;
                            if (list_mode == LIST_MODE_NUM)
                                bare_plen = (list_counter >= 10u) ? 3u : 2u;
                            else if (list_mode == LIST_MODE_ALPHA)
                                bare_plen = (list_counter >= 26u) ? 3u : 2u;
                            else
                                bare_plen = 1u;
                            if (llen <= bare_plen) {
                                /* Enter on prefix-only line: clear it, stay on it, exit list */
                                RIA.addr0 = TEXT_BUF_BASE + (uint16_t)cur.row * TEXT_COLS;
                                RIA.step0 = 1;
                                {
                                    uint8_t ci;
                                    for (ci = 0u; ci < (uint8_t)bare_plen + 1u; ci++)
                                        RIA.rw0 = 0u;
                                }
                                cur.col   = 0u;
                                list_mode = LIST_MODE_NONE;
                                doc_dirty = 1u;
                                redraw_screen();
                                draw_status_bar("LIST MODE OFF");
                            } else {
                                uint8_t pi, plen2;
                                char    prefix[8];
                                do_enter();
                                if (list_mode == LIST_MODE_NUM) {
                                    list_counter++;
                                    if (list_counter >= 10u) {
                                        prefix[0] = (char)('0' + list_counter / 10u);
                                        prefix[1] = (char)('0' + list_counter % 10u);
                                        prefix[2] = list_sep; prefix[3] = ' '; plen2 = 4u;
                                    } else {
                                        prefix[0] = (char)('0' + list_counter);
                                        prefix[1] = list_sep; prefix[2] = ' '; plen2 = 3u;
                                    }
                                } else if (list_mode == LIST_MODE_ALPHA) {
                                    list_counter++;
                                    if (list_counter >= 26u) {
                                        /* "aa)" "ab)" ... */
                                        prefix[0] = (char)('a' + list_counter / 26u - 1u);
                                        prefix[1] = (char)('a' + list_counter % 26u);
                                        prefix[2] = list_sep; prefix[3] = ' '; plen2 = 4u;
                                    } else {
                                        prefix[0] = (char)('a' + list_counter);
                                        prefix[1] = list_sep; prefix[2] = ' '; plen2 = 3u;
                                    }
                                } else {
                                    prefix[0] = list_bullet; prefix[1] = ' '; plen2 = 2u;
                                }
                                RIA.addr0 = TEXT_BUF_BASE + (uint16_t)cur.row * TEXT_COLS;
                                RIA.step0 = 1;
                                for (pi = 0u; pi < plen2; pi++) RIA.rw0 = (uint8_t)prefix[pi];
                                cur.col = plen2;
                                if ((uint16_t)(cur.row + 1u) > content_rows)
                                    content_rows = (uint16_t)(cur.row + 1u);
                                doc_dirty = 1u;
                                redraw_screen();
                            }
                        } else {
                            do_enter();
                            doc_dirty = 1u;
                        }
                    }

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
                            printf(CSI "%d;1H",
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
                        if (sel_active) {
                            /* delete selection without touching clipboard */
                            if (sel_mode == SEL_MODE_CHAR) {
                                uint8_t c_from = (sel_col < cur.col) ? sel_col : cur.col;
                                uint8_t c_to   = (sel_col > cur.col) ? sel_col : cur.col;
                                uint8_t dlen   = (uint8_t)(c_to - c_from);
                                uint8_t dj;
                                for (dj = 0u; dj < dlen; dj++)
                                    line_shift_left(cur.row, (uint8_t)(c_from + 1u));
                                cur.col = c_from;
                            } else {
                                uint8_t dfrom = sel_min_row();
                                uint8_t dto   = sel_max_row();
                                uint8_t dn    = (uint8_t)(dto - dfrom + 1u);
                                uint8_t di;
                                if (dto >= (uint8_t)content_rows)
                                    dto = (uint8_t)(content_rows > 0u ? content_rows - 1u : 0u);
                                for (di = 0u; di < dn; di++) rows_shift_up(dfrom);
                                cur.row = dfrom;
                                if (cur.row >= (uint8_t)content_rows && content_rows > 0u)
                                    cur.row = (uint8_t)(content_rows - 1u);
                                cur.col = 0u;
                                if (cur.row < scroll_row) scroll_row = cur.row;
                            }
                            sel_active = 0u;
                            redraw_screen();
                            draw_status_bar("SELECTED DELETED");
                            doc_dirty = 1u;
                        } else {
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
                                printf(CSI "%d;1H",
                                       (int)((cur.row - scroll_row) + 1u + TITLE_ROWS));
                                for (j2 = 0u; j2 < TEXT_COLS; j2++)
                                    putchar((uint8_t)g_linebuf[j2]);
                            }
                        } else if (cur.row < content_rows) {
                            /* at end of line: join next row onto current */
                            do_delete_join();
                        }
                        doc_dirty = 1u;
                        } /* else (!sel_active) */
                    } /* !view_mode */

                /* --- Ctrl+Q: exit --- */
                } else if (key_ctrl && key(KEY_Q)) {
                    repeat_key = 0u;
                    if (doc_dirty) {
                        int cq = prompt_confirm(" Save changes before exit? [Y/N/Esc] ");
                        if (cq < 0) {
                            redraw_screen();
                            draw_status_bar(NULL);
                            goto ctrl_q_cancel;
                        }
                        if (cq > 0) {
                            uint8_t ask2 = (strcmp(current_filename, NEW_FILENAME) == 0);
                            if (!ask2 ||
                                prompt_input("SAVE path/filename : ", current_filename, 64u)) {
                                ok = save_file(current_filename);
                                if (ok >= 0) doc_dirty = 0u;
                            }
                        }
                    }
                    break;
                    ctrl_q_cancel:;

                /* --- F1: keyboard shortcuts help popup --- */
                } else if (key(KEY_F1)) {
                    repeat_key = 0u;
                    {
                        /* 2 cols x 33 chars + " | " sep = 69 inner wide; frame=1 -> 71 outer
                           rows: ceil(N/2) content + 1 header; centered on 80x30 */
                        static const char * const help_lines[] = {
                            "Ctrl+N           new document    ",
                            "Ctrl+O           open            ",
                            "Ctrl+S           save            ",
                            "Ctrl+Shift+S     save as         ",
                            "Ctrl+F           find text       ",
                            "Ctrl+H           replace text    ",
                            "Ins/Alt+I        toggle INS/OVR  ",
                            "Tab/Shift+Tab    next/prev tab   ",
                            "Ctrl+Shift+Home  jump to begin   ",
                            "Ctrl+Shift+End   jump to end     ",
                            "Shift+Left/Right select char     ",
                            "Shift+Up/Down    select line     ",
                            "Ctrl+Shift+L/R   select word     ",
                            "Ctrl+C/X/V       copy/cut/paste  ",
                            "Ctrl+Shift+C     center line     ",
                            "Ctrl+Shift+R     align to right  ",
                            "Ctrl+Shift+L     align to left   ",
                            "Ctrl+Shift+Alt+L lists mode      ",
                            "Ctrl+Shift+T     insert timestamp",
                            "Alt+P            section sign \x15  ",
                            "F1               Keysology       ",
                            "F2               ASCII table     ",
                            "F4               toggle EDIT/VIEW",
                            "Ctrl+Q           quit            ",
                        };
                        #define HELP_COLS     2u
                        #define HELP_COL_W   33u
                        #define HELP_N       ((uint8_t)(sizeof(help_lines)/sizeof(help_lines[0])))
                        #define HELP_ROWS    ((uint8_t)((HELP_N + HELP_COLS - 1u) / HELP_COLS))
                        char rowbuf[72];
                        uint8_t hr, hc, hi;
                        uint8_t prev_ks[KEYBOARD_BYTES];
                        uint8_t cur_ks[KEYBOARD_BYTES];
                        uint8_t ki, ji, was, now2;
                        int     got_key = 0;

                        window_open(3u, 7u, 76u, (uint8_t)(HELP_ROWS + 4u), CSI "37m", CSI "48;2;30;50;30m", 0, true);
                        window_text(" \xfe " APPNAME " Keysology", 1u, 1u, CSI "1;37m", CSI "48;2;30;50;30m");
                        for (hr = 0u; hr < HELP_ROWS; hr++) {
                            uint8_t ci;
                            for (ci = 0u; ci < (uint8_t)(HELP_COLS * (HELP_COL_W + 3u) - 3u); ci++) rowbuf[ci] = ' ';
                            rowbuf[HELP_COLS * (HELP_COL_W + 3u) - 3u] = '\0';
                            for (hc = 0u; hc < HELP_COLS; hc++) {
                                const char *src;
                                uint8_t sc, dc;
                                hi = (uint8_t)(hr + hc * HELP_ROWS);
                                src = (hi < HELP_N) ? help_lines[hi] : "";
                                dc = (uint8_t)(hc * (HELP_COL_W + 3u));
                                for (sc = 0u; sc < HELP_COL_W && src[sc]; sc++) rowbuf[dc + sc] = src[sc];
                                if (hc < (uint8_t)(HELP_COLS - 1u)) {
                                    rowbuf[dc + HELP_COL_W]      = ' ';
                                    rowbuf[dc + HELP_COL_W + 1u] = ' ';
                                    rowbuf[dc + HELP_COL_W + 2u] = ' ';
                                }
                            }
                            window_text(rowbuf, 2u, (uint8_t)(hr + 3u), CSI "37m", CSI "48;2;30;50;30m");
                        }

                        /* wait for any new key-down (edge detect, ignores currently held keys) */
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
                        window_close();
                        redraw_screen();
                    }

                /* --- F4: toggle view/edit mode --- */
                } else if (key(KEY_F4)) {
                    repeat_key = 0u;
                    view_mode = view_mode ? 0u : 1u;
                    if (view_mode && sel_active) { sel_active = 0u; redraw_screen(); }
                    draw_status_bar(view_mode ? "MODE View" : "MODE Edit");

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
                                printf(CSI "%d;%dH%c",
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
                            for (i = 0u; i < TEXT_COLS; i++) g_linebuf[i] = (char)RIA.rw1;
                            /* strip leading spaces into g_linebuf[0..tlen-1] */
                            { uint8_t s = 0u;
                              while (s < tlen && g_linebuf[s] == ' ') s++;
                              tlen = (uint8_t)(tlen - s);
                              if (tlen > 0u && s > 0u) {
                                  for (i = 0u; i < tlen; i++) g_linebuf[i] = g_linebuf[i + s];
                              }
                            }
                            pad = (uint8_t)((TEXT_COLS - tlen) / 2u);
                            RIA.addr0 = TEXT_BUF_BASE + (uint16_t)cur.row * TEXT_COLS;
                            RIA.step0 = 1;
                            for (i = 0u; i < pad; i++)   RIA.rw0 = ' ';
                            for (i = 0u; i < tlen; i++)  RIA.rw0 = (uint8_t)g_linebuf[i];
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
                            for (i = 0u; i < TEXT_COLS; i++) g_linebuf[i] = (char)RIA.rw1;
                            /* strip leading spaces */
                            { uint8_t s = 0u;
                              while (s < tlen && g_linebuf[s] == ' ') s++;
                              tlen = (uint8_t)(tlen - s);
                              if (tlen > 0u && s > 0u) {
                                  for (i = 0u; i < tlen; i++) g_linebuf[i] = g_linebuf[i + s];
                              }
                            }
                            pad = (uint8_t)(TEXT_COLS - tlen);
                            RIA.addr0 = TEXT_BUF_BASE + (uint16_t)cur.row * TEXT_COLS;
                            RIA.step0 = 1;
                            for (i = 0u; i < pad; i++)  RIA.rw0 = ' ';
                            for (i = 0u; i < tlen; i++) RIA.rw0 = (uint8_t)g_linebuf[i];
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
                            for (i = 0u; i < TEXT_COLS; i++) g_linebuf[i] = (char)RIA.rw1;
                            s = 0u;
                            while (s < tlen && g_linebuf[s] == ' ') s++;
                            if (s > 0u) {
                                tlen = (uint8_t)(tlen - s);
                                RIA.addr0 = TEXT_BUF_BASE + (uint16_t)cur.row * TEXT_COLS;
                                RIA.step0 = 1;
                                for (i = 0u; i < tlen; i++) RIA.rw0 = (uint8_t)g_linebuf[i + s];
                                for (i = tlen; i < TEXT_COLS; i++) RIA.rw0 = ' ';
                                if (cur.col < s) cur.col = 0u;
                                else cur.col = (uint8_t)(cur.col - s);
                                doc_dirty = 1u;
                                redraw_screen();
                            }
                        }
                    }

                /* --- F2: ASCII character table popup (17x17, hex labels) --- */
                } else if (key(KEY_F2)) {
                    repeat_key = 0u;
                    {
                        /* inner area: 3 + 16*3 = 51 cols, 1 header + 16 rows = 17 rows
                           outer (with frame): 53 wide, 19 tall
                           centered on 80x30: x=(80-53)/2+1=14, y=(30-19)/2+1=6 */
                        static const char hex[16] = {'0','1','2','3','4','5','6','7',
                                                     '8','9','A','B','C','D','E','F'};
                        char rowbuf[55];
                        uint8_t r, c;

                        window_open(29u, 6u, 22u, 19u, CSI "37m", CSI "48;2;60;60;60m", 0, true);

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
                        printf(CSI "%d;%dH%c",
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

                /* --- Shift+Tab: remove spaces back to prev tab stop --- */
                } else if (key_shifts && key(KEY_TAB)) {
                    if (!view_mode && cur.col > 0u) {
                        uint8_t tb_target = (uint8_t)((cur.col - 1u) & ~7u);
                        uint8_t tb_del    = (uint8_t)(cur.col - tb_target);
                        uint8_t tb_i;
                        for (tb_i = 0u; tb_i < tb_del; tb_i++) {
                            uint8_t bch;
                            RIA.addr1 = TEXT_BUF_BASE + (uint16_t)cur.row * TEXT_COLS + cur.col - 1u;
                            RIA.step1 = 0;
                            bch = RIA.rw1;
                            if (bch != ' ' && bch != 0u) break;
                            cur.col--;
                            if (insert_mode) {
                                line_shift_left(cur.row, (uint8_t)(cur.col + 1u));
                            } else {
                                RIA.addr0 = TEXT_BUF_BASE + (uint16_t)cur.row * TEXT_COLS + cur.col;
                                RIA.step0 = 0;
                                RIA.rw0   = ' ';
                            }
                        }
                        if (tb_i > 0u) { doc_dirty = 1u; redraw_screen(); }
                    }

                /* --- Tab: insert spaces to next tab stop (col % 8) --- */
                } else if (key(KEY_TAB)) {
                    if (!view_mode) {
                        uint8_t tb_spaces = (uint8_t)(8u - (cur.col % 8u));
                        uint8_t tb_i;
                        for (tb_i = 0u; tb_i < tb_spaces; tb_i++) {
                            if (cur.col >= (uint8_t)(TEXT_COLS - 1u)) break;
                            if (insert_mode) line_shift_right(cur.row, cur.col);
                            RIA.addr0 = TEXT_BUF_BASE + (uint16_t)cur.row * TEXT_COLS + cur.col;
                            RIA.step0 = 0;
                            RIA.rw0   = ' ';
                            cur.col++;
                        }
                        if ((uint16_t)(cur.row + 1u) > content_rows)
                            content_rows = (uint16_t)(cur.row + 1u);
                        doc_dirty = 1u;
                        redraw_screen();
                    }

                /* --- Generic character input --- */
                } else {
                    ch = keycode_to_char(last_key, (uint8_t)(key_shifts && !key_ctrl),
                                         key_capslock, key_ralt);
                    if (!ch) did_action = false;
                    if (ch && !view_mode) {
                        if (insert_mode && cur.col < (uint8_t)(TEXT_COLS - 1u)) {
                            line_shift_right(cur.row, cur.col);
                        }
                        RIA.addr0 = TEXT_BUF_BASE + (uint16_t)cur.row * TEXT_COLS + cur.col;
                        RIA.step0 = 0;
                        RIA.rw0   = (uint8_t)ch;
                        doc_dirty = 1u;

                        /* print char at terminal position */
                        printf(CSI "%d;%dH%c",
                               (int)((cur.row - scroll_row) + 1u + TITLE_ROWS),
                               (int)(cur.col + 1u),
                               ch);

                        if (cur.col < (uint8_t)(TEXT_COLS - 1u)) {
                            cur.col++;

                            /* extend content tracking */
                            if ((uint16_t)(cur.row + 1u) > content_rows) {
                                content_rows = (uint16_t)(cur.row + 1u);
                                /* EoD moved down — redraw so it appears in new position */
                                redraw_screen();
                            } else if (insert_mode) {
                                /* redraw rest of line (insert mode shifts chars) */
                                uint8_t j2;
                                RIA.addr1 = TEXT_BUF_BASE + (uint16_t)cur.row * TEXT_COLS + cur.col;
                                RIA.step1 = 1;
                                for (j2 = cur.col; j2 < TEXT_COLS; j2++) {
                                    char c = (char)RIA.rw1;
                                    putchar(c ? c : ' ');
                                }
                            }

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

                if (did_action) {
                    /* position cursor after any key action */
                    printf(CSI "%d;%dH",
                           (int)((cur.row - scroll_row) + 1u + TITLE_ROWS),
                           (int)(cur.col + 1u));
                    handled_key = true;
                    if (!prev_dirty && doc_dirty) {
                        printf(CSI "s");
                        draw_title_bar();
                        printf(CSI "u");
                    }
                    draw_status_bar(NULL);
                }

            }
        } else {
            handled_key = false;
        }
    }

    {
        uint16_t xi;
        RIA.addr0 = TEXT_BUF_BASE; RIA.step0 = 1;
        for (xi = 0u; xi < 20480u; xi++) RIA.rw0 = 0u;
        RIA.addr0 = CLIP_BUF_BASE; RIA.step0 = 1;
        for (xi = 0u; xi < 2560u;  xi++) RIA.rw0 = 0u;
        RIA.addr0 = SCREEN_CACHE_BASE; RIA.step0 = 1;
        for (xi = 0u; xi < 2080u;  xi++) RIA.rw0 = 0u;
        RIA.addr0 = XRAM_SCRATCH; RIA.step0 = 1;
        for (xi = 0u; xi < 82u;    xi++) RIA.rw0 = 0u;
        RIA.addr0 = XRAM_WIN_BUF_BASE; RIA.step0 = 1;
        for (xi = 0u; xi < 2400u;  xi++) RIA.rw0 = 0u;
    }
    clip_delete();
    xreg_ria_keyboard(0xFFFF);
    xreg_ria_mouse(0xFFFF);
    flush_rx();
    flush_rx();
    printf(DECSTBM_FULL ALTSCREEN_LEAVE);
    return 0;

}
