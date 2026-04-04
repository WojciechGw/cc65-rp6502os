// razemOS .COM
// BMP Viewer
// command : view
//

#include "commons.h"

#define APPVER "20260404.1657"

#define APPNAME "Viewer for BMP files 640x480x1bpp"
#define APPDIRDEFAULT "" // view in current directory if empty
#define APP_MSG_TITLE CSI_RESET CSI "[2;1H" CSI HIGHLIGHT_COLOR " razemOS > " ANSI_RESET " " APPNAME ANSI_DARK_GRAY CSI "[2;60H" "version " APPVER ANSI_RESET
#define APP_MSG_START ANSI_DARK_GRAY CSI "[4;1H" "Show BMP file in 640x480xbpp1 format" ANSI_RESET
#define APP_WORKBENCH_POS CSI "[6;1H"

/* KEYBOARD input subsystem */
#define KEYBORD_INPUT 0xFFE0
#define KEYBOARD_BYTES 32
uint8_t keystates[KEYBOARD_BYTES] = {0};
bool handled_key = false;
#define key(code) (keystates[(code) >> 3] & (1 << ((code) & 7)))

void pc_fb_clear(unsigned char value) {
    unsigned int i;
    RIA.addr0 = PC_FB_ADDR;
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
    static uint8_t buf[80];
    uint16_t pixel_offset;
    uint8_t  top_down;
    uint16_t file_row, xram_row;
    uint8_t  b;
    int fd;

    fd = open(path, O_RDONLY);
    if (fd < 0) {
        printf("View ERROR: cannot open %s" NEWLINE NEWLINE, path);
        return -1;
    }

    if (read(fd, buf, 26) != 26 || buf[0] != 'B' || buf[1] != 'M') {
        printf("View ERROR: invalid BMP" NEWLINE NEWLINE);
        close(fd);
        return -1;
    }

    pixel_offset = (uint16_t)buf[10] | ((uint16_t)buf[11] << 8);

    top_down = (buf[25] & 0x80) != 0;

    lseek(fd, pixel_offset, SEEK_SET);

    for (file_row = 0; file_row < PC_FB_HEIGHT; file_row++) {
        if (read(fd, buf, PC_FB_STRIDE) != PC_FB_STRIDE) break;
        xram_row = top_down ? file_row : (uint16_t)(PC_FB_HEIGHT - 1u - file_row);
        RIA.addr0 = address + xram_row * PC_FB_STRIDE;
        RIA.step0 = 1;
        for (b = 0; b < PC_FB_STRIDE; b++)
            RIA.rw0 = buf[b];
    }

    close(fd);
    printf("View INFO: loaded %s" NEWLINE NEWLINE, path);
    return 0;
}

#ifdef CODEOFF

// SaveBMP - dave framebuffer (640x480xbpp1) to BMP file.
// palette: index 0 = black, index 1 = white (black_is_1 = 0).
// usage: SaveBMP("MSC0:/qrcode.bmp");
//
int SaveBMP(const char *path) {
    static uint8_t buf[80];
    uint16_t row;
    uint8_t  b;
    int fd;

    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) {
        printf("SaveBMP: cannot create %s\n", path);
        return -1;
    }

    /* --- BMP File Header (14 bytes) --- */
    buf[0]='B';   buf[1]='M';
    /* file size = 14 + 40 + 8 + 38400 = 38462 = 0x965E */
    buf[2]=0x5E;  buf[3]=0x96;  buf[4]=0x00;  buf[5]=0x00;
    /* reserved */
    buf[6]=0x00;  buf[7]=0x00;  buf[8]=0x00;  buf[9]=0x00;
    /* pixel data offset = 62 = 0x3E */
    buf[10]=0x3E; buf[11]=0x00; buf[12]=0x00; buf[13]=0x00;
    write(fd, buf, 14);

    /* --- BITMAPINFOHEADER (40 bytes) --- */
    buf[0]=0x28;  buf[1]=0x00;  buf[2]=0x00;  buf[3]=0x00; /* header size = 40 */
    buf[4]=0x80;  buf[5]=0x02;  buf[6]=0x00;  buf[7]=0x00; /* width  = 640 */
    buf[8]=0x20;  buf[9]=0xFE;  buf[10]=0xFF; buf[11]=0xFF; /* height = -480 (top-down) */
    buf[12]=0x01; buf[13]=0x00;               /* color planes = 1 */
    buf[14]=0x01; buf[15]=0x00;               /* bits per pixel = 1 */
    buf[16]=0x00; buf[17]=0x00; buf[18]=0x00; buf[19]=0x00; /* compression = BI_RGB */
    buf[20]=0x00; buf[21]=0x00; buf[22]=0x00; buf[23]=0x00; /* image size = 0 */
    buf[24]=0x13; buf[25]=0x0B; buf[26]=0x00; buf[27]=0x00; /* X ppm = 2835 (~72 dpi) */
    buf[28]=0x13; buf[29]=0x0B; buf[30]=0x00; buf[31]=0x00; /* Y ppm = 2835 */
    buf[32]=0x02; buf[33]=0x00; buf[34]=0x00; buf[35]=0x00; /* colors in table = 2 */
    buf[36]=0x00; buf[37]=0x00; buf[38]=0x00; buf[39]=0x00; /* important colors = 0 */
    write(fd, buf, 40);

    /* --- Color table: 2 x 4 bytes (B, G, R, reserved) --- */
    buf[0]=0x00; buf[1]=0x00; buf[2]=0x00; buf[3]=0x00; /* index 0 = black */
    buf[4]=0xFF; buf[5]=0xFF; buf[6]=0xFF; buf[7]=0x00; /* index 1 = white */
    write(fd, buf, 8);

    /* --- Pixel data: PC_FB_HEIGHT rows x PC_FB_STRIDE bytes --- */
    for (row = 0; row < PC_FB_HEIGHT; row++) {
        RIA.addr0 = PC_FB_ADDR + row * PC_FB_STRIDE;
        RIA.step0 = 1;
        for (b = 0; b < PC_FB_STRIDE; b++)
            buf[b] = RIA.rw0;
        write(fd, buf, PC_FB_STRIDE);
    }

    close(fd);

    printf("\r\nSaveBMP: %s saved\r\n", path);
    return 0;
}

