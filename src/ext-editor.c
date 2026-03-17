#include <rp6502.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <fcntl.h>
#include "commons.h"

#define APPVER "20260317.1050"

/* --- XRAM register addresses --- */
#define XRAM_STRUCT_GFX_MENU     0xFF00
#define XRAM_STRUCT_GFX_EDITOR   0xFF10
#define XRAM_STRUCT_SYS_KEYBOARD 0xFF20
#define XRAM_STRUCT_SYS_MOUSE    0xFF40

/* --- GFX canvas and mode constants --- */
#define GFX_CANVAS_640x480      0b00000011
#define GFX_MODE1_FONTSIZE8     0b00000000
#define GFX_MODE1_FONTSIZE16    0b00001000
#define GFX_MODE1_COLORS_16     0b00000010

/* --- 16-color palette --- */
#define GFX_MODE1_COLOR_BLACK       0b0000
#define GFX_MODE1_COLOR_DARKRED     0b0001
#define GFX_MODE1_COLOR_DARKGREEN   0b0010
#define GFX_MODE1_COLOR_DARKYELLOW  0b0011
#define GFX_MODE1_COLOR_DARKBLUE    0b0100
#define GFX_MODE1_COLOR_DARKMAGENTA 0b0101
#define GFX_MODE1_COLOR_DARKCYAN    0b0110
#define GFX_MODE1_COLOR_GRAY        0b0111
#define GFX_MODE1_COLOR_DARKGRAY    0b1000
#define GFX_MODE1_COLOR_RED         0b1001
#define GFX_MODE1_COLOR_GREEN       0b1010
#define GFX_MODE1_COLOR_YELLOW      0b1011
#define GFX_MODE1_COLOR_MAGENTA     0b1100
#define GFX_MODE1_COLOR_CYAN        0b1101
#define GFX_MODE1_COLOR_WHITE       0b1111

/* --- editor / menu colors --- */
#define MENU_COLOR_BG        GFX_MODE1_COLOR_DARKGREEN
#define MENU_COLOR_FG        GFX_MODE1_COLOR_WHITE
#define MENU_PROMPT_BG       GFX_MODE1_COLOR_DARKYELLOW
#define MENU_PROMPT_FG       GFX_MODE1_COLOR_BLACK
#define EDITOR_COLOR_BG      GFX_MODE1_COLOR_DARKGRAY
#define EDITOR_COLOR_FG      GFX_MODE1_COLOR_WHITE
#define EDITOR_HILITE_BG     GFX_MODE1_COLOR_DARKBLUE
#define EDITOR_HILITE_FG     GFX_MODE1_COLOR_YELLOW
#define EDITOR_COLOR_ATTR    ((uint8_t)((EDITOR_COLOR_BG << 4) | EDITOR_COLOR_FG))

/* --- keyboard --- */
#define KEYBOARD_BYTES 32
uint8_t keystates[KEYBOARD_BYTES] = {0};
#define key(code) (keystates[(code) >> 3] & (1u << ((code) & 7)))

/* --- cursor --- */
struct Cursor {
    uint8_t  row;
    uint8_t  col;
    uint16_t blink_counter;
    bool     blink_state;
};
static struct Cursor cur;

/* --- screen globals --- */
static uint16_t screenaddr_base      = 0x0000u;
static uint16_t screenaddr_base_menu = 0xA000u;
static uint16_t screenaddr_current   = 0x0000u;
static uint16_t screenaddr_max       = 0x0000u;
static uint16_t content_rows         = 0u;

/* --- file and search state --- */
static char current_filename[64];
static char search_pattern[32];
static char g_linebuf[82];

/* --- Insert/Overwrite mode and clipboard --- */
static uint8_t insert_mode = 1u;   /* 1=Insert, 0=Overwrite */
static uint8_t sel_active  = 0u;   /* selection mark set */
static uint8_t sel_row     = 0u;   /* mark row */
static uint8_t sel_col     = 0u;   /* mark col */
static char    clipboard[81];      /* clipboard buffer (max 80 chars + \0) */
static uint8_t clip_len    = 0u;   /* number of chars in clipboard */
static char    line_tmp[80];       /* temp buffer for line-shift operations */

