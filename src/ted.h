#ifndef TED_H
#define TED_H

/* ================================================================
   ted.h — all #define constants for TEd text editor
   Sources: ted.c, commons.h, commons/csi.h, commons/ansi.h,
            commons/colors.h, commons/console.h, commons/helpers.h,
            commons/strings.h, commons/usb_hid_keys.h
   ================================================================ */

/* ------------------------------------------------------------------ */
/* XRAM layout
 *
 *  0x0000  TEXT_BUF_BASE      512 rows x 80 cols =  40960 B  (0x0000-0x9FFF)
 *  [ free: 0xA000-0xE3ED  ~17 KB ]
 *  0xE3EE  CLIP_BUF_BASE       32 rows x 80 cols =   2560 B  (0xE3EE-0xEDED)
 *  0xEDEE  SCREEN_CACHE_BASE   26 rows x 80 cols =   2080 B  (0xEDEE-0xF60D)
 *  0xF60E  XRAM_SCRATCH        file-write scratch =    82 B  (0xF60E-0xF65F)
 *  0xF660  XRAM_AREA_BUF_BASE  area backup      =    2400 B  (0xF660-0xFFBF)
 *  0xFFC0  MOUSE_INPUT         system mouse state =    32 B
 *  0xFFE0  KEYBOARD_INPUT      system key bitfield=    32 B
 */

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

/* --- app identity --- */
#define APPVER         "20260609.1949"
#define APPNAME        "TEd"
#define APPDESCRPTION1 "Text Editor"
#define APPDESCRPTION2 "for Picocomputer 6502"
#define APPCOPYRIGHT   "(c) 2026 by WojciechGw"
#define APP_MSG_TITLE CSI "1;1H" CSI "48;2;40;80;40m" " \xfe " APPNAME " " ANSI_RESET " " APPDESCRPTION1

/* --- autorepeat timings (clock() = centiseconds, 1 tick = 10 ms) --- */
#define REPEAT_DELAY      35u   /* 350 ms before first repeat */
#define REPEAT_RATE        3u   /* 30 ms between repeats */
#define REPEAT_RATE_FAST   1u   /* 10 ms between repeats (arrow keys) */

#define TEXT_BUF_BASE      0x0000u
#define TEXT_COLS          80u
#define TEXT_ROWS          512u
#define TEXT_BUF_END       ((TEXT_COLS * TEXT_ROWS)-1)

#define CLIP_MAX_LINES     32u
#define CLIP_BUF_BASE      0xE3EEu
#define SCREEN_CACHE_BASE  0xEDEEu
#define XRAM_SCRATCH       0xF60Eu
#define XRAM_AREA_BUF_BASE 0xF660u

/* system XRAM (from commons.h) */
#define MOUSE_INPUT        0xFFC0   /* 32 bytes mouse state */
#define KEYBOARD_INPUT     0xFFE0   /* 32 bytes keyboard bitfield */
#define GFX_STRUCT         0xFFC0u  /* VGA mode config (view.com only) */
#define GFX_DATA           0x2000u  /* framebuffer 640x480x1bpp (view.com only) */

/* XRAM register aliases used in ted.c */
#define XRAM_STRUCT_SYS_KEYBOARD  KEYBOARD_INPUT
#define XRAM_STRUCT_SYS_MOUSE     MOUSE_INPUT

/* ------------------------------------------------------------------ */
/* terminal dimensions (640x480, 16px font)                           */
#define TERM_ROWS    30u
#define TITLE_ROWS    2u
#define STATUS_ROWS     2u
#define EDIT_ROWS    (TERM_ROWS - TITLE_ROWS - STATUS_ROWS)   /* 26 */

/* ------------------------------------------------------------------ */
/* clipboard file backing store                                        */
#define CLIP_FILE       "TMP/ted_cb.dat"
#define CLIP_META_FILE  "TMP/ted_cb.meta"

/* ------------------------------------------------------------------ */
/* menu / status bar                                                   */
#define INFO_READY  "Ready"
#define CHAR_VBAR   "\xb3"
#define CHAR_HBAR   "\xc4"
#define MODE_CAPS   "A"
#define MODE_NCAPS  "a"
#define MODE_INS    "INS"
#define MODE_OVR    "OVR"
#define MODE_EDIT   "EDIT"
#define MODE_VIEW   "VIEW"
#define CLIPBOARD_WITHDATA "CLIP"

/* ------------------------------------------------------------------ */
/* selection modes                                                     */
#define SEL_MODE_LINE  0u
#define SEL_MODE_CHAR  1u

