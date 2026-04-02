/*
 * Noise / Static Screensaver
 * for Picocomputer OS Shell
 * Copyright (c) 2026 WojciechGw
 */

#include "commons.h"

#define APPVER "20260402.2141"

#define SCREEN_COLS     80
#define SCREEN_ROWS     30
#define UPDATES_PER_FRAME 80   /* cells refreshed per vsync */

/* ---- keyboard ----------------------------------------------------------- */
#define KEYBOARD_INPUT 0xFFE0
#define KEYBOARD_BYTES 32
static uint8_t keystates[KEYBOARD_BYTES];
#define key(code) (keystates[code >> 3] & (1 << (code & 7)))
#define RX_READY (RIA.ready & RIA_READY_RX_BIT)

/* ---- character set ------------------------------------------------------ */
/* CP437: printable ASCII + Greek/math + shade blocks                         */

#define SET2

#ifdef SET1
static const char MCHARS[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "0123456789"
    "!@#$%&*+-=?/|<>[]{}"
    "\xe0\xe1\xe2\xe3\xe4\xe5\xe6\xe7\xe8\xe9\xea\xeb\xec\xed\xee"
    "\xf0\xf1\xf7\xf8"
    " \xb0 \xb1 \xb2 ";
#endif

#ifdef SET2
static const char MCHARS[] = "\xb0\xb1\xb2";
#endif

#define N_MCHARS (sizeof(MCHARS) - 1)

/* ---- color table -------------------------------------------------------- */
/* picked randomly per cell to give depth illusion                            */
static const char * const COLORS[] = {
    "\x1b[1;37m",   /* bright white  */
    "\x1b[1;32m",   /* bright green  */
    "\x1b[32m",     /* normal green  */
    "\x1b[90m",     /* dark gray     */

};
#define N_COLORS (sizeof(COLORS) / sizeof(COLORS[0]))

/* ---- helpers ------------------------------------------------------------ */
static void goto_pos(uint8_t col, uint8_t row)
{
    printf("\x1b[%d;%dH", (int)(row + 1), (int)(col + 1));
}

static char rnd_char(void)
{
    return MCHARS[rand() % N_MCHARS];
}

static void flush_rx(void)
{
    int c;
    while (RX_READY) c = RIA.rx;
    (void)c;
}

/* ---- initial fill ------------------------------------------------------- */
static void fill_screen(void)
{
    uint8_t row, col;
    printf("\x1b[H");   /* cursor to top-left */
    for (row = 0; row < SCREEN_ROWS; row++) {
        printf(COLORS[rand() % N_COLORS]);
        for (col = 0; col < SCREEN_COLS; col++)
            putchar(rnd_char());
    }
}

/* ---- main --------------------------------------------------------------- */
int main(int argc, char **argv)
{
    uint8_t i, v;
    uint8_t col, row;

    if (argc == 1 && strcmp(argv[0], "/?") == 0) {
        printf(NEWLINE
               "Noise Screensaver for Picocomputer" NEWLINE
               "version " APPVER NEWLINE
               NEWLINE
               "press and hold both Shift keys to exit" NEWLINE
               NEWLINE);
        return 0;
    }

    _randomize();

    fill_screen();

    printf(CSI_RESET CSI_CURSOR_HIDE ANSI_CLS "\x1b[40m");


    xreg_ria_keyboard(KEYBOARD_INPUT);
    v = RIA.vsync;

    while (1) {

        if (RIA.vsync == v) continue;
        v = RIA.vsync;

        /* --- read keyboard --- */
        RIA.step0 = 0;
        for (i = 0; i < KEYBOARD_BYTES; i++) {
            RIA.addr0 = KEYBOARD_INPUT + i;
            keystates[i] = RIA.rw0;
        }

        if (key(KEY_LEFTSHIFT) && key(KEY_RIGHTSHIFT)) break;

        /* --- update random cells --- */
        for (i = 0; i < UPDATES_PER_FRAME; i++) {
            col = (uint8_t)(rand() % SCREEN_COLS);
            row = (uint8_t)(rand() % SCREEN_ROWS);
            goto_pos(col, row);
            printf("%s%c", COLORS[rand() % N_COLORS], rnd_char());
        }
    }

    flush_rx();
    printf(CSI_RESET CSI_CURSOR_SHOW);
    return 0;
}