/* ================================================================
   keycode_to_char: USB HID keycode -> ASCII
   Handles A-Z, 0-9, space and filename/editor punctuation.
   Returns 0 for unmapped keys.
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
   menu_draw_row: draws text to one row of the menu bar (XRAM).
   Pads with spaces to 80 columns.
   ================================================================ */
static void menu_draw_row(uint8_t row, const char *text, uint8_t bg, uint8_t fg)
{
    uint8_t i;
    uint8_t attr = (uint8_t)((bg << 4) | fg);
    RIA.addr0 = screenaddr_base_menu + (uint16_t)row * 160u;
    RIA.step0 = 1;
    for (i = 0u; text[i] && i < 80u; i++) {
        RIA.rw0 = (uint8_t)text[i];
        RIA.rw0 = attr;
    }
    while (i < 80u) {
        RIA.rw0 = ' ';
        RIA.rw0 = attr;
        i++;
    }
}

/* ================================================================
   draw_menu_bar: restores standard 3-row menu bar.
   Row 0: key shortcuts
   Row 1: [INS]/[OVR] + status message or current filename
   Row 2: MARK SET indicator (when sel_active)
   ================================================================ */
static void draw_menu_bar(const char *status)
{
    uint8_t     i, j;
    const char *mode_str;
    const char *info;

    mode_str = insert_mode ? "[INS] " : "[OVR] ";
    info     = status ? status : (current_filename[0] ? current_filename : "");

    menu_draw_row(0,
        "Ins=Mode ^K=Mark ^C=Copy ^X=Cut ^V=Paste "
        "F5=Open F6=Save F7=Find ESC=Exit",
        MENU_COLOR_BG, MENU_COLOR_FG);

    /* compose "[INS/OVR] info..." in g_linebuf */
    for (i = 0u; mode_str[i]; i++) g_linebuf[i] = mode_str[i];
    for (j = 0u; info[j] && i < 80u; j++, i++) g_linebuf[i] = info[j];
    g_linebuf[i] = 0;
    menu_draw_row(1, g_linebuf, MENU_COLOR_BG, GFX_MODE1_COLOR_YELLOW);

    menu_draw_row(2,
        sel_active ? "MARK SET  (^C=copy  ^X=cut  ^K=clear)" : "",
        MENU_COLOR_BG, GFX_MODE1_COLOR_DARKYELLOW);
}

/* ================================================================
   menu_input: blocking text-input dialog in menu rows 1-2.
   Uses edge-detection (press, not held) for reliable single-keystroke.
   Returns 1 on Enter, 0 on Esc.
   ================================================================ */
static int menu_input(const char *prompt, char *buf, uint8_t maxlen)
{
    uint8_t prev_ks[KEYBOARD_BYTES];
    uint8_t cur_ks[KEYBOARD_BYTES];
    uint8_t len, k, j, code, was, now;
    uint8_t shift, caps;
    int     done, result;
    char    ch;

    len    = 0u;
    done   = 0;
    result = 0;
    buf[0] = 0;

    /* snapshot current state to avoid instant trigger on held keys */
    for (k = 0u; k < KEYBOARD_BYTES; k++) prev_ks[k] = keystates[k];

    menu_draw_row(1, prompt,  MENU_PROMPT_BG, MENU_PROMPT_FG);
    menu_draw_row(2, "",      MENU_COLOR_BG,  GFX_MODE1_COLOR_WHITE);

    while (!done) {
        /* read fresh keyboard state from XRAM */
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
                was  = (prev_ks[k] >> j) & 1u;
                now  = (cur_ks [k] >> j) & 1u;
                if (!was && now) {                  /* key just pressed */
                    code = (uint8_t)((k << 3) | j);
                    if (code == KEY_ENTER || code == KEY_KPENTER) {
                        result = 1; done = 1;
                    } else if (code == KEY_ESC) {
                        result = 0; done = 1;
                    } else if (code == KEY_BACKSPACE) {
                        if (len > 0u) {
                            len--;
                            buf[len] = 0;
                            menu_draw_row(2, buf, MENU_COLOR_BG, GFX_MODE1_COLOR_WHITE);
                        }
                    } else {
                        ch = keycode_to_char(code, shift, caps);
                        if (ch && len < (uint8_t)(maxlen - 1u)) {
                            buf[len++] = ch;
                            buf[len]   = 0;
                            menu_draw_row(2, buf, MENU_COLOR_BG, GFX_MODE1_COLOR_WHITE);
                        }
                    }
                }
            }
            prev_ks[k] = cur_ks[k];
        }
    }

    /* sync keystates so main loop doesn't re-trigger the same key */
    for (k = 0u; k < KEYBOARD_BYTES; k++) keystates[k] = cur_ks[k];
    return result;
}