/* ------------------------------------------------------------------ */
/* list modes                                                          */
#define LIST_MODE_NONE    0u
#define LIST_MODE_NUM     1u   /* "1."  "2."  ... */
#define LIST_MODE_ALPHA   2u   /* "a)"  "b)"  ... "z)" "aa)" ... */
#define LIST_MODE_BULLET  3u   /* "* "  "- "  etc. */

/* ------------------------------------------------------------------ */
/* filename defaults                                                   */
#define NEW_FILENAME  APPNAME "-NewDocument.txt"

/* ------------------------------------------------------------------ */
/* keyboard                                                            */
#define KEYBOARD_BYTES  32
uint8_t keystates[KEYBOARD_BYTES] = {0};
#define key(code)  (keystates[(code) >> 3] & (1u << ((code) & 7)))

/* ================================================================
   From commons/csi.h
   ================================================================ */
#define ESC                 "\x1b"
#define CSI                 ESC "["
#define CSI_RESET           ESC "c"
#define CSI_CLS             CSI "2J"
#define CSI_CURSOR_SHOW     CSI "?25h"
#define CSI_CURSOR_HIDE     CSI "?25l"
#define CSI_CURSOR_SCP      CSI "c"
#define CSI_CURSOR_RCP      CSI "u"
#define CSI_CURSOR_HOME     CSI "1;1H"
#define CSI_ECHO_OFF        CSI "12h"
#define CSI_ECHO_ON         CSI "12l"

#define OSC                     ESC "]"
#define OSC_DEFAULT_COLORFG     OSC "10;#"
#define OSC_DEFAULT_COLORBG     OSC "11;#"
#define OSC_CURSOR_COLOR        OSC "12;#"
#define OSC_ST                  ESC "\\"
#define OSC_RESET_COLORFG       OSC "110" OSC_ST
#define OSC_RESET_COLORBG       OSC "111" OSC_ST
#define OSC_RESET_CURSOR_COLOR  OSC "112" OSC_ST

#define DECSCUSR_DEFAULT   CSI "0 q"
#define DECSCUSR_BLOCK     CSI "1 q"
#define DECSCUSR_UNDERLINE CSI "3 q"
#define DECSCUSR_BAR       CSI "5 q"

#define CSI_CURSOR_CUU_1   CSI "1A"
#define CSI_CURSOR_CUD_1   CSI "1B"
#define CSI_CURSOR_CUB_1   CSI "1D"
#define CSI_CURSOR_CUF_1   CSI "1C"

#define SO "\x0E"   /* Shift Out  - wybierz G1 */
#define SI "\x0F"    /* Shift In   - wybierz G0 */
#define CHAR_SO 0x0E   /* Shift Out  - wybierz G1 */
#define CHAR_SI 0x0F    /* Shift In   - wybierz G0 */
#define DECSC ESC "7"
#define DECRC ESC "8"
#define TERMINAL_MOTIVE_LIGHT OSC_DEFAULT_COLORFG "000000" OSC_DEFAULT_COLORBG "F0F0F0"
#define TERMINAL_MOTIVE_DARK OSC_DEFAULT_COLORFG "FFFFFF" OSC_DEFAULT_COLORBG "101010"
#define TERMINAL_MOTIVE TERMINAL_MOTIVE_LIGHT
#define ALTSCREEN_ENTER CSI "?1049h" CSI "?25l" CSI "0m" DECSCUSR_BAR CSI_ECHO_OFF TERMINAL_MOTIVE OSC_ST ESC "(\xB" ESC ")0"
#define ALTSCREEN_LEAVE CSI "0m" CSI "?25h" CSI_ECHO_ON CSI "?1049l"

/* ================================================================
   From commons/ansi.h
   ================================================================ */
#define ANSI_CLS        CSI "2J\x1b[H"
#define ANSI_RESET      CSI "0m"
#define ANSI_BOLD       CSI "1m"
#define ANSI_CYAN       CSI "36m"
#define ANSI_GREEN      CSI "32m"
#define ANSI_YELLOW     CSI "33m"
#define ANSI_WHITE      CSI "37m"
#define ANSI_MAGENTA    CSI "35m"
#define ANSI_RED        CSI "31m"
#define ANSI_DARK_GRAY  CSI "90m"

