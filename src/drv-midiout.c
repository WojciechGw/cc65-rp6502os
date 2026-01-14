#include "rp6502.h"
#include "stdbool.h"
#include "stdint.h"
#include "drv-midiout.h"
#include "commons/usb_hid_keys.h"

// Keyboard related
//
// XRAM locations
#define KEYBOARD_INPUT 0xFFE0 // KEYBOARD_BYTES of bitmask data
// 256 bytes HID code max, stored in 32 uint8
#define KEYBOARD_BYTES 32
uint8_t keystates[KEYBOARD_BYTES] = {0};
// keystates[code>>3] gets contents from correct byte in array
// 1 << (code&7) moves a 1 into proper position to mask with byte contents
// final & gives 1 if key is pressed, 0 if not
#define key(code) (keystates[code >> 3] & (1 << (code & 7)))

static void midi_note_on(uint8_t ch, uint8_t note, uint8_t vel) {
    (void)midi_tx_put(0x90 | (ch & 0x0F));
    (void)midi_tx_put(note);
    (void)midi_tx_put(vel);
}

int main(void) {

  	bool handled_key = false;
	uint8_t i,j,new_key,new_keys;

    // midi_init();

    midi_note_on(0x90, 60, 0x7F);

    while (true) {

        #ifdef VSYNCWAIT 
        if(RIA.vsync == v) continue;
        v = RIA.vsync;
        #endif

        xregn( 0, 0, 0, 1, KEYBOARD_INPUT);
        RIA.addr0 = KEYBOARD_INPUT;
        RIA.step0 = 0;

        for (i = 0; i < KEYBOARD_BYTES; i++) {
            RIA.addr0 = KEYBOARD_INPUT + i;
            new_keys = RIA.rw0;
            for (j = 0; j < 8; j++) {
                uint8_t code = (i << 3) + j;
                new_key = (new_keys & (1<<j));
                if ((code>3) && (new_key != (keystates[i] & (1<<j)))) {
                    // printf("\x1b" POS_KEYPRESS "0x%02X %s", code, (new_key ?  FONT_CHAR_DOWN : FONT_CHAR_UP));
                }
            }
            keystates[i] = new_keys;
        }
        
        if ((key(KEY_LEFTSHIFT) != 0) && (key(KEY_RIGHTSHIFT) != 0)) {
            break;
        }
        
    }

}