/* ================================================================
   editor_clear: clears full XRAM canvas (0x0000-0x9FFF) and resets
   cursor, scroll, and content tracking.
   ================================================================ */
static void editor_clear(void)
{
    uint16_t i;

    RIA.addr0 = screenaddr_base;
    RIA.step0 = 1;
    /* 20480 cells * 2 bytes = 40960 bytes (full canvas 0x0000-0x9FFF) */
    for (i = 0u; i < 20480u; i++) {
        RIA.rw0 = ' ';
        RIA.rw0 = EDITOR_COLOR_ATTR;
    }

    cur.row           = 0u;
    cur.col           = 0u;
    cur.blink_counter = 0u;
    cur.blink_state   = false;
    content_rows      = 0u;
    sel_active        = 0u;
    screenaddr_current= screenaddr_base;
    screenaddr_max    = screenaddr_base;
    xram0_struct_set(XRAM_STRUCT_GFX_EDITOR, vga_mode1_config_t, xram_data_ptr, screenaddr_base);
}

/* ================================================================
   load_file: reads a text file from SD into the XRAM canvas.
   Max 256 rows x 80 columns; long lines are silently truncated.
   Returns 1 on success, -1 on error.
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
            if (col > 0u) row++;    /* flush last line if no trailing \n */
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
            } else if (col < 80u) {
                if (need_addr) {
                    RIA.addr0 = screenaddr_base + row * 160u;
                    RIA.step0 = 2;   /* step=2: write chars only, skip color bytes */
                    need_addr = 0u;
                }
                RIA.rw0 = (uint8_t)c;
                col++;
            }
        }
    }

    close(fd);

    content_rows = row;
    if (content_rows > 60u) {
        screenaddr_max = screenaddr_base + (content_rows - 60u) * 160u;
    } else {
        screenaddr_max = screenaddr_base;
    }

    strncpy(current_filename, filename, 63u);
    current_filename[63] = 0;
    cur.row            = 0u;
    cur.col            = 0u;
    screenaddr_current = screenaddr_base;
    xram0_struct_set(XRAM_STRUCT_GFX_EDITOR, vga_mode1_config_t, xram_data_ptr, screenaddr_base);
    return 1;
}

/* ================================================================
   save_file: writes XRAM canvas rows to a text file on SD.
   Trailing spaces on each line are trimmed before writing.
   Returns 1 on success, -1 on error.
   ================================================================ */
static int save_file(const char *filename)
{
    int      fd;
    uint16_t row;
    uint8_t  j, len;

    fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) return -1;

    for (row = 0u; row < content_rows; row++) {
        /* read 80 chars from XRAM, step=2 skips color bytes */
        RIA.addr1 = screenaddr_base + row * 160u;
        RIA.step1 = 2;
        for (j = 0u; j < 80u; j++) g_linebuf[j] = (char)RIA.rw1;

        /* trim trailing spaces */
        len = 80u;
        while (len > 0u && g_linebuf[len - 1u] == ' ') len--;

        g_linebuf[len] = '\n';
        write(fd, g_linebuf, (unsigned)(len + 1u));
    }

    close(fd);

    strncpy(current_filename, filename, 63u);
    current_filename[63] = 0;
    return 1;
}

/* ================================================================
   find_text: searches XRAM canvas for pattern, starting one position
   past the current cursor. Highlights the found text and scrolls.
   Returns 1 if found, 0 if not found.
   ================================================================ */