/* ANSI escape helpers (ted.c local extensions) */
#define ANSI_HOME       CSI "H"
#define ANSI_HIDE_CUR   CSI "?25l"
#define ANSI_SHOW_CUR   CSI "?25h"
#define ANSI_REVERSE    CSI "0;7m"
#define ANSI_NORMAL     CSI "0m"
#define ANSI_SEL_BG     CSI "48;2;60;60;60m"
#define ANSI_SEL_BG_QA  CSI "48;2;220;0;0m"
#define ANSI_SEL_BG_OFF CSI "49m"
#define DECSTBM_EDIT    CSI "3;28r"
#define DECSTBM_FULL    CSI "r"

/* ================================================================
   From commons/colors.h
   ================================================================ */
#define BLACK         0
#define DARK_RED      1
#define DARK_GREEN    2
#define BROWN         3
#define DARK_BLUE     4
#define DARK_MAGENTA  5
#define DARK_CYAN     6
#define LIGHT_GRAY    7
#define DARK_GRAY     8
#define RED           9
#define GREEN        10
#define YELLOW       11
#define BLUE         12
#define MAGENTA      13
#define CYAN         14
#define WHITE        15

#define HIGHLIGHT_COLOR  "97;42m"
#define CHAR_HIGHLIGHT   "0;7m"
#define CHAR_NORMAL      "7;0m"

/* ================================================================
   From commons/console.h
   ================================================================ */
#define CHAR_BELL   0x07
#define CHAR_BS     0x08
#define CHAR_CR     0x0D
#define CHAR_LF     0x0A
#define CHAR_ESC    0x1B
#define CHAR_NCHR   0x5B
#define CHAR_UP     0x41
#define CHAR_DOWN   0x42
#define CHAR_RIGHT  0x43
#define CHAR_LEFT   0x44
#define CHAR_F1     0x50
#define CHAR_F2     0x51
#define CHAR_F3     0x52
#define CHAR_F4     0x53

#define KEY_DEL  0x7F
#define TAB      "\t"
#define NEWLINE  "\r\n"

/* ================================================================
   From commons/helpers.h
   ================================================================ */
#define STR_HELPER(x)  #x
#define STR(x)         STR_HELPER(x)

uint32_t ticks = 0;
#define PAUSE(millis)  ticks = clock(); while (clock() < (ticks + millis)) {}

#define RX_READY       (RIA.ready & RIA_READY_RX_BIT)
#define TX_READY       (RIA.ready & RIA_READY_TX_BIT)
#define RX_READY_SPIN  while (!RX_READY)
#define TX_READY_SPIN  while (!TX_READY)

/* ================================================================
   From commons/strings.h
   ================================================================ */
#define EXCLAMATION  "[!] "

/* ================================================================
   From commons/usb_hid_keys.h
   ================================================================ */
#ifndef USB_HID_KEYS
#define USB_HID_KEYS

#define KEY_MOD_LCTRL   0x01
#define KEY_MOD_LSHIFT  0x02
#define KEY_MOD_LALT    0x04
#define KEY_MOD_LMETA   0x08
#define KEY_MOD_RCTRL   0x10
#define KEY_MOD_RSHIFT  0x20
#define KEY_MOD_RALT    0x40
#define KEY_MOD_RMETA   0x80

#define KEY_NONE     0x00
#define KEY_ERR_OVF  0x01

#define KEY_A  0x04
#define KEY_B  0x05
#define KEY_C  0x06
#define KEY_D  0x07
#define KEY_E  0x08
#define KEY_F  0x09
#define KEY_G  0x0a
#define KEY_H  0x0b
#define KEY_I  0x0c
#define KEY_J  0x0d
#define KEY_K  0x0e
#define KEY_L  0x0f
#define KEY_M  0x10
#define KEY_N  0x11
#define KEY_O  0x12
#define KEY_P  0x13
#define KEY_Q  0x14
#define KEY_R  0x15
#define KEY_S  0x16
#define KEY_T  0x17
#define KEY_U  0x18
#define KEY_V  0x19
#define KEY_W  0x1a
#define KEY_X  0x1b
#define KEY_Y  0x1c
#define KEY_Z  0x1d

#define KEY_1  0x1e
#define KEY_2  0x1f
#define KEY_3  0x20
#define KEY_4  0x21
#define KEY_5  0x22
#define KEY_6  0x23
#define KEY_7  0x24
#define KEY_8  0x25
#define KEY_9  0x26
#define KEY_0  0x27

