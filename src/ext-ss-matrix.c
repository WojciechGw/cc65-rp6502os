/*
 * CharNoise Screensaver
 * for Picocomputer OS Shell
 * Copyright (c) 2026 WojciechGw
 */

#include "commons.h"

#define APPVER "20260403.1532"

#define SCREEN_COLS  80
#define SCREEN_ROWS  29
#define N_DROPS      50

/* ---- keyboard ----------------------------------------------------------- */
#define KEYBOARD_INPUT 0xFFE0
#define KEYBOARD_BYTES 32
static uint8_t keystates[KEYBOARD_BYTES];
#define key(code) (keystates[code >> 3] & (1 << (code & 7)))
#define RX_READY (RIA.ready & RIA_READY_RX_BIT)

/* ---- drop state --------------------------------------------------------- */
typedef struct {
    uint8_t col;
    int8_t  head;   /* current head row; negative = above screen */
    uint8_t len;    /* trail length (6-19) */
    uint8_t speed;  /* vsyncs between steps (1=fastest) */
    uint8_t tick;   /* vsync counter */
} drop_t;

static drop_t drops[N_DROPS];

/* ---- character set ------------------------------------------------------ */

#define SET2

#ifdef SET0
static const char MCHARS[] =
    "\x03\x04\x06\x07\x08";
#endif

#ifdef SET1
static const char MCHARS[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "0123456789\x60"
    "!@#$%&*+-=?/|<>[]{}";
#endif

#ifdef SET2
static const char MCHARS[] = 
    "\xe0\xe1\xe2\xe3\xe4\xe5\xe6\xe7\xe8\xe9\xea\xeb\xec\xed\xee"
    "\xf0\xf1\xf7\xf8"
    "\xb0\xb1\xb2";

#endif

#ifdef SET3
static const char MCHARS[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "0123456789"
    "!@#$%&*+-=?/|<>[]{}"
    "\xe0\xe1\xe2\xe3\xe4\xe5\xe6\xe7\xe8\xe9\xea\xeb\xec\xed\xee"
    "\xf0\xf1\xf7\xf8"
    "\xb0\xb1\xb2";
#endif

#define N_MCHARS (sizeof(MCHARS) - 1)

/* ---- ANSI color shortcuts ----------------------------------------------- */
#define C_HEAD  "\x1b[1;37m"   /* bright white  - tip of drop  */
#define C_NEAR  "\x1b[1;32m"   /* bright green  - fresh trail  */
#define C_MID   "\x1b[32m"     /* normal green  - mid trail    */
#define C_FAR   "\x1b[90m"     /* dark gray     - old trail    */
#define C_RESET "\x1b[0m"

/* ---- helpers ------------------------------------------------------------ */
static void goto_pos(uint8_t col, uint8_t row)
{
    printf("\x1b[%d;%dH", (int)(row + 1), (int)(col + 1));
}

static char rnd_char(void)
{
    return MCHARS[(uint16_t)rand() % N_MCHARS];
}

static void drop_init(uint8_t i)
{
    drops[i].col   = (uint8_t)rand() % SCREEN_COLS;
    drops[i].len   = 6 + (uint8_t)rand() % 10;
    drops[i].speed = 8; // 1 + (uint8_t)rand() % 8;
    drops[i].tick  = (uint8_t)rand() % drops[i].speed;
    /* stagger: start at random height above screen */
    drops[i].head  = -(int8_t)((uint8_t)rand() % SCREEN_ROWS);
}

static void flush_rx(void)
{
    int c;
    while (RX_READY) c = RIA.rx;
    (void)c;
}

/* ---- main --------------------------------------------------------------- */
int main(int argc, char **argv)
{
    uint8_t i;
    uint8_t v;
    int     h, l;

    if (argc == 1 && strcmp(argv[0], "/?") == 0) {
        printf(NEWLINE
               "Matrix Rain Screensaver for Picocomputer" NEWLINE
               "version " APPVER NEWLINE
               NEWLINE
               "press and hold both Shift keys to EXIT" NEWLINE
               NEWLINE);
        return 0;
    }

    /* seed RNG from clock */
    _randomize();

    /* initialise all drop streams */
    for (i = 0; i < N_DROPS; i++)
        drop_init(i);

    /* full terminal reset, hide cursor, clear screen, black background */
    printf(CSI_RESET CSI_CURSOR_HIDE ANSI_CLS "\x1b[40m");

    xreg_ria_keyboard(KEYBOARD_INPUT);
    v = RIA.vsync;

    while (1) {

        /* wait for next vertical sync */
        if (RIA.vsync == v) continue;
        v = RIA.vsync;

        /* --- read keyboard --- */
        RIA.step0 = 0;
        for (i = 0; i < KEYBOARD_BYTES; i++) {
            RIA.addr0 = KEYBOARD_INPUT + i;
            keystates[i] = RIA.rw0;
        }

        /* both Shift keys held = exit */
        if (key(KEY_LEFTSHIFT) && key(KEY_RIGHTSHIFT)) break;

        /* --- update and draw each drop stream --- */
        for (i = 0; i < N_DROPS; i++) {

            drops[i].tick++;
            if (drops[i].tick < drops[i].speed) continue;
            drops[i].tick = 0;
            drops[i].head++;

            h = (int)drops[i].head;
            l = (int)drops[i].len;

            /* tip: bright white */
            if (h >= 0 && h < SCREEN_ROWS) {
                goto_pos(drops[i].col, (uint8_t)h);
                printf(C_HEAD "%c", rnd_char());
            }

            /* 1 row back: bright green, fresh random char */
            if (h - 1 >= 0 && h - 1 < SCREEN_ROWS) {
                goto_pos(drops[i].col, (uint8_t)(h - 1));
                printf(C_NEAR "%c", rnd_char());
            }

            /* 2-3 rows back: normal green, still flickering */
            if (h - 2 >= 0 && h - 2 < SCREEN_ROWS) {
                goto_pos(drops[i].col, (uint8_t)(h - 2));
                printf(C_MID "%c", rnd_char());
            }
            if (h - 3 >= 0 && h - 3 < SCREEN_ROWS) {
                goto_pos(drops[i].col, (uint8_t)(h - 3));
                printf(C_MID "%c", rnd_char());
            }

            /* ~mid trail: dim/dark — occasional flicker */
            if (h - 5 >= 0 && h - 5 < SCREEN_ROWS) {
                goto_pos(drops[i].col, (uint8_t)(h - 5));
                printf(C_FAR "%c", rnd_char());
            }

            /* erase the tail cell */
            if (h - l >= 0 && h - l < SCREEN_ROWS) {
                goto_pos(drops[i].col, (uint8_t)(h - l));
                printf(C_RESET " ");
            }

            /* when the whole stream has left the screen, recycle */
            if (h - l >= SCREEN_ROWS)
                drop_init(i);
        }
    }

    flush_rx();
    printf(CSI_RESET CSI_CURSOR_SHOW);
    return 0;
}