static int find_text(const char *pattern)
{
    uint8_t  plen, i, col, match;
    uint16_t row, start_row;
    uint8_t  start_col;

    plen = (uint8_t)strlen(pattern);
    if (plen == 0u || plen > 80u) return 0;

    /* start one position past the current cursor */
    if (cur.col + 1u < 80u) {
        start_row = (uint16_t)cur.row;
        start_col = (uint8_t)(cur.col + 1u);
    } else {
        start_row = (uint16_t)cur.row + 1u;
        start_col = 0u;
    }

    for (row = start_row; row < content_rows; row++) {
        /* read row's characters into RAM (step=2 skips color bytes) */
        RIA.addr1 = screenaddr_base + row * 160u;
        RIA.step1 = 2;
        for (i = 0u; i < 80u; i++) g_linebuf[i] = (char)RIA.rw1;

        col = (row == start_row) ? start_col : 0u;

        for (; (uint16_t)col + plen <= 80u; col++) {
            match = 1u;
            for (i = 0u; i < plen; i++) {
                if (g_linebuf[(uint8_t)(col + i)] != pattern[i]) { match = 0u; break; }
            }
            if (match) {
                /* highlight: change color bytes of matched chars */
                RIA.addr0 = screenaddr_base + row * 160u + (uint16_t)col * 2u + 1u;
                RIA.step0 = 2;
                for (i = 0u; i < plen; i++) {
                    RIA.rw0 = (uint8_t)((EDITOR_HILITE_BG << 4) | EDITOR_HILITE_FG);
                }
                /* move cursor to match start */
                cur.row = (uint8_t)row;
                cur.col = col;
                /* scroll so found line appears near center */
                if (row > 30u) {
                    screenaddr_current = screenaddr_base + (row - 30u) * 160u;
                    if (screenaddr_current > screenaddr_max)
                        screenaddr_current = screenaddr_max;
                } else {
                    screenaddr_current = screenaddr_base;
                }
                xram0_struct_set(XRAM_STRUCT_GFX_EDITOR, vga_mode1_config_t,
                                 xram_data_ptr, screenaddr_current);
                return 1;
            }
        }
    }
    return 0;
}

/* ================================================================
   line_shift_right: shifts chars at positions col..78 one slot right
   (col+1..79). The char at position 79 is lost.
   Used for Insert-mode character typing.
   ================================================================ */
static void line_shift_right(uint8_t row, uint8_t col)
{
    uint8_t i, n;

    n = (uint8_t)(79u - col);   /* chars to shift: col..78, count = 79-col */
    if (n == 0u) return;

    /* read col..78 (chars only, step=2) into line_tmp */
    RIA.addr1 = screenaddr_base + (uint16_t)row * 160u + (uint16_t)col * 2u;
    RIA.step1 = 2;
    for (i = 0u; i < n; i++) line_tmp[i] = (char)RIA.rw1;

    /* write to col+1..79 */
    RIA.addr0 = screenaddr_base + (uint16_t)row * 160u + (uint16_t)(col + 1u) * 2u;
    RIA.step0 = 2;
    for (i = 0u; i < n; i++) RIA.rw0 = (uint8_t)line_tmp[i];
}

/* ================================================================
   line_shift_left: shifts chars at positions from_col..79 one slot
   left (from_col-1..78). Writes a space at position 79.
   Used for Insert-mode Backspace (from_col = cursor_col + 1).
   ================================================================ */
static void line_shift_left(uint8_t row, uint8_t from_col)
{
    uint8_t i, n;

    if (from_col == 0u) return;   /* safety: nothing to the left */
    n = (uint8_t)(80u - from_col); /* chars from_col..79, count = 80-from_col */

    /* read from_col..79 into line_tmp */
    RIA.addr1 = screenaddr_base + (uint16_t)row * 160u + (uint16_t)from_col * 2u;
    RIA.step1 = 2;
    for (i = 0u; i < n; i++) line_tmp[i] = (char)RIA.rw1;

    /* write to from_col-1..79-1 */
    RIA.addr0 = screenaddr_base + (uint16_t)row * 160u + (uint16_t)(from_col - 1u) * 2u;
    RIA.step0 = 2;
    for (i = 0u; i < n; i++) RIA.rw0 = (uint8_t)line_tmp[i];

    /* clear last position (space at col 79) */
    RIA.addr0 = screenaddr_base + (uint16_t)row * 160u + 79u * 2u;
    RIA.step0 = 0;
    RIA.rw0   = ' ';
}

/* ================================================================
   do_copy: copies selection (same row) or current line to clipboard.
   ================================================================ */
