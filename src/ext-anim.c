// razemOS .COM
// BMP Viewer
// command : view
//

#include "commons.h"

#define APPVER "20260408.1245"

#define APPNAME "Test anim for BMP files 640x480x1bpp"
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

int main(int argc, char **argv) {
    char filename[64] = "";
    unsigned char i, j, v, action;
    uint16_t animcounter = 0;
    uint8_t new_key, new_keys, keylast;
    uint16_t xaddr;

    (void)argc;
    
    xaddr = GFX_DATA;
    
    xreg(1, 0, 0, GFX_CANVAS_640x480);
    xram0_struct_set(GFX_STRUCT, vga_mode3_config_t, x_wrap, false);
    xram0_struct_set(GFX_STRUCT, vga_mode3_config_t, y_wrap, false);
    xram0_struct_set(GFX_STRUCT, vga_mode3_config_t, x_pos_px, 0);
    xram0_struct_set(GFX_STRUCT, vga_mode3_config_t, y_pos_px, 0);
    xram0_struct_set(GFX_STRUCT, vga_mode3_config_t, width_px, 640);
    xram0_struct_set(GFX_STRUCT, vga_mode3_config_t, height_px, 200);
    xram0_struct_set(GFX_STRUCT, vga_mode3_config_t, xram_data_ptr, xaddr);
    xram0_struct_set(GFX_STRUCT, vga_mode3_config_t, xram_palette_ptr, 0xFFFF);

    xram0_struct_set(GFX_STRUCT + 0x10, vga_mode3_config_t, x_wrap, false);
    xram0_struct_set(GFX_STRUCT + 0x10, vga_mode3_config_t, y_wrap, false);
    xram0_struct_set(GFX_STRUCT + 0x10, vga_mode3_config_t, x_pos_px, 0);
    xram0_struct_set(GFX_STRUCT + 0x10, vga_mode3_config_t, y_pos_px, 201);
    xram0_struct_set(GFX_STRUCT + 0x10, vga_mode3_config_t, width_px, 640);
    xram0_struct_set(GFX_STRUCT + 0x10, vga_mode3_config_t, height_px, 280);
    xram0_struct_set(GFX_STRUCT + 0x10, vga_mode3_config_t, xram_data_ptr, xaddr + (200u * 80u));
    xram0_struct_set(GFX_STRUCT + 0x10, vga_mode3_config_t, xram_palette_ptr, 0xFFFF);

    LoadBMP(argv[1], GFX_DATA);
    xreg(1, 0, 1, GFX_MODE_BITMAP, GFX_BITMAP_bpp1, GFX_STRUCT, GFX_PLANE_1, 1, 200);
    xreg(1, 0, 1, GFX_MODE_BITMAP, GFX_BITMAP_bpp1, GFX_STRUCT + 0x10, GFX_PLANE_0, 201, 480);

    xreg_ria_keyboard(KEYBORD_INPUT);

    for(i = 200; i > 0; i--) {
        xram0_struct_set(GFX_STRUCT, vga_mode3_config_t, y_pos_px, -i);
        PAUSE(1);
    }

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
