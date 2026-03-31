/*
 * courier-gfx.h
 * Character Mode 1 (8x8, 80x60) graphics primitives for Courier TX/RX.
 * Include once per .c file — all functions are static.
 *
 * Requires:
 *   - commons.h (rp6502.h, colors.h)
 *   - Shell must have been active before launch so its 8x8 font is in XRAM
 *     at CGX_FONT (0xF700), loaded by Shell's InitTerminalFont().
 */

#ifndef COURIER_GFX_H
#define COURIER_GFX_H

#define CGX_MODE_CONSOLE 0
#define CGX_MODE_CHAR    1
#define CGX_MODE_TILE    2
#define CGX_MODE_BITMAP  3
#define CGX_MODE_SPRITE  4

#define CGX_FONT8x8    0b0000
#define CGX_FONT8x16   0b1000
#define CGX_FONT_BPP1  0b0000
#define CGX_FONT_BPP4R 0b0001
#define CGX_FONT_BPP4  0b0010
#define CGX_FONT_BPP8  0b0011
#define CGX_FONT_BPP16 0b0100

// ----- canvas constants --------------------------------------------------
#define CGX_COLS        80u
#define CGX_ROWS        30u
#define CGX_DATA        0x0000u    // XRAM framebuffer start
#define CGX_STRUCT      0xFEF0u    // vga_mode1_config_t written here
#define CGX_STRUCT_END  0xFF00u    // address passed to xreg()
#define CGX_FONT_PTR    0xFFFFu    // system default 8x16 font 
#define CGX_FONT_PAL    0xFFFFu    // system default palette
#define CGX_CANVAS      3          // GFX_CANVAS_640x480
#define CGX_MODE_CHAR   1          // GFX_MODE_CHARACTER
#define CGX_PLANE       0          // GFX_PLANE_0
#define CGX_BPP_OPT     CGX_FONT8x16 | CGX_FONT_BPP4
#define CGX_BAR_WIDTH   70         // progress bar fill characters

// ----- primitives --------------------------------------------------------

static void DrawChar(uint8_t row, uint8_t col, char ch, uint8_t fg, uint8_t bg)
{
    RIA.addr0 = CGX_DATA + 2u * ((uint16_t)row * CGX_COLS + (uint16_t)col);
    RIA.step0 = 1;
    RIA.rw0   = (uint8_t)ch;
    RIA.rw0   = (uint8_t)((bg << 4) | fg);
}

static void DrawText(uint8_t row, uint8_t col,
                     const char *s, uint8_t fg, uint8_t bg)
{
    while (*s && col < CGX_COLS)
        DrawChar(row, col++, *s++, fg, bg);
}

static void ClearLine(uint8_t row, uint8_t fg, uint8_t bg)
{
    uint8_t c;
    for (c = 0; c < (uint8_t)CGX_COLS; c++)
        DrawChar(row, c, ' ', fg, bg);
}

/* Draw progress bar on the given row.
   Format: [XXXXXXXX......] XX%
   filled cells: 0xDB (block), empty: 0xB0 (light shade) */
static void DrawBar(uint8_t row, long done, long total)
{
    int pct, filled, i;
    char nb[5];
    uint8_t col;

    if (total > 0L) {
        pct    = (int)(done * 100L / total);
        filled = (int)(done * (long)CGX_BAR_WIDTH / total);
        if (pct    > 100)            pct    = 100;
        if (filled > CGX_BAR_WIDTH)  filled = CGX_BAR_WIDTH;
    } else {
        pct = 0; filled = 0;
    }

    col = 0;
    DrawChar(row, col++, '[', LIGHT_GRAY,  BLACK);
    for (i = 0;      i < filled;          i++)
        DrawChar(row, col++, '\xdb', GREEN,     BLACK);
    for (i = filled; i < CGX_BAR_WIDTH;   i++)
        DrawChar(row, col++, '\xb0', DARK_GRAY, BLACK);
    DrawChar(row, col++, ']', LIGHT_GRAY,  BLACK);
    DrawChar(row, col++, ' ', LIGHT_GRAY,  BLACK);
    nb[0] = (char)('0' + pct / 100);
    nb[1] = (char)('0' + (pct % 100) / 10);
    nb[2] = (char)('0' + pct % 10);
    nb[3] = '%';
    nb[4] = '\0';
    DrawText(row, col, nb, WHITE, BLACK);
}

/* ----- canvas init / restore --------------------------------------------- */

static void cgx_init(void)
{
    uint16_t i;

    /* clear framebuffer: 80 * 30 * 2 = 4800 bytes */
    RIA.addr0 = CGX_DATA;
    RIA.step0  = 1;
    for (i = 0u; i < 4800u; i++)
        RIA.rw0 = 0;

    xreg_vga_canvas(CGX_CANVAS);

    xram0_struct_set(CGX_STRUCT, vga_mode1_config_t, x_wrap,           false);
    xram0_struct_set(CGX_STRUCT, vga_mode1_config_t, y_wrap,           false);
    xram0_struct_set(CGX_STRUCT, vga_mode1_config_t, x_pos_px,         0);
    xram0_struct_set(CGX_STRUCT, vga_mode1_config_t, y_pos_px,         0);
    xram0_struct_set(CGX_STRUCT, vga_mode1_config_t, width_chars,      CGX_COLS);
    xram0_struct_set(CGX_STRUCT, vga_mode1_config_t, height_chars,     CGX_ROWS);
    xram0_struct_set(CGX_STRUCT, vga_mode1_config_t, xram_data_ptr,    CGX_DATA);
    xram0_struct_set(CGX_STRUCT, vga_mode1_config_t, xram_palette_ptr, CGX_FONT_PAL);
    xram0_struct_set(CGX_STRUCT, vga_mode1_config_t, xram_font_ptr,    CGX_FONT_PTR);

    xreg(1, 0, 1, CGX_MODE_CHAR, CGX_BPP_OPT, CGX_STRUCT, CGX_PLANE);
}

static void cgx_restore(void)
{
    xreg_vga_canvas(CGX_CANVAS);
    xreg(1, 0, 1, CGX_MODE_CONSOLE);
}

#endif /* COURIER_GFX_H */
