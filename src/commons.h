#include <rp6502.h>
#include <ctype.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "./commons/csi.h"
#include "./commons/ansi.h"
#include "./commons/colors.h"
#include "./commons/console.h"
#include "./commons/usb_hid_keys.h"

/* GFX subsystem setup */
#define GFX_CANVAS_640x480 3
#define GFX_MODE_CONSOLE   0
#define GFX_MODE_BITMAP    3
#define GFX_BITMAP_bpp1    0b00000000
#define GFX_PLANE_0        0
#define GFX_PLANE_1        1
#define GFX_PLANE_2        2

/* XRAM memory map */
#define GFX_STRUCT 0xFFC0u
#define GFX_DATA   0x2000u

/* 640x480, 1 bpp */
#define PC_FB_ADDR          GFX_DATA
#define PC_FB_WIDTH         640u
#define PC_FB_HEIGHT        480u
#define PC_FB_STRIDE        80u
#define PC_FB_SIZE_BYTES    38400u

#define EXCLAMATION "[!] "

#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)

// wait on clock
uint32_t ticks = 0; // for PAUSE(millis)
#define PAUSE(millis) ticks=clock(); while(clock() < (ticks + millis)){}

// Keyboard related
//
// XRAM locations
#define KEYBOARD_INPUT 0xFFE0 // KEYBOARD_BYTES of bitmask data
#define KEYBOARD_BYTES 32
#ifdef _NEED_KEYSTATES
static uint8_t keystates[KEYBOARD_BYTES] = {0};
#define key(code) (keystates[code >> 3] & (1 << (code & 7)))
#endif

/* ---- TX helpers --------------------------------------------------------- */
#define RX_READY (RIA.ready & RIA_READY_RX_BIT)
#define TX_READY (RIA.ready & RIA_READY_TX_BIT)
#define RX_READY_SPIN while (!RX_READY)
#define TX_READY_SPIN while (!TX_READY)
