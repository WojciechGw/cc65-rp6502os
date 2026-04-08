// razemOS .COM
// BMP Viewer
// command : view
//

#include "commons.h"

#define APPVER "20260408.1557"

#define APPNAME "Viewer for BMP files 640x480x1bpp"
#define APPDIRDEFAULT "" // view in current directory if empty
#define APP_MSG_TITLE CSI_RESET CSI "2;1H" CSI HIGHLIGHT_COLOR " razemOS > " ANSI_RESET " " APPNAME ANSI_DARK_GRAY CSI "2;60H" "version " APPVER ANSI_RESET
#define APP_MSG_START ANSI_DARK_GRAY CSI "4;1H" "Show BMP file in 640x480xbpp1 format" ANSI_RESET
#define APP_WORKBENCH_POS CSI "6;1H"

/* KEYBOARD input subsystem */
#define KEYBORD_INPUT 0xFFE0
#define KEYBOARD_BYTES 32
uint8_t keystates[KEYBOARD_BYTES] = {0};
bool handled_key = false;
#define key(code) (keystates[(code) >> 3] & (1 << ((code) & 7)))

void pc_fb_clear(unsigned char value, uint16_t address) {
    unsigned int i;
    RIA.addr0 = address;
    RIA.step0 = 1;
    for (i = 0u; i < PC_FB_SIZE_BYTES; ++i) {
        RIA.rw0 = value;
    }
}

// LoadBMP - load BMP file 1-bit 640x480 to XRAM given address.
// handle both direction: bottom-up (standard BMP, height > 0)
// and top-down (height < 0, ie. files saved by SaveBMP).
// usage : LoadBMP("MSC0:/qrcode.bmp", GFX_DATA);
//
int LoadBMP(const char *path, uint16_t address) {
    static uint8_t hdr[26];
    uint16_t pixel_offset;
    uint8_t  top_down;
    uint16_t file_row;
    int fd;

    fd = open(path, O_RDONLY);
    if (fd < 0) {
        printf(NEWLINE EXCLAMATION "cannot open %s" NEWLINE, path);
        return -1;
    }

    if (read(fd, hdr, 26) != 26 || hdr[0] != 'B' || hdr[1] != 'M') {
        printf(NEWLINE EXCLAMATION "invalid BMP" NEWLINE);
        close(fd);
        return -1;
    }

    pixel_offset = (uint16_t)hdr[10] | ((uint16_t)hdr[11] << 8);
    top_down = (hdr[25] & 0x80) != 0;

    lseek(fd, pixel_offset, SEEK_SET);

    if (top_down) {
        /* rows in file order == rows in XRAM order: read in chunks <= 0x7FFF */
        uint16_t addr      = address;
        uint16_t remaining = (uint16_t)(PC_FB_HEIGHT * PC_FB_STRIDE); /* 38400 */
        while (remaining > 0) {
            unsigned chunk = (remaining > 0x7FFF) ? 0x7FFF : (unsigned)remaining;
            if (read_xram(addr, chunk, fd) != (int)chunk) break;
            addr      += (uint16_t)chunk;
            remaining -= (uint16_t)chunk;
        }
    } else {
        /* bottom-up: file row 0 -> XRAM row (PC_FB_HEIGHT-1), etc. */
        for (file_row = 0; file_row < PC_FB_HEIGHT; file_row++) {
            uint16_t xram_row = (uint16_t)(PC_FB_HEIGHT - 1u - file_row);
            if (read_xram(address + xram_row * PC_FB_STRIDE, PC_FB_STRIDE, fd) != PC_FB_STRIDE)
                break;
        }
    }

    close(fd);
    return 0;
}

