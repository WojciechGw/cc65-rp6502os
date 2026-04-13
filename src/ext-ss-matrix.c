/*
 * Matrix Rain Screensaver — Mode 1 (Character)
 * for razemOS (65C02S native OS for Picocomputer 6502) 
 * Copyright (c) 2026 WojciechGw
 *
 * Writes directly to XRAM via RIA.rw0 — no ANSI/printf overhead.
 * Full-screen canvas: 80 cols x 60 rows (640x480 with 8x8 font).
 * Font must already be loaded at 0xF700 by the OS Shell (InitTerminalFont).
 */

#include "commons.h"

#define APPVER "20260413.2051"

#define FONTDIR "ROM:"

/* ---- RP6502 VGA constants (from shell.h — not in rp6502.h) -------------- */
#define GFX_CANVAS_640x480          3
#define GFX_MODE_CHARACTER          1
#define GFX_PLANE_0                 0
#define GFX_CHARACTER_FONTSIZE8x8   0
#define GFX_CHARACTER_bpp1          0 // max XRAM [bytes]: 80 x 60 x 1
#define GFX_CHARACTER_bpp4r         1 // max XRAM [bytes]: 80 x 60 x 2
#define GFX_CHARACTER_bpp4          2 // max XRAM [bytes]: 80 x 60 x 2
#define GFX_CHARACTER_bpp8          3 // max XRAM [bytes]: 80 x 60 x 3
#define GFX_CHARACTER_bpp16         4 // max XRAM [bytes]: 80 x 60 x 6

/* ---- canvas ------------------------------------------------------------- */
#define CANVAS_COLS       80            /* 640px / 8px font                  */
#define CANVAS_ROWS       60            /* 480px / 8px font                  */
#define CANVAS_DATA       0x2000u       /* XRAM base: cell data (2 B/cell)   */
#define CANVAS_DATA_SIZE  0x2580u
#define CANVAS_STRUCT     0xFEF0u       /* XRAM base: vga_mode1_config_t     */
#define CANVAS_STRUCT_END 0xFF00u       /* XRAM end of struct (used in xreg) */
#define CANVAS_FONT       0xF700u       /* XRAM: 8x8 font loaded by shell    */
#define CANVAS_PAL        0xFFFFu       /* system default palette            */

/* ---- keyboard ----------------------------------------------------------- */
#define KEYBOARD_INPUT 0xFFE0
#define KEYBOARD_BYTES 32
static uint8_t keystates[KEYBOARD_BYTES];
#define key(code) (keystates[code >> 3] & (1 << (code & 7)))
#define RX_READY (RIA.ready & RIA_READY_RX_BIT)

/* ---- character set ------------------------------------------------------ */
static const char MCHARS[] =
    "\xd0\xd1\xd2\xd3\xd4\xd5\xd6\xd7\xd8\xd9\xda\xdb\xdc\xdd\xde\xdf";
#define N_MCHARS (sizeof(MCHARS) - 1)

/* ---- drop state --------------------------------------------------------- */
#define N_DROPS 500u

typedef struct {
    uint8_t col;
    int8_t  head;   /* current head row; negative = above screen */
    uint8_t len;    /* trail length (6-19) */
    uint8_t speed;  /* vsyncs between steps (1=fastest) */
    uint8_t tick;   /* vsync counter */
} drop_t;

static drop_t drops[N_DROPS];

/* ---- helpers ------------------------------------------------------------ */
char *filename[50] = {"                                                  "};

int FontLoadBMP(char *font, uint16_t bank){

    int fd = 0;
    uint16_t i = 0;
    static unsigned int address;
    address = CANVAS_FONT + (0x800 * bank);

    sprintf(*filename, "%s[font8x8]%s.bmp", FONTDIR, font);
    fd = open(*filename, O_RDONLY);
    if(fd >= 0 ){
        lseek(fd,0x003E,SEEK_SET);
        for(i = 16; i > 0; i--){
            read_xram(address + 0x700 + ((i - 1) * 0x10), 0x10, fd);
            read_xram(address + 0x600 + ((i - 1) * 0x10), 0x10, fd);
            read_xram(address + 0x500 + ((i - 1) * 0x10), 0x10, fd);
            read_xram(address + 0x400 + ((i - 1) * 0x10), 0x10, fd);
            read_xram(address + 0x300 + ((i - 1) * 0x10), 0x10, fd);
            read_xram(address + 0x200 + ((i - 1) * 0x10), 0x10, fd);
            read_xram(address + 0x100 + ((i - 1) * 0x10), 0x10, fd);
            read_xram(address + 0x000 + ((i - 1) * 0x10), 0x10, fd);
        }
        close(fd);
        return 0;
    } else {
        printf("ERROR: reading %s file %i" NEWLINE, *filename, fd);
        return -1;
    }

}

void InitFonts(){

    uint16_t i = 0;
    /* clean up font bank memory */
    RIA.addr0 = CANVAS_FONT;
    RIA.step0 = 1;
    for(i = 0; i < 0x800; i++){
        RIA.rw0 = 0;
    }

    FontLoadBMP("experiment2", 0);

}

static char rnd_char(void)
{
    return MCHARS[rand() % N_MCHARS];
}