int DumpBIN(const char *path) {
    static uint8_t buf[80];
    uint16_t row;
    uint8_t  b;
    int fd;

    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) {
        printf(NEWLINE "DumpBIN: cannot create %s" NEWLINE, path);
        return -1;
    }

    /* --- Pixel data: PC_FB_HEIGHT rows x PC_FB_STRIDE bytes --- */
    for (row = 0; row < PC_FB_HEIGHT; row++) {
        RIA.addr1 = PC_FB_ADDR + row * PC_FB_STRIDE;
        RIA.step1 = 1;
        for (b = 0; b < PC_FB_STRIDE; b++)
            buf[b] = RIA.rw1;
        write(fd, buf, PC_FB_STRIDE);
    }

    close(fd);

    printf(NEWLINE "DumpBIN: %s saved" NEWLINE, path);
    return 0;
}

#endif

int main(int argc, char **argv) {
    char filename[64] = "";
    unsigned char i, j, v, action;
    uint8_t new_key, new_keys, keylast;
    
    #ifdef SHOWAPPNAME 
    printf(CSI_CLS APP_MSG_TITLE APP_MSG_START APP_WORKBENCH_POS);
    #endif

    if (argc > 0) {
        // printf("\r\nBMP Viewer\r\nClearing framebuffer ... please wait ...\r\n\r\n");
        // pc_fb_clear(0x00u);
        xreg(1, 0, 0, GFX_CANVAS_640x480);
        xram0_struct_set(GFX_STRUCT, vga_mode3_config_t, x_wrap, false);
        xram0_struct_set(GFX_STRUCT, vga_mode3_config_t, y_wrap, false);
        xram0_struct_set(GFX_STRUCT, vga_mode3_config_t, x_pos_px, 0);
        xram0_struct_set(GFX_STRUCT, vga_mode3_config_t, y_pos_px, 0);
        xram0_struct_set(GFX_STRUCT, vga_mode3_config_t, width_px, 640);
        xram0_struct_set(GFX_STRUCT, vga_mode3_config_t, height_px, 480);
        if (strcmp(argv[0], "/x") == 0 || strcmp(argv[0], "/xw") == 0) {
            uint16_t xaddr = (argc > 1) ? (uint16_t)strtoul(argv[1], NULL, 0) : GFX_DATA;
            xram0_struct_set(GFX_STRUCT, vga_mode3_config_t, xram_data_ptr, xaddr);
            xram0_struct_set(GFX_STRUCT, vga_mode3_config_t, xram_palette_ptr, 0xFFFF);
        } else {
            xram0_struct_set(GFX_STRUCT, vga_mode3_config_t, xram_data_ptr, GFX_DATA);
            xram0_struct_set(GFX_STRUCT, vga_mode3_config_t, xram_palette_ptr, 0xFFFF);
            snprintf(filename, sizeof(filename), APPDIRDEFAULT "%s", argv[0]);
            {
                int fnlen = strlen(filename);
                if (fnlen >= 4 && (strcmp(filename + fnlen - 4, ".bmp") == 0 || strcmp(filename + fnlen - 4, ".BMP") == 0))
                    LoadBMP(filename, GFX_DATA);
            }
        }
        xreg(1, 0, 1, GFX_MODE_BITMAP, GFX_BITMAP_bpp1, GFX_STRUCT, GFX_PLANE_0);
        if (argc > 1) return 0;
    } else {
        #ifdef SHOWAPPNAME 
        printf(APPNAME);
        #endif
        printf(NEWLINE "Usage: view [file.bmp]" NEWLINE NEWLINE);
        return 1;
    }


    if (strcmp(argv[0], "/x") == 0 || strcmp(argv[0], "/xw") == 0) {
      if (strcmp(argv[0], "/xw") == 0) PAUSE(300);
      xreg(1, 0, 1, GFX_MODE_CONSOLE);
      printf(CSI_CLS);
      return 0;      
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
         printf(CSI_CLS);
         return 0;
      default:
         break;
      }
   }

}