// SaveBMP - save framebuffer (640x480xbpp1) to BMP file.
// palette: index 0 = black, index 1 = white.
// usage: SaveBMP("MSC0:/qrcode.bmp");
//
int SaveBMP(const char *path, uint16_t address) {
    /* BMP header: File Header (14) + BITMAPINFOHEADER (40) + color table (8) = 62 bytes */
    static const uint8_t bmp_hdr[62] = {
        /* File Header */
        'B', 'M',
        0x5E, 0x96, 0x00, 0x00,  /* file size = 38462 = 0x965E */
        0x00, 0x00, 0x00, 0x00,  /* reserved */
        0x3E, 0x00, 0x00, 0x00,  /* pixel data offset = 62 */
        /* BITMAPINFOHEADER */
        0x28, 0x00, 0x00, 0x00,  /* header size = 40 */
        0x80, 0x02, 0x00, 0x00,  /* width  = 640 */
        0x20, 0xFE, 0xFF, 0xFF,  /* height = -480 (top-down) */
        0x01, 0x00,              /* color planes = 1 */
        0x01, 0x00,              /* bits per pixel = 1 */
        0x00, 0x00, 0x00, 0x00,  /* compression = BI_RGB */
        0x00, 0x00, 0x00, 0x00,  /* image size = 0 */
        0x13, 0x0B, 0x00, 0x00,  /* X ppm = 2835 (~72 dpi) */
        0x13, 0x0B, 0x00, 0x00,  /* Y ppm = 2835 */
        0x02, 0x00, 0x00, 0x00,  /* colors in table = 2 */
        0x00, 0x00, 0x00, 0x00,  /* important colors = 0 */
        /* Color table */
        0x00, 0x00, 0x00, 0x00,  /* index 0 = black */
        0xFF, 0xFF, 0xFF, 0x00   /* index 1 = white */
    };
    /* stage header in the 62 bytes of XRAM immediately before the framebuffer */
    #define BMP_HDR_XRAM (address - 62u)
    uint8_t  i;
    uint16_t addr, remaining;
    int fd;

    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) {
        printf(NEWLINE EXCLAMATION "cannot create %s" NEWLINE, path);
        return -1;
    }

    /* stage header to XRAM immediately before the framebuffer */
    RIA.addr0 = BMP_HDR_XRAM;
    RIA.step0 = 1;
    for (i = 0; i < 62u; i++)
        RIA.rw0 = bmp_hdr[i];

    /* write header + pixel data as one contiguous XRAM block in chunks <= 0x7FFF */
    addr      = BMP_HDR_XRAM;
    remaining = 62u + (uint16_t)(PC_FB_HEIGHT * PC_FB_STRIDE);  /* 38462 = 0x965E */
    while (remaining > 0) {
        unsigned chunk = (remaining > 0x7FFF) ? 0x7FFF : (unsigned)remaining;
        if (write_xram(addr, chunk, fd) < 0) break;
        addr      += (uint16_t)chunk;
        remaining -= (uint16_t)chunk;
    }

    close(fd);
    printf(NEWLINE "%s saved" NEWLINE, path);
    return 0;
}

int DumpBIN(const char *path, uint16_t addr) {

    uint16_t remaining = (uint16_t)(PC_FB_HEIGHT * PC_FB_STRIDE); /* 38400 = 0x9600 */
    int fd;
    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) {
        printf(NEWLINE EXCLAMATION "cannot create %s" NEWLINE, path);
        return -1;
    }
    while (remaining > 0) {
        unsigned chunk = (remaining > 0x7FFF) ? 0x7FFF : remaining;
        if (write_xram(addr, chunk, fd) < 0) break;
        addr      += (uint16_t)chunk;
        remaining -= (uint16_t)chunk;
    }
    close(fd);
    printf(NEWLINE "%s saved" NEWLINE, path);
    return 0;
}