static void draw_cell(uint8_t row, uint8_t col, char ch, uint8_t fg)
{
    RIA.addr0 = CANVAS_DATA + 2 * ((uint16_t)row * CANVAS_COLS + col);
    RIA.step0 = 1;
    RIA.rw0 = (uint8_t)ch;
    RIA.rw0 = (BLACK << 4) | fg;
}

static void clear_screen(void)
{
    uint8_t row, col;
    for (row = 0; row < CANVAS_ROWS; row++)
        for (col = 0; col < CANVAS_COLS; col++)
            draw_cell(row, col, ' ', BLACK);
}

static void drop_init(uint16_t i)
{
    drops[i].col   = (uint8_t)(rand() % CANVAS_COLS);
    drops[i].len   = 6 + (uint8_t)(rand() % 14);
    drops[i].speed = 1 + (uint8_t)(rand() % 8);
    drops[i].tick  = (uint8_t)(rand() % drops[i].speed);
    /* stagger: start at random height above screen */
    drops[i].head  = -(int8_t)((uint8_t)(rand() % CANVAS_ROWS));
}

static void setup_canvas(void)
{
    uint8_t font_bpp_opt = GFX_CHARACTER_FONTSIZE8x8 | GFX_CHARACTER_bpp4;

    InitFonts();

    xreg_vga_canvas(GFX_CANVAS_640x480);

    xram0_struct_set(CANVAS_STRUCT, vga_mode1_config_t, x_wrap,           false);
    xram0_struct_set(CANVAS_STRUCT, vga_mode1_config_t, y_wrap,           false);
    xram0_struct_set(CANVAS_STRUCT, vga_mode1_config_t, x_pos_px,         0);
    xram0_struct_set(CANVAS_STRUCT, vga_mode1_config_t, y_pos_px,         0);
    xram0_struct_set(CANVAS_STRUCT, vga_mode1_config_t, width_chars,      CANVAS_COLS);
    xram0_struct_set(CANVAS_STRUCT, vga_mode1_config_t, height_chars,     CANVAS_ROWS);
    xram0_struct_set(CANVAS_STRUCT, vga_mode1_config_t, xram_data_ptr,    CANVAS_DATA);
    xram0_struct_set(CANVAS_STRUCT, vga_mode1_config_t, xram_palette_ptr, CANVAS_PAL);
    xram0_struct_set(CANVAS_STRUCT, vga_mode1_config_t, xram_font_ptr,    CANVAS_FONT);

    /* all scanlines = Mode 1 Character */
    xreg(1, 0, 1, GFX_MODE_CHARACTER, font_bpp_opt, CANVAS_STRUCT, GFX_PLANE_0);
}

static void restore_canvas(void)
{
    xreg_vga_canvas(GFX_CANVAS_640x480);
    xreg(1, 0, 1, 0);
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
    uint8_t i, v;
    uint16_t di;
    int h, l;

    if (argc == 1 && strcmp(argv[0], "/?") == 0) {
        printf(NEWLINE
               "Matrix Rain Screensaver for Picocomputer" NEWLINE
               "version " APPVER NEWLINE
               NEWLINE
               "Mode 1 Character — 80x60, direct XRAM" NEWLINE
               NEWLINE
               "press ESC to exit" NEWLINE
               NEWLINE);
        return 0;
    }

    _randomize();

    /* initialise all drop streams */
    for (di = 0; di < N_DROPS; di++)
        drop_init(di);

    clear_screen();
    setup_canvas();

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

        if (key(KEY_ESC)) break;

        /* --- update and draw each drop stream --- */
        for (di = 0; di < N_DROPS; di++) {

            drops[di].tick++;
            if (drops[di].tick < drops[di].speed) continue;
            drops[di].tick = 0;
            drops[di].head++;

            h = (int)drops[di].head;
            l = (int)drops[di].len;

            /* head: bright white */
            if (h >= 0 && h < CANVAS_ROWS)
                draw_cell((uint8_t)h, drops[di].col, rnd_char(), WHITE);

            /* h-1: bright green, fresh */
            if (h - 1 >= 0 && h - 1 < CANVAS_ROWS)
                draw_cell((uint8_t)(h - 1), drops[di].col, rnd_char(), GREEN);

            /* h-2, h-3: normal green */
            if (h - 2 >= 0 && h - 2 < CANVAS_ROWS)
                draw_cell((uint8_t)(h - 2), drops[di].col, rnd_char(), GREEN);
            if (h - 3 >= 0 && h - 3 < CANVAS_ROWS)
                draw_cell((uint8_t)(h - 3), drops[di].col, rnd_char(), DARK_GREEN);

            /* h-5: dim tail */
            if (h - 5 >= 0 && h - 5 < CANVAS_ROWS)
                draw_cell((uint8_t)(h - 5), drops[di].col, rnd_char(), DARK_GRAY);

            /* erase the tail cell */
            if (h - l >= 0 && h - l < CANVAS_ROWS)
                draw_cell((uint8_t)(h - l), drops[di].col, ' ', BLACK);

            /* when the whole stream has left the screen, recycle */
            if (h - l >= CANVAS_ROWS)
                drop_init(di);
        }
    }

    flush_rx();
    restore_canvas();

    return 0;
}