#define KEY_ENTER      0x28
#define KEY_ESC        0x29
#define KEY_BACKSPACE  0x2a
#define KEY_TAB        0x2b
#define KEY_SPACE      0x2c
#define KEY_MINUS      0x2d
#define KEY_EQUAL      0x2e
#define KEY_LEFTBRACE  0x2f
#define KEY_RIGHTBRACE 0x30
#define KEY_BACKSLASH  0x31
#define KEY_HASHTILDE  0x32
#define KEY_SEMICOLON  0x33
#define KEY_APOSTROPHE 0x34
#define KEY_GRAVE      0x35
#define KEY_COMMA      0x36
#define KEY_DOT        0x37
#define KEY_SLASH      0x38
#define KEY_CAPSLOCK     0x39
#define KEY_CAPSLOCK_LED 0x02  /* Caps Lock LED state (RIA keycode 2, not key press) */

#define KEY_F1   0x3a
#define KEY_F2   0x3b
#define KEY_F3   0x3c
#define KEY_F4   0x3d
#define KEY_F5   0x3e
#define KEY_F6   0x3f
#define KEY_F7   0x40
#define KEY_F8   0x41
#define KEY_F9   0x42
#define KEY_F10  0x43
#define KEY_F11  0x44
#define KEY_F12  0x45

#define KEY_SYSRQ      0x46
#define KEY_SCROLLLOCK 0x47
#define KEY_PAUSE      0x48
#define KEY_INSERT     0x49
#define KEY_HOME       0x4a
#define KEY_PAGEUP     0x4b
#define KEY_DELETE     0x4c
#define KEY_END        0x4d
#define KEY_PAGEDOWN   0x4e
#define KEY_RIGHT      0x4f
#define KEY_LEFT       0x50
#define KEY_DOWN       0x51
#define KEY_UP         0x52

#define KEY_NUMLOCK    0x53
#define KEY_KPSLASH    0x54
#define KEY_KPASTERISK 0x55
#define KEY_KPMINUS    0x56
#define KEY_KPPLUS     0x57
#define KEY_KPENTER    0x58
#define KEY_KP1        0x59
#define KEY_KP2        0x5a
#define KEY_KP3        0x5b
#define KEY_KP4        0x5c
#define KEY_KP5        0x5d
#define KEY_KP6        0x5e
#define KEY_KP7        0x5f
#define KEY_KP8        0x60
#define KEY_KP9        0x61
#define KEY_KP0        0x62
#define KEY_KPDOT      0x63

#define KEY_102ND    0x64
#define KEY_COMPOSE  0x65
#define KEY_POWER    0x66
#define KEY_KPEQUAL  0x67

#define KEY_F13  0x68
#define KEY_F14  0x69
#define KEY_F15  0x6a
#define KEY_F16  0x6b
#define KEY_F17  0x6c
#define KEY_F18  0x6d
#define KEY_F19  0x6e
#define KEY_F20  0x6f
#define KEY_F21  0x70
#define KEY_F22  0x71
#define KEY_F23  0x72
#define KEY_F24  0x73

#define KEY_OPEN   0x74
#define KEY_HELP   0x75
#define KEY_PROPS  0x76
#define KEY_FRONT  0x77
#define KEY_STOP   0x78
#define KEY_AGAIN  0x79
#define KEY_UNDO   0x7a
#define KEY_CUT    0x7b
#define KEY_COPY   0x7c
#define KEY_PASTE  0x7d
#define KEY_FIND   0x7e
#define KEY_MUTE        0x7f
#define KEY_VOLUMEUP    0x80
#define KEY_VOLUMEDOWN  0x81

#define KEY_KPCOMMA      0x85
#define KEY_RO           0x87
#define KEY_KATAKANAHIRAGANA 0x88
#define KEY_YEN          0x89
#define KEY_HENKAN       0x8a
#define KEY_MUHENKAN     0x8b
#define KEY_KPJPCOMMA    0x8c

#define KEY_HANGEUL        0x90
#define KEY_HANJA          0x91
#define KEY_KATAKANA       0x92
#define KEY_HIRAGANA       0x93
#define KEY_ZENKAKUHANKAKU 0x94

#define KEY_KPLEFTPAREN  0xb6
#define KEY_KPRIGHTPAREN 0xb7

#define KEY_LEFTCTRL   0xe0
#define KEY_LEFTSHIFT  0xe1
#define KEY_LEFTALT    0xe2
#define KEY_LEFTMETA   0xe3
#define KEY_RIGHTCTRL  0xe4
#define KEY_RIGHTSHIFT 0xe5
#define KEY_RIGHTALT   0xe6
#define KEY_RIGHTMETA  0xe7

