#include <rp6502.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <fcntl.h>

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
•Ü©à‰¢òæ´§è®ù„‡óΩç\0\
A.D.2026\0\
\0\
--- END ---";

//void scrollup(uint16_t scraddr)
//{
    // xram0_struct_set(0xFF00, vga_mode1_config_t, xram_data_ptr, scraddr);
//}

void main()
{
    uint16_t i;
    uint16_t cur_col = 0;
    uint16_t cur_row = 0;
    uint16_t content_rows = 0;
    uint16_t screenaddr_end = 0;
    uint8_t mouse_wheel = 0;
    uint8_t mouse_wheel_prev = 0;
    int mouse_wheel_change = 0;
    uint16_t screenaddr_base = 0x0000;
    uint16_t screenaddr_current = 0x0000;
    uint16_t screenaddr_max = 0x0000;

    xram0_struct_set(0xFF00, vga_mode1_config_t, x_wrap, false);
    xram0_struct_set(0xFF00, vga_mode1_config_t, y_wrap, false);
    xram0_struct_set(0xFF00, vga_mode1_config_t, x_pos_px, 0);
    xram0_struct_set(0xFF00, vga_mode1_config_t, y_pos_px, 0);
    xram0_struct_set(0xFF00, vga_mode1_config_t, width_chars, 80);
    xram0_struct_set(0xFF00, vga_mode1_config_t, height_chars, 60);
    xram0_struct_set(0xFF00, vga_mode1_config_t, xram_data_ptr, screenaddr_base);
    xram0_struct_set(0xFF00, vga_mode1_config_t, xram_palette_ptr, 0xFFFF);
    xram0_struct_set(0xFF00, vga_mode1_config_t, xram_font_ptr, 0xFFFF);

    xreg_vga_canvas(3);
    xreg_vga_mode(1, 8, 0xFF00);
    xreg_ria_keyboard(0xFF10);
    xreg_ria_mouse(0xFFF0);
    xreg(0, 0, 0x01, 0xFFF0);

    RIA.addr0 = screenaddr_base;
    RIA.step0 = 1;
    for (i = 0; i < (80*60); i++)
    {
        RIA.rw0 = ' ';
    }
    
    RIA.addr0 = screenaddr_base;
    RIA.step0 = 1;
    screenaddr_end = screenaddr_base;
    for (i = 0; i < sizeof(doc); i++)
    {
        if (doc[i] == '\0')
        {
            ++cur_row;
            cur_col = 0;
            screenaddr_end = screenaddr_base + (cur_row * 80);
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
    RIA.rw0 = '*';

    if (screenaddr_end > screenaddr_base) {
        content_rows = (uint16_t)((screenaddr_end - screenaddr_base + 79) / 80);
    } else {
        content_rows = 0;
    }

    screenaddr_current = screenaddr_base;
    mouse_wheel = 0;
    mouse_wheel_prev = 0;
    mouse_wheel_change = 0;

    // xram0_struct_set(0xFF00, vga_mode1_config_t, xram_data_ptr, screenaddr_base);

    RIA.addr1 = 0xFFF3;
    RIA.step1 = 0;
    mouse_wheel = RIA.rw1;
    
    // Obliczamy max: koniec tekstu minus jeden ekran (60 wierszy * 80 znak¢w)
    // Warunek if zapobiega przekr©ceniu, jeòli tekst jest kr¢tszy niæ ekran.
    if (screenaddr_end > (screenaddr_base + 80*60)) {
        screenaddr_max = screenaddr_end - (80*60);
    } else {
        screenaddr_max = screenaddr_base;
    }
    
    while(1){

        RIA.addr1 = 0xFFF3;
        RIA.step1 = 0;
        mouse_wheel = RIA.rw1;
        mouse_wheel_change = mouse_wheel_prev - mouse_wheel;

        if(mouse_wheel_change == 1 || mouse_wheel_change == -1)
        {
            // Obliczamy przesuni©cie w bajtach (80 znak¢w na lini©)
            // Uæywamy int, aby zachowaÜ znak +/-
            int offset = mouse_wheel_change * 80;

            if (mouse_wheel_change < 0) {
                // --- PRZEWIJANIE W G‡R® (zmniejszanie adresu) ---
                
                // Zamieniamy ujemny offset na dodatni• liczb© bajt¢w do odj©cia
                uint16_t bytes_to_sub = (uint16_t)(-offset);

                // Sprawdzamy, czy odj©cie nie spowoduje zejòcia poniæej bazy
                // Warunek: Czy obecny adres jest wystarczaj•co duæy, by odj•Ü?
                if (screenaddr_current >= (screenaddr_base + bytes_to_sub)) {
                    screenaddr_current -= bytes_to_sub;
                } else {
                    // Jeòli chcemy odj•Ü wi©cej niæ moæemy, ustawiamy na baz© (pocz•tek)
                    screenaddr_current = screenaddr_base;
                }
            } 
            else {
                // --- PRZEWIJANIE W D‡ù (zwi©kszanie adresu) ---
                
                // Tutaj warto dodaÜ sprawdzenie screenaddr_max, 
                // ale skupiaj•c si© na Twoim pytaniu (minimum):
               
                screenaddr_current += (uint16_t)offset;
                
                // Opcjonalnie ograniczenie z g¢ry (jeòli obliczysz screenaddr_max):
                // if (screenaddr_current > screenaddr_max) screenaddr_current = screenaddr_max;
            }

            // Aktualizacja rejestru wideo
            xram0_struct_set(0xFF00, vga_mode1_config_t, xram_data_ptr, screenaddr_current);
            printf("%04X %04X\r\n", screenaddr_base, screenaddr_current);
            printf("Wheel: %03i %03i %i\r\n", mouse_wheel, mouse_wheel_prev, mouse_wheel_change);
        }
        mouse_wheel_prev = mouse_wheel;

        // check keyboard
        RIA.addr0 = 0xFF10;
        RIA.step0 = 0;
        if (!(RIA.rw0 & 1)) break;
    }

    printf("\n");

}