static void do_copy(void)
{
    uint8_t i, start_col, end_col;

    if (sel_active && sel_row == cur.row) {
        /* character-level copy within same row */
        if (sel_col < cur.col) { start_col = sel_col; end_col = cur.col; }
        else                   { start_col = cur.col; end_col = sel_col; }
        clip_len = (uint8_t)(end_col - start_col);
        if (clip_len > 80u) clip_len = 80u;
        RIA.addr1 = screenaddr_base + (uint16_t)cur.row * 160u + (uint16_t)start_col * 2u;
        RIA.step1 = 2;
        for (i = 0u; i < clip_len; i++) clipboard[i] = (char)RIA.rw1;
    } else {
        /* no same-row selection: copy whole current line (trimmed) */
        RIA.addr1 = screenaddr_base + (uint16_t)cur.row * 160u;
        RIA.step1 = 2;
        for (i = 0u; i < 80u; i++) clipboard[i] = (char)RIA.rw1;
        clip_len = 80u;
        while (clip_len > 0u && clipboard[clip_len - 1u] == ' ') clip_len--;
    }

    clipboard[clip_len] = 0;
    sel_active = 0u;
    draw_menu_bar("Copied.");
}

/* ================================================================
   do_cut: copies to clipboard then clears the source region.
   ================================================================ */
static void do_cut(void)
{
    uint8_t i, was_sel, saved_sel_row, cut_start, cut_end;

    /* save selection state before do_copy() clears sel_active */
    was_sel       = sel_active;
    saved_sel_row = sel_row;
    if (sel_col < cur.col) { cut_start = sel_col; cut_end = cur.col; }
    else                   { cut_start = cur.col; cut_end = sel_col; }

    do_copy();   /* fills clipboard, clears sel_active, calls draw_menu_bar("Copied.") */

    if (was_sel && saved_sel_row == cur.row && cut_start < cut_end) {
        /* clear the selected char range */
        RIA.addr0 = screenaddr_base + (uint16_t)cur.row * 160u + (uint16_t)cut_start * 2u;
        RIA.step0 = 2;
        for (i = cut_start; i < cut_end; i++) RIA.rw0 = ' ';
        cur.col = cut_start;
    } else {
        /* clear entire current line */
        RIA.addr0 = screenaddr_base + (uint16_t)cur.row * 160u;
        RIA.step0 = 2;
        for (i = 0u; i < 80u; i++) RIA.rw0 = ' ';
        cur.col = 0u;
    }
    draw_menu_bar("Cut.");
}

/* ================================================================
   do_paste: inserts or overwrites clipboard text at cursor.
   Insert mode: shifts existing chars right to make room.
   Overwrite mode: overwrites chars starting at cursor.
   ================================================================ */
static void do_paste(void)
{
    uint8_t i, paste_len, save_len;
    uint16_t base;

    if (clip_len == 0u) return;

    paste_len = clip_len;
    if ((uint16_t)cur.col + paste_len > 80u)
        paste_len = (uint8_t)(80u - cur.col);

    base = screenaddr_base + (uint16_t)cur.row * 160u + (uint16_t)cur.col * 2u;

    if (insert_mode) {
        /* chars that survive after the paste (fit within 80-col limit) */
        save_len = (uint8_t)((uint16_t)cur.col + paste_len < 80u
                             ? 80u - cur.col - paste_len : 0u);

        if (save_len > 0u) {
            /* read chars that will be pushed right */
            RIA.addr1 = base;
            RIA.step1 = 2;
            for (i = 0u; i < save_len; i++) line_tmp[i] = (char)RIA.rw1;
        }

        /* write clipboard at cursor position */
        RIA.addr0 = base;
        RIA.step0 = 2;
        for (i = 0u; i < paste_len; i++) RIA.rw0 = (uint8_t)clipboard[i];

        /* write saved chars after the pasted block */
        if (save_len > 0u) {
            RIA.addr0 = base + (uint16_t)paste_len * 2u;
            RIA.step0 = 2;
            for (i = 0u; i < save_len; i++) RIA.rw0 = (uint8_t)line_tmp[i];
        }
    } else {
        /* overwrite mode: write clipboard, no shifting */
        RIA.addr0 = base;
        RIA.step0 = 2;
        for (i = 0u; i < paste_len; i++) RIA.rw0 = (uint8_t)clipboard[i];
    }

    cur.col = (uint8_t)(cur.col + paste_len);
    if (cur.col >= 80u) cur.col = 79u;
    draw_menu_bar(NULL);
}

/* ================================================================
   main
   ================================================================ */
