// razemOS .COM
// ROMS Browser & Launchpad - browse *.rp6502 files with arrow-key tile navigation
// command: roms [path]
//

#include "commons.h"
#include "./commons/courier-gfx.h"

#define APPVER         "20260408.1800"
#define APP_FOOTER "________________________________________________________________________________"

#define MAXROMS        64
#define ROM_DNAME_LEN  18   /* display name chars per tile (TILE_W - 2 padding) */
#define ROM_PATH_LEN   64   /* full path stored for ria_execl */
#define TILE_COLS      4
#define TILE_W         20   /* chars per tile = ROM_DNAME_LEN + 2 */
#define HDR_ROWS       4    /* rows 0-3 reserved for header */
#define FTR_ROWS       3    /* rows 27-29 reserved for status */
#define CONTENT_ROWS   (CGX_ROWS - HDR_ROWS - FTR_ROWS)  /* 26 */

#ifndef AM_DIR
#define AM_DIR 0x10
#endif

/* keyboard */
#define KEYBOARD_INPUT   0xFFE0u
#define KEYBOARD_BYTES  32

static uint8_t keystates[KEYBOARD_BYTES];
static bool    handled_key;
#define key(code) (keystates[(code) >> 3] & (1 << ((code) & 7)))

/* ROM list */
static char     rom_dname[MAXROMS][ROM_DNAME_LEN + 1];
static char     rom_path [MAXROMS][ROM_PATH_LEN];
static int      rom_count;
static f_stat_t roms_ent;
static char     roms_cwd[ROM_PATH_LEN];

/* ------------------------------------------------------------------ */

static bool ends_with_rp6502(const char *name)
{
    int len = (int)strlen(name);
    if (len < 8) return false;
    return name[len - 7] == '.'
        && (name[len - 6] == 'r' || name[len - 6] == 'R')
        && (name[len - 5] == 'p' || name[len - 5] == 'P')
        &&  name[len - 4] == '6'
        &&  name[len - 3] == '5'
        &&  name[len - 2] == '0'
        &&  name[len - 1] == '2';
}

static void draw_tile(int idx, bool selected, int top_row)
{
    int screen_row = HDR_ROWS + (idx / TILE_COLS) - top_row;
    int screen_col = (idx % TILE_COLS) * TILE_W;
    uint8_t fg = selected ? WHITE     : LIGHT_GRAY;
    uint8_t bg = selected ? DARK_GREEN : DARK_GRAY;
    static char tbuf[TILE_W + 1];
    int nlen, i;

    if (screen_row < HDR_ROWS || screen_row >= CGX_ROWS - FTR_ROWS)
        return;

    tbuf[0] = ' ';
    nlen = (int)strlen(rom_dname[idx]);
    if (nlen > ROM_DNAME_LEN) nlen = ROM_DNAME_LEN;
    for (i = 0; i < nlen; i++)          tbuf[1 + i] = rom_dname[idx][i];
    for (; i < ROM_DNAME_LEN; i++)      tbuf[1 + i] = ' ';
    tbuf[TILE_W - 1] = ' ';
    tbuf[TILE_W]     = 0;
    DrawText((uint8_t)screen_row, (uint8_t)screen_col, tbuf, fg, bg);
}

static void draw_status(int sel)
{
    /* row 29: "N/TOTAL  <selected name>" padded to 80 chars */
    static char sbuf[CGX_COLS + 1];
    static char n1[8], n2[8];
    int i, n, i1, i2;
    const char *p;

    i1 = 7; i2 = 7;
    n1[7] = n2[7] = 0;

    n = sel + 1;
    do { n1[--i1] = (char)('0' + n % 10); n /= 10; } while (n);
    n = rom_count;
    do { n2[--i2] = (char)('0' + n % 10); n /= 10; } while (n);

    i = 0;
    for (p = n1 + i1; *p && i < CGX_COLS; ) sbuf[i++] = *p++;
    if (i < CGX_COLS) sbuf[i++] = '/';
    for (p = n2 + i2; *p && i < CGX_COLS; ) sbuf[i++] = *p++;
    if (i < CGX_COLS) sbuf[i++] = ' ';
    for (p = rom_dname[sel]; *p && i < CGX_COLS; ) sbuf[i++] = *p++;
    while (i < CGX_COLS) sbuf[i++] = ' ';
    sbuf[CGX_COLS] = 0;
    DrawText((uint8_t)(CGX_ROWS - 2), 0, APP_FOOTER, LIGHT_GRAY, BLACK);
    DrawText((uint8_t)(CGX_ROWS - 1), 0, sbuf, LIGHT_GRAY, BLACK);
}

static void redraw_all(int sel, int top_row)
{
    int r, i;
    for (r = HDR_ROWS; r < CGX_ROWS - FTR_ROWS; r++)
        ClearLine((uint8_t)r, DARK_GRAY, BLACK);
    for (i = top_row * TILE_COLS;
         i < rom_count && (i / TILE_COLS) < top_row + CONTENT_ROWS;
         i++)
        draw_tile(i, i == sel, top_row);
    draw_status(sel);
}

static void draw_header(void)
{
    ClearLine(1, WHITE, BLACK);
    DrawText(1,  0, " razemOS > "    ,     WHITE, DARK_GREEN);
    DrawText(1, 12, "ROMS launcher"  ,     WHITE,      BLACK);
    DrawText(1, 59, "version " APPVER, DARK_GRAY,      BLACK);
    ClearLine(2, DARK_GRAY, BLACK);
    DrawText(2,  12, "navigate by arrows, press [ENTER] to launch, press [Esc] to exit", DARK_GRAY, BLACK);
}

/* ------------------------------------------------------------------ */