int main(int argc, char **argv) {
    char filename[64] = "";
    unsigned char i, j, v, action;
    uint8_t new_key, new_keys, keylast, showtime;
    uint16_t xaddress;

    #ifdef SHOWAPPNAME
    printf(CSI_CLS APP_MSG_TITLE APP_MSG_START APP_WORKBENCH_POS);
    #endif

    action   = 0;
    showtime = 30;
    xaddress = GFX_DATA;

    if (argc > 0) {

        /* /x [addr] — show framebuffer at optional XRAM address */
        if (!strcmp(argv[0], "/x")) {
            if (argc > 1) xaddress = (uint16_t)strtoul(argv[1], NULL, 0);
        }

        /* filename — load BMP file */
        if (argv[0][0] != '/') {
            snprintf(filename, sizeof(filename), APPDIRDEFAULT "%s", argv[0]);
            {
                int fnlen = strlen(filename);
                if (fnlen >= 4 && (strcmp(filename + fnlen - 4, ".bmp") == 0 ||
                                   strcmp(filename + fnlen - 4, ".BMP") == 0))
                    LoadBMP(filename, xaddress);
            }
        }

        /* /d — dump framebuffer to .bin (derived from argv[0] filename) */
        if (argc >= 2 && !strcmp(argv[1], "/d")) {
            char binpath[64];
            const char *base = argv[0];
            const char *p;
            for (p = base; *p; p++)
                if (*p == '/' || *p == ':') base = p + 1;
            strncpy(binpath, base, sizeof(binpath) - 1);
            binpath[sizeof(binpath) - 1] = 0;
            /* strip any extension */
            {
                char *dot = strrchr(binpath, '.');
                if (dot) *dot = 0;
            }
            strncat(binpath, ".bin", sizeof(binpath) - strlen(binpath) - 1);
            DumpBIN(binpath, xaddress);
            return 0;
        }

        /* set up bitmap display */
        if (argv[0][0] != '/' || !strcmp(argv[0], "/x")) {
            xreg(1, 0, 0, GFX_CANVAS_640x480);
            xram0_struct_set(GFX_STRUCT, vga_mode3_config_t, x_wrap, false);
            xram0_struct_set(GFX_STRUCT, vga_mode3_config_t, y_wrap, false);
            xram0_struct_set(GFX_STRUCT, vga_mode3_config_t, x_pos_px, 0);
            xram0_struct_set(GFX_STRUCT, vga_mode3_config_t, y_pos_px, 0);
            xram0_struct_set(GFX_STRUCT, vga_mode3_config_t, width_px, 640);
            xram0_struct_set(GFX_STRUCT, vga_mode3_config_t, height_px, 480);
            xram0_struct_set(GFX_STRUCT, vga_mode3_config_t, xram_data_ptr, xaddress);
            xram0_struct_set(GFX_STRUCT, vga_mode3_config_t, xram_palette_ptr, 0xFFFF);
            xreg(1, 0, 1, GFX_MODE_BITMAP, GFX_BITMAP_bpp1, GFX_STRUCT, GFX_PLANE_0);
        }

        /* /w [time] — show for given time (ms) then return */
        if (!strcmp(argv[0], "/w") || (argc >= 2 && !strcmp(argv[1], "/w"))) {
            if (argc > 2) showtime = (uint16_t)strtoul(argv[2], NULL, 0);
            PAUSE(showtime);
            xreg(1, 0, 1, GFX_MODE_CONSOLE);
            printf(CSI_RESET);
            return 0;
        }

    } else {
        #ifdef SHOWAPPNAME
        printf(APPNAME);
        #endif
        printf(NEWLINE "Usage: view [filename.bmp|/x][/d|/w][address|time]" NEWLINE);
        return 1;
    }

    xreg_ria_keyboard(KEYBORD_INPUT);

    v = RIA.vsync;
    while (1)
    {
      // wait for VSYNC
      if (RIA.vsync == v) continue;
      v = RIA.vsync;

      RIA.addr1 = KEYBORD_INPUT;
      RIA.step1 = 0;
      // fill the keystates bitmask array
      for (i = 0; i < KEYBOARD_BYTES; i++) {
         RIA.addr1 = KEYBORD_INPUT + i;
         new_keys = RIA.rw1;
         // check for change in any and all keys
         for (j = 0; j < 8; j++) {
               new_key = (new_keys & (1<<j));
               if ((((i<<3)+j)>3) && (new_key != (keystates[i] & (1<<j)))) {
                  keylast = ((i<<3)+j);
                  // printf( "key %d %s\n", keylast, (new_key ? "pressed" : "released"));
               }
         }
         keystates[i] = new_keys;
      }
      if (!(keystates[0] & 1) && !new_key) {
         if (!handled_key) { // handle only once per single keypress
               // handle the keystrokes
               if (key(KEY_LEFT)) {
                  action = 1;
               }
               if (key(KEY_RIGHT)) {
                  action = 2;
               }
               if (key(KEY_UP)) {
                  action = 3;
               }
               if (key(KEY_DOWN)) {
                  action = 4;
               }
               if (key(KEY_SPACE)) {
                  action = 5;
               }
               if (key(KEY_ESC)) {
                  action = 99;
            }
            handled_key = true;
         }
      } else { // no keys down
         action = 0;
         handled_key = false;
      }

      switch(action){
      case 1:
         printf("LEFT\n\n");
         break;
      case 2:
         printf("RIGHT\n\n");
         break;
      case 3:
         printf("UP\n\n");
         break;
      case 4:
         printf("DOWN\n\n");
         break;
      case 5:
         printf("SPACE\n\n");
         break;
      case 99:
         xreg(1, 0, 1, GFX_MODE_CONSOLE);
         return 0;
      default:
         break;
      }
   }

}
