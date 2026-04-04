#include <rp6502.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <time.h>
#include <ctype.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#include "./commons/csi.h"
#include "./commons/ansi.h"
#include "./commons/colors.h"
#include "./commons/console.h"
#include "./commons/usb_hid_keys.h"

#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)

// wait on clock
uint32_t ticks = 0; // for PAUSE(millis)
#define PAUSE(millis) ticks=clock(); while(clock() < (ticks + millis)){}

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