int main(int argc, char **argv)
{
    const char *path;
    const char *base;
    int dirdes;
    int sel, top_row, new_sel, new_top, old_sel;
    int action;
    uint8_t v, new_keys, new_key;
    int i, j, keylast;
    int nlen, plen, flen, pos;
    static char pathbuf[ROM_PATH_LEN];

    path = (argc >= 1) ? argv[0] : ".";

    /* get cwd to build absolute paths when scanning "." */
    roms_cwd[0] = 0;
    f_getcwd(roms_cwd, sizeof(roms_cwd));

    /* scan directory for *.rp6502 files */
    dirdes = f_opendir(path);
    if (dirdes < 0) {
        printf(NEWLINE EXCLAMATION "cannot open: %s" NEWLINE, path);
        return 1;
    }
    rom_count = 0;
    while (rom_count < MAXROMS) {
        if (f_readdir(&roms_ent, dirdes) < 0 || !roms_ent.fname[0]) break;
        if (roms_ent.fattrib & AM_DIR) continue;
        if (!ends_with_rp6502(roms_ent.fname)) continue;

        /* display name: filename without .rp6502 extension */
        nlen = (int)strlen(roms_ent.fname) - 7;
        if (nlen < 0) nlen = 0;
        if (nlen > ROM_DNAME_LEN) nlen = ROM_DNAME_LEN;
        for (i = 0; i < nlen; i++)
            rom_dname[rom_count][i] = roms_ent.fname[i];
        rom_dname[rom_count][nlen] = 0;

        /* full path: <base>/<fname> */
        base = (strcmp(path, ".") == 0 && roms_cwd[0]) ? roms_cwd : path;
        plen = (int)strlen(base);
        flen = (int)strlen(roms_ent.fname);
        pos = 0;
        for (i = 0; i < plen && pos < ROM_PATH_LEN - 2; i++)
            pathbuf[pos++] = base[i];
        if (pos > 0 && pathbuf[pos - 1] != '/' && pathbuf[pos - 1] != ':')
            pathbuf[pos++] = '/';
        for (i = 0; i < flen && pos < ROM_PATH_LEN - 1; i++)
            pathbuf[pos++] = roms_ent.fname[i];
        pathbuf[pos] = 0;
        strncpy(rom_path[rom_count], pathbuf, ROM_PATH_LEN - 1);
        rom_path[rom_count][ROM_PATH_LEN - 1] = 0;

        rom_count++;
    }
    f_closedir(dirdes);

    if (rom_count == 0) {
        printf(NEWLINE "No .rp6502 files in %s" NEWLINE, path);
        return 0;
    }

    /* enter character mode display */
    cgx_init();
    draw_header();

    sel         = 0;
    top_row     = 0;
    handled_key = false;
    action      = 0;
    keylast     = 0;
    new_key     = 0;

    redraw_all(sel, top_row);

    xreg_ria_keyboard(KEYBOARD_INPUT);
    v = RIA.vsync;

    while (1) {
        if (RIA.vsync == v) continue;
        v = RIA.vsync;

        /* read keyboard bitmask from XRAM */
        new_key = 0;
        for (i = 0; i < KEYBOARD_BYTES; i++) {
            RIA.addr1 = (uint16_t)(KEYBOARD_INPUT + i);
            new_keys  = RIA.rw1;
            for (j = 0; j < 8; j++) {
                new_key = new_keys & (uint8_t)(1 << j);
                if (((i << 3) + j) > 3 && new_key != (keystates[i] & (uint8_t)(1 << j)))
                    keylast = (i << 3) + j;
            }
            keystates[i] = new_keys;
        }

        if (!(keystates[0] & 1) && !new_key) {
            if (!handled_key) {
                action = 0;
                if (key(KEY_LEFT))  action = 1;
                if (key(KEY_RIGHT)) action = 2;
                if (key(KEY_UP))    action = 3;
                if (key(KEY_DOWN))  action = 4;
                if (key(KEY_ENTER)) action = 5;
                if (key(KEY_ESC))   action = 99;
                handled_key = true;
            }
        } else {
            action      = 0;
            handled_key = false;
        }

        if (!action) continue;

        new_sel = sel;
        switch (action) {
        case 1: /* LEFT */
            if (sel > 0) new_sel = sel - 1;
            break;
        case 2: /* RIGHT */
            if (sel < rom_count - 1) new_sel = sel + 1;
            break;
        case 3: /* UP */
            if (sel >= TILE_COLS) new_sel = sel - TILE_COLS;
            break;
        case 4: /* DOWN */
            if (sel + TILE_COLS < rom_count) new_sel = sel + TILE_COLS;
            break;
        case 5: /* ENTER — launch selected ROM */
            cgx_restore();
            ria_execl(rom_path[sel], NULL);
            /* execl returned (failed) — restore display */
            cgx_init();
            draw_header();
            redraw_all(sel, top_row);
            action = 0;
            continue;
        case 99: /* ESC — exit */
            cgx_restore();
            return 0;
        default:
            break;
        }
        action = 0;

        if (new_sel == sel) continue;

        old_sel = sel;
        sel     = new_sel;

        /* scroll if selection moved off screen */
        new_top = top_row;
        if (sel / TILE_COLS < top_row)
            new_top = sel / TILE_COLS;
        else if (sel / TILE_COLS >= top_row + CONTENT_ROWS)
            new_top = sel / TILE_COLS - CONTENT_ROWS + 1;

        if (new_top != top_row) {
            top_row = new_top;
            redraw_all(sel, top_row);
        } else {
            /* only redraw changed tiles + status */
            draw_tile(old_sel, false, top_row);
            draw_tile(sel,     true,  top_row);
            draw_status(sel);
        }
    }
}
