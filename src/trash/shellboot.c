// razemOS shellboot
//

#include "commons.h"

/* KEYBOARD input subsystem */
#define KEYBORD_INPUT 0xFFE0
#define KEYBOARD_BYTES 32
uint8_t keystates[KEYBOARD_BYTES] = {0};
bool handled_key = false;
#define key(code) (keystates[(code) >> 3] & (1 << ((code) & 7)))

#define PAUSE(millis) ticks=clock(); while(clock() < (ticks + millis)){}

/* GFX subsystem setup */
#define GFX_CANVAS_640x480 3
#define GFX_MODE_CONSOLE 0
#define GFX_MODE_BITMAP    3
#define GFX_BITMAP_bpp1    0b00000000
#define GFX_PLANE_0        0

/* XRAM memory map */
#define GFX_STRUCT 0xFFC0u
#define GFX_DATA   0x2000u

/* 640x480, 1 bpp */
#define PC_FB_ADDR          0x2000u
#define PC_FB_WIDTH         640u
#define PC_FB_HEIGHT        480u
#define PC_FB_STRIDE        80u
#define PC_FB_SIZE_BYTES    38400u

void main() {

   unsigned char i, j, v, action;
   uint8_t new_key, new_keys, keylast;

   xreg(1, 0, 0, GFX_CANVAS_640x480);
   xram0_struct_set(GFX_STRUCT, vga_mode3_config_t, x_wrap, false);
   xram0_struct_set(GFX_STRUCT, vga_mode3_config_t, y_wrap, false);
   xram0_struct_set(GFX_STRUCT, vga_mode3_config_t, x_pos_px, 0);
   xram0_struct_set(GFX_STRUCT, vga_mode3_config_t, y_pos_px, 0);
   xram0_struct_set(GFX_STRUCT, vga_mode3_config_t, width_px, 640);
   xram0_struct_set(GFX_STRUCT, vga_mode3_config_t, height_px, 480);
   xram0_struct_set(GFX_STRUCT, vga_mode3_config_t, xram_data_ptr, GFX_DATA);
   xram0_struct_set(GFX_STRUCT, vga_mode3_config_t, xram_palette_ptr, 0xFFFF);

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
         printf(NEWLINE);
         return;
      default:
         break;
      }
   }
}