#define KEY_MEDIA_PLAYPAUSE    0xe8
#define KEY_MEDIA_STOPCD       0xe9
#define KEY_MEDIA_PREVIOUSSONG 0xea
#define KEY_MEDIA_NEXTSONG     0xeb
#define KEY_MEDIA_EJECTCD      0xec
#define KEY_MEDIA_VOLUMEUP     0xed
#define KEY_MEDIA_VOLUMEDOWN   0xee
#define KEY_MEDIA_MUTE         0xef
#define KEY_MEDIA_WWW          0xf0
#define KEY_MEDIA_BACK         0xf1
#define KEY_MEDIA_FORWARD      0xf2
#define KEY_MEDIA_STOP         0xf3
#define KEY_MEDIA_FIND         0xf4
#define KEY_MEDIA_SCROLLUP     0xf5
#define KEY_MEDIA_SCROLLDOWN   0xf6
#define KEY_MEDIA_EDIT         0xf7
#define KEY_MEDIA_SLEEP        0xf8
#define KEY_MEDIA_COFFEE       0xf9
#define KEY_MEDIA_REFRESH      0xfa
#define KEY_MEDIA_CALC         0xfb

#endif /* USB_HID_KEYS */

/* ================================================================
   From commons.h — GFX (view.com only, not used by ted.com)
   ================================================================ */
#define GFX_CANVAS_640x480  3
#define GFX_MODE_CONSOLE    0
#define GFX_MODE_BITMAP     3
#define GFX_BITMAP_bpp1     0b00000000
#define GFX_PLANE_0         0
#define GFX_PLANE_1         1
#define GFX_PLANE_2         2

#define PC_FB_ADDR        GFX_DATA
#define PC_FB_WIDTH       640u
#define PC_FB_HEIGHT      480u
#define PC_FB_STRIDE      80u
#define PC_FB_SIZE_BYTES  38400u

/* ------------------------------------------------------------------ */
/* window draw XRAM buffer alias                                       */
#define XRAM_AREA_BUF  XRAM_AREA_BUF_BASE

/* ------------------------------------------------------------------ */
/* menu_input action macro (uses locals: pos, len, buf, field, i, ch,
   input_row, plen, maxlen — must be used only inside menu_input)     */
#define MI_ACTION(c) do { \
    uint8_t _fire = 0u; \
    if ((c) == KEY_LEFT)      { if (pos > 0u) { pos--; _fire=1u; } } \
    else if ((c) == KEY_RIGHT){ if (pos < len) { pos++; _fire=1u; } } \
    else if ((c) == KEY_BACKSPACE) { \
        if (pos > 0u) { \
            for (i = pos - 1u; i < len - 1u; i++) buf[i] = buf[i + 1u]; \
            len--; pos--; buf[len] = 0; _fire=1u; \
            printf(CSI "%d;%dH" ANSI_SEL_BG, (int)input_row, (int)(plen + 1u)); \
            for (i = 0u; buf[i] && i < field; i++) putchar((uint8_t)buf[i]); \
            for (; i < field; i++) putchar(' '); \
            printf(ANSI_SEL_BG_OFF); \
        } \
    } else if ((c) == KEY_DELETE) { \
        if (pos < len) { \
            for (i = pos; i < len - 1u; i++) buf[i] = buf[i + 1u]; \
            len--; buf[len] = 0; _fire=1u; \
            printf(CSI "%d;%dH" ANSI_SEL_BG, (int)input_row, (int)(plen + 1u)); \
            for (i = 0u; buf[i] && i < field; i++) putchar((uint8_t)buf[i]); \
            for (; i < field; i++) putchar(' '); \
            printf(ANSI_SEL_BG_OFF); \
        } \
    } else { \
        ch = keycode_to_char((c), shift, caps, 0u); \
        if (ch && len < (uint8_t)(maxlen - 1u) && len < field) { \
            for (i = len; i > pos; i--) buf[i] = buf[i - 1u]; \
            buf[pos++] = ch; len++; buf[len] = 0; _fire=1u; \
            printf(CSI "%d;%dH" ANSI_SEL_BG, (int)input_row, (int)(plen + 1u)); \
            for (i = 0u; buf[i] && i < field; i++) putchar((uint8_t)buf[i]); \
            for (; i < field; i++) putchar(' '); \
            printf(ANSI_SEL_BG_OFF); \
        } \
    } \
    if (_fire) printf(CSI "%d;%dH", (int)input_row, (int)(plen + pos + 1u)); \
} while(0)

#endif /* TED_H */
