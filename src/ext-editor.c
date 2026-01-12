#include <rp6502.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <fcntl.h>
#include "commons.h"

#define XRAM_STRUCT_GFX_MENU     0xFF00
#define XRAM_STRUCT_GFX_EDITOR   0xFF10
#define XRAM_STRUCT_SYS_KEYBOARD 0xFF20
#define XRAM_STRUCT_SYS_MOUSE    0xFF40

#define GFX_CANVAS_CONSOLE      0b00000000
#define GFX_CANVAS_320x240      0b00000001
#define GFX_CANVAS_320x180      0b00000010
#define GFX_CANVAS_640x480      0b00000011
#define GFX_CANVAS_640x360      0b00000010

#define GFX_MODE1_FONTSIZE8     0b00000000
#define GFX_MODE1_FONTSIZE16    0b00001000

#define GFX_MODE1_COLORS_2      0b00000000
#define GFX_MODE1_COLORS_16R    0b00000001
#define GFX_MODE1_COLORS_16     0b00000010
#define GFX_MODE1_COLORS_256    0b00000011
#define GFX_MODE1_COLORS_32768  0b00000100

#define GFX_MODE1_COLOR_BLACK       0b0000
#define GFX_MODE1_COLOR_DARKRED     0b0001
#define GFX_MODE1_COLOR_DARKGREEN   0b0010
#define GFX_MODE1_COLOR_DARKYELLOW  0b0011
#define GFX_MODE1_COLOR_DARKBLUE    0b0100
#define GFX_MODE1_COLOR_DARKMAGENTA 0b0101
#define GFX_MODE1_COLOR_DARKCYAN    0b0110
#define GFX_MODE1_COLOR_GRAY        0b0111
#define GFX_MODE1_COLOR_DARKGRAY    0b1000
#define GFX_MODE1_COLOR_RED         0b1001
#define GFX_MODE1_COLOR_GREEN       0b1010
#define GFX_MODE1_COLOR_YELLOW      0b1011
#define GFX_MODE1_COLOR_MAGENTA     0b1100
#define GFX_MODE1_COLOR_CYAN        0b1101
#define GFX_MODE1_COLOR_WHITE       0b1111

#define KEYBOARD_BYTES 32
uint8_t keystates[KEYBOARD_BYTES] = {0};
// keystates[code>>3] gets contents from correct byte in array
// 1 << (code&7) moves a 1 into proper position to mask with byte contents
// final & gives 1 if key is pressed, 0 if not
#define key(code) (keystates[code >> 3] & (1 << (code & 7)))

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
¥†©ˆä¢˜¾«¤¨ãà—½\0\
A.D.2026\0\
\0\
Adam Mickiewicz\0\
\0\
PAN TADEUSZ\0\
czyli ostatni zajazd na Litwie.\0\
Historia szlachecka z roku 1811 i 1812 we dwunastu ksi©gach wierszem\0\
\0\
Litwo! Ojczyzno moja! ty jeste˜ jak zdrowie.\0\
Ile ci© trzeba ceni†, ten tylko si© dowie,\0\
Kto ci© straciˆ. Dzi˜ pi©kno˜† tw¥ w caˆej ozdobie\0\
Widz© i opisuj©, bo t©skni© po tobie.\0\
\0\
Panno —wi©ta, co Jasnej bronisz Cz©stochowy\0\
I w Ostrej ˜wiecisz Bramie! Ty, co gr¢d zamkowy\0\
Nowogr¢dzki ochraniasz z jego wiernym ludem!\0\
Jak mnie dziecko do zdrowia powr¢ciˆa˜ cudem\0\
(Gdy od pˆacz¥cej matki, pod Twoj¥ opiek©\0\
Ofiarowany, martw¥ podniosˆem powiek©\0\
I zaraz mogˆem pieszo do Twych ˜wi¥tyä progu\0\
I˜† za wr¢cone ¾ycie podzi©kowa† Bogu),\0\
Tak nas powr¢cisz cudem na Ojczyzny ˆono.\0\
\0\
Tymczasem przeno˜ moj¥ dusz© ut©sknion¥\0\
Do tych pag¢rk¢w le˜nych, do tych ˆ¥k zielonych,\0\
Szeroko nad bˆ©kitnym Niemnem rozci¥gnionych;\0\
Do tych p¢l malowanych zbo¾em rozmaitem,\0\
Wyzˆacanych pszenic¥, posrebrzanych ¾ytem;\0\
Gdzie bursztynowy ˜wierzop, gryka jak ˜nieg biaˆa,\0\
Gdzie panieäskim rumieäcem dzi©cielina paˆa,\0\
A wszystko przepasane jakby wst©g¥, miedz¥\0\
Zielon¥, na niej z rzadka ciche grusze siedz¥.\0\
[...]\0\
\0\
Rok wydania (pierwsze wydanie): 1834 (Pary¾)\0\
----END----";

