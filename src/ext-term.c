/*
 * Copyright (c) 2025 Rumbledethumps
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * SPDX-License-Identifier: Unlicense
 */

#include <rp6502.h>
#include <stdbool.h>
#include <stdio.h>
#include <fcntl.h>
#include <stdint.h>
#include "usb_hid_keys.h"

// An extremely simple terminal for the Pico RIA W modem.
// Uses the terminal built in to the Pico VGA.

// Keyboard related
//
// XRAM locations
#define KEYBOARD_INPUT 0xFF10 // KEYBOARD_BYTES of bitmask data
// 256 bytes HID code max, stored in 32 uint8
#define KEYBOARD_BYTES 32
uint8_t keystates[KEYBOARD_BYTES] = {0};
// keystates[code>>3] gets contents from correct byte in array
// 1 << (code&7) moves a 1 into proper position to mask with byte contents
// final & gives 1 if key is pressed, 0 if not
#define key(code) (keystates[code >> 3] & (1 << (code & 7)))

// wait on clock
uint32_t ticks = 0; // for PAUSE(millis)
#define PAUSE(millis) ticks=clock(); while(clock() < (ticks + millis)){}

#define RX_READY (RIA.ready & RIA_READY_RX_BIT)

void print(char *s)
{
    while (*s)
        if (RIA.ready & RIA_READY_TX_BIT)
            RIA.tx = *s++;
}

static void drop_console_rx(void) {
    int i;
    while (RX_READY) i = RIA.rx;
}

int main(int argc, char **argv)
{
    char rx_char, tx_char;
    bool rx_mode, tx_mode;
    int fd, cp;
	bool handled_key = false;
	uint8_t i,j,new_key,new_keys;

    {
        int i;
        printf("\r\n--------------\r\nargc=%d\r\n", argc);
        for(i = 0; i < argc; i++) {
            printf("argv[%d]=\"%s\"\r\n", i, argv[i]);
        }
    }

    cp = code_page(437);
    if (cp != 437)
    {
        print("Code page 437 not found.\r\n");
    }

    fd = open("AT:", 0);
    if (fd < 0)
    {
        print("Modem not found.\r\n");
        return -1;
    }
    print("Modem online.\r\n");

    
    xregn( 0, 0, 0, 1, KEYBOARD_INPUT);
    RIA.addr0 = KEYBOARD_INPUT;
    RIA.step0 = 0;

    while (true)
    {

        for (i = 0; i < KEYBOARD_BYTES; i++) {
            RIA.addr0 = KEYBOARD_INPUT + i;
            new_keys = RIA.rw0;
            for (j = 0; j < 2; j++) {
                // uint8_t code = (i << 3) + j;
                new_key = (new_keys & (1<<j));
                // if ((code>3) && (new_key != (keystates[i] & (1<<j)))) {
                //    printf("\x1b" POS_KEYPRESS " keycode 0x%02X %s", code, (new_key ?  CHAR_DOWN : CHAR_UP));
                //}
            }
            keystates[i] = new_keys;
        }
        
        if ((key(KEY_LEFTSHIFT) != 0) && (key(KEY_RIGHTSHIFT) != 0)) {
            break;
        }

        if (!rx_mode)
        {
            ria_push_char(1);
            ria_set_ax(fd);
            rx_mode = ria_call_int(RIA_OP_READ_XSTACK);
            rx_char = ria_pop_char();
        }
        else if ((RIA.ready & RIA_READY_TX_BIT))
        {
            RIA.tx = rx_char;
            rx_mode = false;
        }

        if (tx_mode)
        {
            ria_push_char(tx_char);
            ria_set_ax(fd);
            tx_mode = !ria_call_int(RIA_OP_WRITE_XSTACK);
        }
        else if (RIA.ready & RIA_READY_RX_BIT)
        {
            tx_char = RIA.rx;
            tx_mode = true;
        }
    }

    close(fd);
    drop_console_rx();
    printf("\x1b" "c" "\x1b[?25h");
    return 0;

}