void main(void)
{
    uint8_t temp;
    uint8_t mouse_wheel, mouse_wheel_prev;
    int     mouse_wheel_change;
    bool    handled_key;
    uint8_t k, j, new_key, new_keys, last_key;
    uint8_t key_capslock, key_shifts, key_ctrl;
    int     ok;
    char    ch;

    /* init state */
    cur.row           = 0u;
    cur.col           = 0u;
    cur.blink_counter = 0u;
    cur.blink_state   = false;
    current_filename[0] = 0;
    search_pattern[0]   = 0;
    content_rows        = 0u;
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

    /* configure VGA overlay: menu bar at bottom */
    xram0_struct_set(XRAM_STRUCT_GFX_MENU, vga_mode1_config_t, x_wrap,       false);
    xram0_struct_set(XRAM_STRUCT_GFX_MENU, vga_mode1_config_t, y_wrap,       false);
    xram0_struct_set(XRAM_STRUCT_GFX_MENU, vga_mode1_config_t, x_pos_px,     0);
    xram0_struct_set(XRAM_STRUCT_GFX_MENU, vga_mode1_config_t, y_pos_px,     480-24);
    xram0_struct_set(XRAM_STRUCT_GFX_MENU, vga_mode1_config_t, width_chars,  80);
    xram0_struct_set(XRAM_STRUCT_GFX_MENU, vga_mode1_config_t, height_chars, 3);
    xram0_struct_set(XRAM_STRUCT_GFX_MENU, vga_mode1_config_t, xram_data_ptr,    screenaddr_base_menu);
    xram0_struct_set(XRAM_STRUCT_GFX_MENU, vga_mode1_config_t, xram_palette_ptr, 0xFFFF);
    xram0_struct_set(XRAM_STRUCT_GFX_MENU, vga_mode1_config_t, xram_font_ptr,    0xFFFF);

    /* configure VGA overlay: editor canvas */
    xram0_struct_set(XRAM_STRUCT_GFX_EDITOR, vga_mode1_config_t, x_wrap,       false);
    xram0_struct_set(XRAM_STRUCT_GFX_EDITOR, vga_mode1_config_t, y_wrap,       false);
    xram0_struct_set(XRAM_STRUCT_GFX_EDITOR, vga_mode1_config_t, x_pos_px,     0);
    xram0_struct_set(XRAM_STRUCT_GFX_EDITOR, vga_mode1_config_t, y_pos_px,     0);
    xram0_struct_set(XRAM_STRUCT_GFX_EDITOR, vga_mode1_config_t, width_chars,  80);
    xram0_struct_set(XRAM_STRUCT_GFX_EDITOR, vga_mode1_config_t, height_chars, 60);
    xram0_struct_set(XRAM_STRUCT_GFX_EDITOR, vga_mode1_config_t, xram_data_ptr,    screenaddr_base);
    xram0_struct_set(XRAM_STRUCT_GFX_EDITOR, vga_mode1_config_t, xram_palette_ptr, 0xFFFF);
    xram0_struct_set(XRAM_STRUCT_GFX_EDITOR, vga_mode1_config_t, xram_font_ptr,    0xFFFF);

    xreg_vga_canvas(GFX_CANVAS_640x480);
    xreg_vga_mode(1, GFX_MODE1_FONTSIZE16 | GFX_MODE1_COLORS_16, XRAM_STRUCT_GFX_EDITOR, 1,      0, 480-24);
    xreg_vga_mode(1, GFX_MODE1_FONTSIZE8  | GFX_MODE1_COLORS_16, XRAM_STRUCT_GFX_MENU,   1, 480-24, 480   );
    xreg_ria_keyboard(XRAM_STRUCT_SYS_KEYBOARD);
    xreg_ria_mouse(XRAM_STRUCT_SYS_MOUSE);

    /* clear editor canvas and draw menu bar */
    editor_clear();
    draw_menu_bar(NULL);

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
                if (screenaddr_current >= screenaddr_base + 160u)
                    screenaddr_current -= 160u;
                else
                    screenaddr_current  = screenaddr_base;
            } else {
                /* scroll down */
                if (screenaddr_current + 160u <= screenaddr_max)
                    screenaddr_current += 160u;
                else
                    screenaddr_current  = screenaddr_max;
            }
            xram0_struct_set(XRAM_STRUCT_GFX_EDITOR, vga_mode1_config_t,
                             xram_data_ptr, screenaddr_current);
        }
        mouse_wheel_prev = mouse_wheel;

        /* --- cursor blink (every 60 iterations) --- */
        cur.blink_counter++;
        if (cur.blink_counter == 60u) {
            RIA.addr0 = screenaddr_base + (uint16_t)cur.row * 160u
                                        + (uint16_t)cur.col * 2u + 1u;
            RIA.step0 = 0;
            temp = RIA.rw0;
            RIA.rw0 = (uint8_t)((temp << 4) | (temp >> 4));
            cur.blink_counter = 0u;
            cur.blink_state   = !cur.blink_state;
        }

        /* --- scan keyboard --- */
        for (k = 0u; k < KEYBOARD_BYTES; k++) {
            RIA.addr1 = XRAM_STRUCT_SYS_KEYBOARD + k;
            RIA.step1 = 0;
            new_keys  = RIA.rw1;
            for (j = 0u; j < 8u; j++) {
                uint8_t code = (uint8_t)((k << 3) + j);
                new_key = new_keys & (uint8_t)(1u << j);
                if ((code > 3u) && (new_key != (keystates[k] & (uint8_t)(1u << j)))) {
                    last_key = code;
                }
            }
            keystates[k] = new_keys;
        }

        key_capslock = (uint8_t)(key(KEY_CAPSLOCK)   ? 1u : 0u);
        key_shifts   = (uint8_t)((key(KEY_LEFTSHIFT) || key(KEY_RIGHTSHIFT)) ? 1u : 0u);
        key_ctrl     = (uint8_t)((key(KEY_LEFTCTRL)  || key(KEY_RIGHTCTRL))  ? 1u : 0u);

        /* keystates[0] & 1 == 1 means KEY_NONE is "active" (no real key pressed) */
        if (!(keystates[0] & 1u)) {
            if (!handled_key) {

                /* --- Ctrl+key combos (checked before raw keys) --- */
                if (key_ctrl && key(KEY_K)) {
                    /* set or clear selection mark */
                    if (sel_active && sel_row == cur.row && sel_col == cur.col) {
                        sel_active = 0u;          /* second press at same spot: clear */
                    } else {
                        sel_active = 1u;
                        sel_row    = cur.row;
                        sel_col    = cur.col;
                    }
                    draw_menu_bar(NULL);

                } else if (key_ctrl && key(KEY_C)) {
                    do_copy();

                } else if (key_ctrl && key(KEY_X)) {
                    do_cut();

                } else if (key_ctrl && key(KEY_V)) {
                    do_paste();

                /* --- Insert / Overwrite mode toggle --- */
                } else if (key(KEY_INSERT)) {
                    insert_mode = insert_mode ? 0u : 1u;
                    draw_menu_bar(NULL);

                /* --- File / Search dialogs --- */
                } else if (key(KEY_F5)) {
                    if (menu_input("Open: (e.g. MSC0:/dir/file.txt)", current_filename, 64u)) {
                        ok = load_file(current_filename);
                        draw_menu_bar(ok >= 0 ? current_filename : "ERROR: cannot open file");
                    } else {
                        draw_menu_bar(NULL);
                    }

                } else if (key(KEY_F6)) {
                    if (menu_input("Save: (e.g. MSC0:/dir/file.txt)", current_filename, 64u)) {
                        ok = save_file(current_filename);
                        draw_menu_bar(ok >= 0 ? "Saved." : "ERROR: cannot save file");
                    } else {
                        draw_menu_bar(NULL);
                    }

                } else if (key(KEY_F7)) {
                    if (menu_input("Find:", search_pattern, 32u)) {
                        if (!find_text(search_pattern)) {
                            draw_menu_bar("Not found.");
                        } else {
                            draw_menu_bar(NULL);
                        }
                    } else {
                        draw_menu_bar(NULL);
                    }

                /* --- Cursor movement --- */
                } else if (key(KEY_LEFT)) {
                    RIA.addr0 = screenaddr_base + (uint16_t)cur.row * 160u
                                                + (uint16_t)cur.col * 2u + 1u;
                    RIA.step0 = 0;
                    RIA.rw0   = EDITOR_COLOR_ATTR;
                    if (cur.col > 0u) cur.col--;

                } else if (key(KEY_RIGHT)) {
                    RIA.addr0 = screenaddr_base + (uint16_t)cur.row * 160u
                                                + (uint16_t)cur.col * 2u + 1u;
                    RIA.step0 = 0;
                    RIA.rw0   = EDITOR_COLOR_ATTR;
                    if (cur.col < 79u) cur.col++;

                } else if (key(KEY_UP)) {
                    RIA.addr0 = screenaddr_base + (uint16_t)cur.row * 160u
                                                + (uint16_t)cur.col * 2u + 1u;
                    RIA.step0 = 0;
                    RIA.rw0   = EDITOR_COLOR_ATTR;
                    if (cur.row > 0u) cur.row--;

                } else if (key(KEY_DOWN)) {
                    RIA.addr0 = screenaddr_base + (uint16_t)cur.row * 160u
                                                + (uint16_t)cur.col * 2u + 1u;
                    RIA.step0 = 0;
                    RIA.rw0   = EDITOR_COLOR_ATTR;
                    if (cur.row < 255u) {
                        cur.row++;
                        if ((uint16_t)(cur.row + 1u) > content_rows) {
                            content_rows = (uint16_t)(cur.row + 1u);
                            if (content_rows > 60u)
                                screenaddr_max = screenaddr_base + (content_rows - 60u) * 160u;
                        }
                    }

                /* --- Enter: move to start of next row --- */
                } else if (key(KEY_ENTER) || key(KEY_KPENTER)) {
                    RIA.addr0 = screenaddr_base + (uint16_t)cur.row * 160u
                                                + (uint16_t)cur.col * 2u + 1u;
                    RIA.step0 = 0;
                    RIA.rw0   = EDITOR_COLOR_ATTR;
                    if (cur.row < 255u) {
                        cur.row++;
                        if ((uint16_t)(cur.row + 1u) > content_rows) {
                            content_rows = (uint16_t)(cur.row + 1u);
                            if (content_rows > 60u)
                                screenaddr_max = screenaddr_base + (content_rows - 60u) * 160u;
                        }
                    }
                    cur.col = 0u;

                /* --- Backspace --- */
                } else if (key(KEY_BACKSPACE)) {
                    if (cur.col > 0u) {
                        /* restore color at current cursor pos (un-blink) */
                        RIA.addr0 = screenaddr_base + (uint16_t)cur.row * 160u
                                                    + (uint16_t)cur.col * 2u + 1u;
                        RIA.step0 = 0;
                        RIA.rw0   = EDITOR_COLOR_ATTR;
                        cur.col--;
                        if (insert_mode) {
                            /* delete char: shift remaining chars left */
                            line_shift_left(cur.row, (uint8_t)(cur.col + 1u));
                        } else {
                            /* overwrite mode: just erase char at new col */
                            RIA.addr0 = screenaddr_base + (uint16_t)cur.row * 160u
                                                        + (uint16_t)cur.col * 2u;
                            RIA.step0 = 1;
                            RIA.rw0   = ' ';
                            RIA.rw0   = EDITOR_COLOR_ATTR;
                        }
                    }

                /* --- ESC: exit --- */
                } else if (key(KEY_ESC)) {
                    printf("Bye!\n\n");
                    break;

                /* --- Generic character input --- */
                } else {
                    ch = keycode_to_char(last_key, (uint8_t)(key_shifts && !key_ctrl),
                                         key_capslock);
                    if (ch) {
                        if (insert_mode && cur.col < 79u) {
                            line_shift_right(cur.row, cur.col);
                        }
                        RIA.addr0 = screenaddr_base + (uint16_t)cur.row * 160u
                                                    + (uint16_t)cur.col * 2u;
                        RIA.step0 = 1;
                        RIA.rw0   = (uint8_t)ch;
                        RIA.rw0   = EDITOR_COLOR_ATTR;
                        if (cur.col < 79u) cur.col++;
                        /* extend content tracking */
                        if ((uint16_t)(cur.row + 1u) > content_rows) {
                            content_rows = (uint16_t)(cur.row + 1u);
                            if (content_rows > 60u)
                                screenaddr_max = screenaddr_base + (content_rows - 60u) * 160u;
                        }
                        /* mark new cursor position */
                        RIA.addr0 = screenaddr_base + (uint16_t)cur.row * 160u
                                                    + (uint16_t)cur.col * 2u + 1u;
                        RIA.step0 = 0;
                        RIA.rw0   = EDITOR_COLOR_ATTR;
                    }
                }
                handled_key = true;
            }
        } else {
            handled_key = false;
        }
    }

    printf("\n");
}