static uint16_t screenaddr_base = 0x0000;
static uint16_t screenaddr_max = 0x9FFF;
static uint16_t screenaddr_base_menu = 0xA000;
static uint16_t screenaddr_current = 0x0000;
static uint16_t cur_col = 0;
static uint16_t cur_row = 0;

struct Cursor {
    uint8_t row;
    uint8_t col;
    uint16_t blink_counter;
};

static struct Cursor cur;

//void scrollup(uint16_t scraddr)
//{
    // xram0_struct_set(0xFF00, vga_mode1_config_t, xram_data_ptr, scraddr);
//}

void main()
{
    uint8_t temp = 0;
    uint16_t i = 0;
    uint16_t content_rows = 0;
    uint16_t screenaddr_end = 0;
    uint8_t mouse_wheel = 0;
    uint8_t mouse_wheel_prev = 0;
    int mouse_wheel_change = 0;
    bool handled_key = false;
	uint8_t k,j,new_key,new_keys;
    uint8_t v;

    cur.row = 0;
    cur.col = 0;
    cur.blink_counter = 0;

    // menu
    xram0_struct_set(XRAM_STRUCT_GFX_MENU, vga_mode1_config_t, x_wrap, false);
    xram0_struct_set(XRAM_STRUCT_GFX_MENU, vga_mode1_config_t, y_wrap, false);
    xram0_struct_set(XRAM_STRUCT_GFX_MENU, vga_mode1_config_t, x_pos_px, 0);
    xram0_struct_set(XRAM_STRUCT_GFX_MENU, vga_mode1_config_t, y_pos_px, 480-24);
    xram0_struct_set(XRAM_STRUCT_GFX_MENU, vga_mode1_config_t, width_chars , 80);
    xram0_struct_set(XRAM_STRUCT_GFX_MENU, vga_mode1_config_t, height_chars, 3);
    xram0_struct_set(XRAM_STRUCT_GFX_MENU, vga_mode1_config_t, xram_data_ptr, screenaddr_base_menu);
    xram0_struct_set(XRAM_STRUCT_GFX_MENU, vga_mode1_config_t, xram_palette_ptr, 0xFFFF);
    xram0_struct_set(XRAM_STRUCT_GFX_MENU, vga_mode1_config_t, xram_font_ptr, 0xFFFF);

    // editor
    xram0_struct_set(XRAM_STRUCT_GFX_EDITOR, vga_mode1_config_t, x_wrap, false);
    xram0_struct_set(XRAM_STRUCT_GFX_EDITOR, vga_mode1_config_t, y_wrap, false);
    xram0_struct_set(XRAM_STRUCT_GFX_EDITOR, vga_mode1_config_t, x_pos_px, 0);
    xram0_struct_set(XRAM_STRUCT_GFX_EDITOR, vga_mode1_config_t, y_pos_px, 0);
    xram0_struct_set(XRAM_STRUCT_GFX_EDITOR, vga_mode1_config_t, width_chars , 80);
    xram0_struct_set(XRAM_STRUCT_GFX_EDITOR, vga_mode1_config_t, height_chars, 60);
    xram0_struct_set(XRAM_STRUCT_GFX_EDITOR, vga_mode1_config_t, xram_data_ptr, screenaddr_base);
    xram0_struct_set(XRAM_STRUCT_GFX_EDITOR, vga_mode1_config_t, xram_palette_ptr, 0xFFFF);
    xram0_struct_set(XRAM_STRUCT_GFX_EDITOR, vga_mode1_config_t, xram_font_ptr, 0xFFFF);

    xreg_vga_canvas(GFX_CANVAS_640x480);
    xreg_vga_mode(1, GFX_MODE1_FONTSIZE16 | GFX_MODE1_COLORS_16, XRAM_STRUCT_GFX_EDITOR, 1,       0, 480-24);
    xreg_vga_mode(1, GFX_MODE1_FONTSIZE8   | GFX_MODE1_COLORS_16, XRAM_STRUCT_GFX_MENU  , 1,  480-24, 480   );
    xreg_ria_keyboard(XRAM_STRUCT_SYS_KEYBOARD);
    xreg_ria_mouse(XRAM_STRUCT_SYS_MOUSE);

    RIA.addr0 = screenaddr_base;
    RIA.step0 = 1;
    for (i = 0; i < (80 * 60 * 2); i++)
    {
        RIA.rw0 = ' ';
        RIA.rw0 = (GFX_MODE1_COLOR_DARKGRAY << 4) | GFX_MODE1_COLOR_WHITE;
    }
    
    RIA.addr0 = screenaddr_base_menu;
    RIA.step0 = 1;
    for (i = 0; i < 4 * 80; i++)
    {
        RIA.rw0 = ' ';
        RIA.rw0 = (GFX_MODE1_COLOR_DARKGREEN << 4) | GFX_MODE1_COLOR_WHITE;
    }

    RIA.addr0 = screenaddr_base_menu + 160;
    RIA.step0 = 2;
    RIA.rw0 = ' ';
    RIA.rw0 = 0x1E;
    RIA.rw0 = ' ';
    RIA.rw0 = 'M';
    RIA.rw0 = 'e';
    RIA.rw0 = 'n';
    RIA.rw0 = 'u';
    RIA.rw0 = ' ';
    
    RIA.addr0 = screenaddr_base;
    RIA.step0 = 2;
    screenaddr_end = screenaddr_base;
    for (i = 0; i < sizeof(doc); i++)
    {
        if (doc[i] == '\0')
        {
            ++cur_row;
            cur_col = 0;
            screenaddr_end = screenaddr_base + (cur_row * 160);
            RIA.addr0 = screenaddr_end;
            continue;
        }
        RIA.rw0 = doc[i];
        ++cur_col;
        ++screenaddr_end;
        if (cur_col >= 80)
        {
            ++cur_row;
            cur_col = 0;
        }
    }
    RIA.addr0 = screenaddr_max - 2;
    RIA.rw0 = '*';

    if (screenaddr_end > screenaddr_base) {
        content_rows = (uint16_t)((screenaddr_end - screenaddr_base + 79) / 160);
    } else {
        content_rows = 0;
    }

    screenaddr_current = screenaddr_base;
    mouse_wheel = 0;
    mouse_wheel_prev = 0;
    mouse_wheel_change = 0;

    RIA.addr1 = XRAM_STRUCT_SYS_MOUSE + 3;
    RIA.step1 = 0;
    mouse_wheel = RIA.rw1;
    
    if (screenaddr_end > (screenaddr_base + 80 * 60)) {
        screenaddr_max = screenaddr_end - (80 * 60);
    } else {
        screenaddr_max = screenaddr_base;
    }

    v = RIA.vsync;
    while(1){

        RIA.addr1 = XRAM_STRUCT_SYS_MOUSE + 3;
        RIA.step1 = 0;
        mouse_wheel = RIA.rw1;
        mouse_wheel_change = mouse_wheel_prev - mouse_wheel;

        if(mouse_wheel_change == 1 || mouse_wheel_change == -1)
        {
            int offset = mouse_wheel_change * 160;

            if (mouse_wheel_change < 0) {
                uint16_t bytes_to_sub = (uint16_t)(-offset);

                if (screenaddr_current >= (screenaddr_base + bytes_to_sub)) {
                    screenaddr_current -= bytes_to_sub;
                } else {
                    screenaddr_current = screenaddr_base;
                }
            } 
            else {
               
                screenaddr_current += (uint16_t)offset;
                
            }

            // Aktualizacja rejestru wideo
            xram0_struct_set(XRAM_STRUCT_GFX_EDITOR, vga_mode1_config_t, xram_data_ptr, screenaddr_current);
            // printf("%04X %04X\r\n", screenaddr_base, screenaddr_current);
            // printf("Wheel: %03i %03i %i\r\n", mouse_wheel, mouse_wheel_prev, mouse_wheel_change);
        }
        mouse_wheel_prev = mouse_wheel;
        
        ++cur.blink_counter;
        if(cur.blink_counter == 60){
        // if(RIA.vsync == v){
            RIA.addr0 = screenaddr_base + cur.row * 160 + cur.col * 2 + 1;
            RIA.step0 = 0;
            temp = RIA.rw0;
            RIA.rw0 = temp << 4 | temp >> 4;
            cur.blink_counter = 0;
            // v = RIA.vsync;
        }

        // check keyboard
        RIA.addr1 = XRAM_STRUCT_SYS_KEYBOARD;
        RIA.step1 = 0;
        for (k = 0; k < KEYBOARD_BYTES; k++) {
            RIA.addr1 = XRAM_STRUCT_SYS_KEYBOARD + k;
            new_keys = RIA.rw1;
            for (j = 0; j < 8; j++) {
                uint8_t code = (k << 3) + j;
                new_key = (new_keys & (1 << j));
                if ((code > 3) && (new_key != (keystates[k] & (1 << j)))) {
                    // printf("\x1b" POS_KEYPRESS "0x%02X %s", code, (new_key ?  FONT_CHAR_DOWN : FONT_CHAR_UP));
                }
            }
            keystates[k] = new_keys;
        }
        
        if ((key(KEY_LEFTSHIFT) != 0) && (key(KEY_RIGHTSHIFT) != 0)) {
            break;
        }
        
        // break on any key : if (!(RIA.rw0 & 1)) break;
    }

    printf("\n");

}