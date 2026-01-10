#include <rp6502.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <fcntl.h>

static uint16_t screenaddr = 0x0000;
static uint16_t cur_screenaddr = 0x0000;
static char doc[]="--- BEGIN ---\0\
Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor\0\
incididunt ut labore et dolore magna aliqua. Ut enim ad minim veniam, quis\0\
nostrud exercitation ullamco laboris nisi ut aliquip ex ea commodo consequat.\0\
Duis aute irure dolor in reprehenderit in voluptate velit esse cillum dolore eu\0\
fugiat nulla pariatur. Excepteur sint occaecat cupidatat non proident, sunt in\0\
culpa qui officia deserunt mollit anim id est laborum.\0\
\0\
Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor\0\
incididunt ut labore et dolore magna aliqua. Ut enim ad minim veniam, quis\0\
nostrud exercitation ullamco laboris nisi ut aliquip ex ea commodo consequat.\0\
Duis aute irure dolor in reprehenderit in voluptate velit esse cillum dolore eu\0\
fugiat nulla pariatur. Excepteur sint occaecat cupidatat non proident, sunt in\0\
culpa qui officia deserunt mollit anim id est laborum.\0\
\0\
Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor\0\
incididunt ut labore et dolore magna aliqua. Ut enim ad minim veniam, quis\0\
nostrud exercitation ullamco laboris nisi ut aliquip ex ea commodo consequat.\0\
Duis aute irure dolor in reprehenderit in voluptate velit esse cillum dolore eu\0\
fugiat nulla pariatur. Excepteur sint occaecat cupidatat non proident, sunt in\0\
culpa qui officia deserunt mollit anim id est laborum.\0\
\0\
Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor\0\
incididunt ut labore et dolore magna aliqua. Ut enim ad minim veniam, quis\0\
nostrud exercitation ullamco laboris nisi ut aliquip ex ea commodo consequat.\0\
Duis aute irure dolor in reprehenderit in voluptate velit esse cillum dolore eu\0\
fugiat nulla pariatur. Excepteur sint occaecat cupidatat non proident, sunt in\0\
culpa qui officia deserunt mollit anim id est laborum.\0\
\0\
Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor\0\
incididunt ut labore et dolore magna aliqua. Ut enim ad minim veniam, quis\0\
nostrud exercitation ullamco laboris nisi ut aliquip ex ea commodo consequat.\0\
Duis aute irure dolor in reprehenderit in voluptate velit esse cillum dolore eu\0\
fugiat nulla pariatur. Excepteur sint occaecat cupidatat non proident, sunt in\0\
culpa qui officia deserunt mollit anim id est laborum.\0\
\0\
Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor\0\
incididunt ut labore et dolore magna aliqua. Ut enim ad minim veniam, quis\0\
nostrud exercitation ullamco laboris nisi ut aliquip ex ea commodo consequat.\0\
Duis aute irure dolor in reprehenderit in voluptate velit esse cillum dolore eu\0\
fugiat nulla pariatur. Excepteur sint occaecat cupidatat non proident, sunt in\0\
culpa qui officia deserunt mollit anim id est laborum.\0\
\0\
•Ü©à‰¢æ´§è®ù„‡Ωç\0\
A.D.2026\0\0--- END ---";

//void scrollup(uint16_t scraddr)
//{
    // xram0_struct_set(0xFF00, vga_mode1_config_t, xram_data_ptr, scraddr);
//}

void main()
{
    uint16_t i;
    uint16_t cur_col = 0;
    uint16_t cur_row = 0;
    uint16_t cur_row_xramadr = 0;
    uint8_t mouse_wheel = 0;
    uint8_t mouse_wheel_prev = 0;
    int mouse_wheel_change = 0;

    xreg_vga_canvas(3);

    xram0_struct_set(0xFF00, vga_mode1_config_t, x_wrap, false);
    xram0_struct_set(0xFF00, vga_mode1_config_t, y_wrap, false);
    xram0_struct_set(0xFF00, vga_mode1_config_t, x_pos_px, 0);
    xram0_struct_set(0xFF00, vga_mode1_config_t, y_pos_px, 0);
    xram0_struct_set(0xFF00, vga_mode1_config_t, width_chars, 80);
    xram0_struct_set(0xFF00, vga_mode1_config_t, height_chars, 60);
    xram0_struct_set(0xFF00, vga_mode1_config_t, xram_data_ptr, 0x0000);
    xram0_struct_set(0xFF00, vga_mode1_config_t, xram_palette_ptr, 0xFFFF);
    xram0_struct_set(0xFF00, vga_mode1_config_t, xram_font_ptr, 0xFFFF);

    xreg_vga_mode(1, 8, 0xFF00);
    xreg_ria_keyboard(0xFF10);
    xreg_ria_mouse(0xFFF0);
    xreg(0, 0, 0x01, 0xFFF0);

    RIA.addr0 = 0;
    RIA.step0 = 1;
    for (i = 0; i < (80*60); i++)
    {
        RIA.rw0 = ' ';
    }
    
    RIA.addr0 = 0;
    RIA.step0 = 1;
    for (i = 0; i < sizeof(doc); i++)
    {
        ++cur_col;
        if (doc[i] == NULL)
        {
            ++cur_row;
            cur_col = 0;
            cur_screenaddr = ((cur_row * 80) - 1);
            RIA.addr0 = cur_screenaddr;
        } 
        RIA.rw0 = doc[i];
    }
    RIA.rw0 = '*';

    cur_screenaddr = 0;
    // scroll(false, true);
    // printf("\x1b" "c");
    while(1){
        // check mouse wheel
        RIA.addr1 = 0xFFF3;
        RIA.step1 = 0;
        mouse_wheel = RIA.rw1;
        if (mouse_wheel_prev != mouse_wheel)
        {
            mouse_wheel_change = mouse_wheel - mouse_wheel_prev;
            mouse_wheel_prev = mouse_wheel;
            // printf("\x1b[3;1H%03i %03i %03i", mouse_wheel, mouse_wheel_prev, mouse_wheel_change);
            cur_screenaddr += mouse_wheel_change * 80;
            xram0_struct_set(0xFF00, vga_mode1_config_t, xram_data_ptr, cur_screenaddr);
        }
        // check keyboard
        RIA.addr0 = 0xFF10;
        RIA.step0 = 0;
        if (!(RIA.rw0 & 1)) break;
    }

    printf("\n");

}