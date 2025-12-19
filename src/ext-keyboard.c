/*
    Picocomputer 6502 - Keyboard Status Visualiser
    Copyright (c) 2025 WojciechGw
*/

// #define VSYNCWAIT
// #define DEBUG

#include <rp6502.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>
#include "usb_hid_keys.h"
#include "colors.h"

#define VERSION "20251215.1928"

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

#define KB_VIEW_ROWS 7
#define KB_VIEW_COLS 80

#define CHAR_RIGHT "\x10"
#define CHAR_LEFT "\x11"
#define CHAR_UP "\x1e"
#define CHAR_DOWN "\x1f"

#define POS_APPHEADER "[1;1H" 
#define POS_KEYBOARD  "[3;1H"
#define POS_KEYPRESS  "[1;66H"
#define NEWLINE "\r\n"

#define HIGHLIGHT_COLOR "[37;41m" // white (37) on red (41)
// #define HIGHLIGHT_COLOR "[30;47m" // black (30) on white (47)

// wait on clock
uint32_t ticks = 0; // for PAUSE(millis)
#define PAUSE(millis) ticks=clock(); while(clock() < (ticks + millis)){}

#define RX_READY (RIA.ready & RIA_READY_RX_BIT)

typedef struct {
    uint8_t row;
    uint8_t col;
    uint8_t width;
    uint8_t code;
    const char *label;
} key_shape_t;

static bool keyboard_pressed[KB_VIEW_ROWS][KB_VIEW_COLS];

static const key_shape_t keyboard_shapes[] = {
    {0,  0, 5,  KEY_ESC,        "ESC"},
    {0,  6, 4,  KEY_F1,         "F1"},
    {0, 10, 4,  KEY_F2,         "F2"},
    {0, 14, 4,  KEY_F3,         "F3"},
    {0, 18, 4,  KEY_F4,         "F4"},
    {0, 23, 4,  KEY_F5,         "F5"},
    {0, 27, 4,  KEY_F6,         "F6"},
    {0, 31, 4,  KEY_F7,         "F7"},
    {0, 35, 4,  KEY_F8,         "F8"},
    {0, 40, 4,  KEY_F9,         "F9"},
    {0, 44, 5,  KEY_F10,        "F10"},
    {0, 49, 5,  KEY_F11,        "F11"},
    {0, 54, 5,  KEY_F12,        "F12"},
    {0, 60, 8,  KEY_SYSRQ,      "PrtScr"}, // Keyboard Print Screen
    {0, 68, 7,  KEY_SCROLLLOCK, "ScrLk"}, // Keyboard Scroll Lock
    {0, 75, 5,  KEY_PAUSE,      "Brk"}, // Keyboard Pause
    {2, 50, 5,  KEY_INSERT,     "Ins"}, // Keyboard Insert
    {2, 55, 5,  KEY_HOME,       "Hom"}, // Keyboard Home
    {2, 60, 5,  KEY_PAGEUP,     "PgU"}, // Keyboard Page Up
    {3, 50, 5,  KEY_DELETE,     "Del"}, // Keyboard Delete Forward
    {3, 55, 5,  KEY_END,        "End"}, // Keyboard End
    {3, 60, 5,  KEY_PAGEDOWN,   "PgD"}, // Keyboard Page Down
    {2,  0, 3,  KEY_GRAVE,      "`"},
    {2,  3, 3,  KEY_1,          "1"},
    {2,  6, 3,  KEY_2,          "2"},
    {2,  9, 3,  KEY_3,          "3"},
    {2, 12, 3,  KEY_4,          "4"},
    {2, 15, 3,  KEY_5,          "5"},
    {2, 18, 3,  KEY_6,          "6"},
    {2, 21, 3,  KEY_7,          "7"},
    {2, 24, 3,  KEY_8,          "8"},
    {2, 27, 3,  KEY_9,          "9"},
    {2, 30, 3,  KEY_0,          "0"},
    {2, 33, 3,  KEY_MINUS,      "-"},
    {2, 36, 3,  KEY_EQUAL,      "="},
    {2, 40, 9,  KEY_BACKSPACE,  "BackSpc"},
    {3,  0, 7,  KEY_TAB,        "Tab"},
    {3,  7, 3,  KEY_Q,          "Q"},
    {3, 10, 3,  KEY_W,          "W"},
    {3, 13, 3,  KEY_E,          "E"},
    {3, 16, 3,  KEY_R,          "R"},
    {3, 19, 3,  KEY_T,          "T"},
    {3, 22, 3,  KEY_Y,          "Y"},
    {3, 25, 3,  KEY_U,          "U"},
    {3, 28, 3,  KEY_I,          "I"},
    {3, 31, 3,  KEY_O,          "O"},
    {3, 34, 3,  KEY_P,          "P"},
    {3, 37, 3,  KEY_LEFTBRACE,  "["},
    {3, 40, 3,  KEY_RIGHTBRACE, "]"},
    {3, 44, 5,  KEY_BACKSLASH,  "\\"},
    {4,  0, 8,  KEY_CAPSLOCK,   "Caps"},
    {4,  8, 3,  KEY_A,          "A"},
    {4, 11, 3,  KEY_S,          "S"},
    {4, 14, 3,  KEY_D,          "D"},
    {4, 17, 3,  KEY_F,          "F"},
    {4, 20, 3,  KEY_G,          "G"},
    {4, 23, 3,  KEY_H,          "H"},
    {4, 26, 3,  KEY_J,          "J"},
    {4, 29, 3,  KEY_K,          "K"},
    {4, 32, 3,  KEY_L,          "L"},
    {4, 35, 3,  KEY_SEMICOLON,  ";"},
    {4, 38, 3,  KEY_APOSTROPHE, "'"},
    {4, 41, 8,  KEY_ENTER,      "Enter"},
    {5,  0, 10, KEY_LEFTSHIFT,  "Shift"},
    {5, 10, 3,  KEY_Z,          "Z"},
    {5, 13, 3,  KEY_X,          "X"},
    {5, 16, 3,  KEY_C,          "C"},
    {5, 19, 3,  KEY_V,          "V"},
    {5, 22, 3,  KEY_B,          "B"},
    {5, 25, 3,  KEY_N,          "N"},
    {5, 28, 3,  KEY_M,          "M"},
    {5, 31, 3,  KEY_COMMA,      ","},
    {5, 34, 3,  KEY_DOT,        "."},
    {5, 37, 3,  KEY_SLASH,      "/"},
    {5, 40, 9,  KEY_RIGHTSHIFT, "Shift"},
    {6,  0, 6,  KEY_LEFTCTRL,   "Ctrl"},
    {6,  6, 3,  KEY_LEFTMETA,   "\x03"},  
    {6,  9, 5,  KEY_LEFTALT,    "Alt"},
    {6, 14, 18, KEY_SPACE,      "Spacebar"},
    {6, 32, 5,  KEY_RIGHTALT,   "Alt"},
    {6, 37, 3,  KEY_COMPOSE,    "\x7f"},
    {6, 40, 3,  KEY_RIGHTMETA,  "\x03"},  
    {6, 43, 6,  KEY_RIGHTCTRL,  "Ctrl"},
    {5, 55, 5,  KEY_UP,         CHAR_UP},
    {6, 50, 5,  KEY_LEFT,       CHAR_LEFT},
    {6, 55, 5,  KEY_DOWN,       CHAR_DOWN},
    {6, 60, 5,  KEY_RIGHT,      CHAR_RIGHT},
    {2, 66, 5,  KEY_NUMLOCK,    "Num"},
    {2, 71, 3,  KEY_KPSLASH,    "/"},  // Keypad /
    {2, 74, 3,  KEY_KPASTERISK, "*"},  // Keypad *
    {2, 77, 3,  KEY_KPMINUS,    "-"},  // Keypad -
    {3, 68, 3,  KEY_KP7,        "7"},  // Keypad 7 and Home
    {3, 71, 3,  KEY_KP8,        "8"},  // Keypad 8 and Up Arrow
    {3, 74, 3,  KEY_KP9,        "9"},  // Keypad 9 and Page Up
    {3, 77, 3,  KEY_KPPLUS,     "+"},  // Keypad +
    {4, 68, 3,  KEY_KP4,        "4"},  // Keypad 4 and Left Arrow
    {4, 71, 3,  KEY_KP5,        "5"},  // Keypad 5
    {4, 74, 3,  KEY_KP6,        "6"},  // Keypad 6 and Right Arrow
    {5, 68, 3,  KEY_KP1,        "1"},  // Keypad 1 and End
    {5, 71, 3,  KEY_KP2,        "2"},  // Keypad 2 and Down Arrow
    {5, 74, 3,  KEY_KP3,        "3"},  // Keypad 3 and PageDn
    {6, 68, 3,  KEY_KP0,        "0"},  // Keypad 0 and Insert
    {6, 74, 3,  KEY_KPDOT,      "."},  // Keypad . and Delete
    {6, 77, 3,  KEY_KPENTER,    "="},  // Keypad ENTER
};

static char keyboard_canvas[KB_VIEW_ROWS][KB_VIEW_COLS + 1];
static uint8_t last_rendered_states[KEYBOARD_BYTES] = {0};
static bool keyboard_view_initialized = false;

static void drop_console_rx(void) {
    int i;
    while (RX_READY) i = RIA.rx;
}

static void clear_keyboard_canvas(void) {
    uint8_t r;
    for (r = 0; r < KB_VIEW_ROWS; r++) {
        memset(keyboard_canvas[r], ' ', KB_VIEW_COLS);
        keyboard_canvas[r][KB_VIEW_COLS] = 0;
        memset(keyboard_pressed[r], 0, KB_VIEW_COLS);
    }
}

static void draw_key_on_canvas(const key_shape_t *shape, bool pressed) {
    uint8_t row = shape->row, col = shape->col, w = shape->width;
    uint8_t label_len = (uint8_t)strlen(shape->label);
    uint8_t start;

    if (row >= KB_VIEW_ROWS || (col + w) > KB_VIEW_COLS) return;
    // if (label_len > (w - 2)) label_len = w - 2;

    keyboard_canvas[row][col] = (pressed ? '[' : ' ');
    keyboard_canvas[row][col + w - 1] = (pressed ? ']' : ' ');
    // memset(&keyboard_canvas[row][col + 1], pressed ? '=' : ' ', w - 2);

    start = col + 1 + ((w - 2 - label_len) / 2);
    if (label_len) {
        memcpy(&keyboard_canvas[row][start], shape->label, label_len);
        memset(&keyboard_pressed[row][start], pressed, label_len);
    }
    memset(&keyboard_pressed[row][col], pressed, w);
}

static void render_keyboard_view(void) {
    bool changed = !keyboard_view_initialized;
    uint8_t i, c;
    uint8_t rows = (uint8_t)(sizeof(keyboard_shapes) / sizeof(keyboard_shapes[0]));
    bool highlight;
    
    if (!changed) {
        for (i = 0; i < KEYBOARD_BYTES; i++) {
            if (keystates[i] != last_rendered_states[i]) { changed = true; break; }
        }
    }
    if (!changed) return;

    clear_keyboard_canvas();
    for (i = 0; i < rows; i++) {
        draw_key_on_canvas(&keyboard_shapes[i], key(keyboard_shapes[i].code));
    }

    printf("\x1b" POS_KEYBOARD);
    // without highlighting
    // for (i = 0; i < KB_VIEW_ROWS; i++) printf("%s\r\n", keyboard_canvas[i]);
    // for (i = 0; i < KEYBOARD_BYTES; i++) last_rendered_states[i] = keystates[i];

    highlight = false;
    for (i = 0; i < KB_VIEW_ROWS; i++) {
        for (c = 0; c < KB_VIEW_COLS; c++) {
            bool want = keyboard_pressed[i][c];
            if (want != highlight) {
                printf(want ? "\x1b" HIGHLIGHT_COLOR : "\x1b[0m");
                highlight = want;
            }
            putchar(keyboard_canvas[i][c]);
        }
        printf("\x1b[0m\r\n");
    }

    for (i = 0; i < KEYBOARD_BYTES; i++) last_rendered_states[i] = keystates[i];
    keyboard_view_initialized = true;
    printf(ANSI_DARK_GRAY "\x1b[12;1HEXIT - press and hold both Shift keys" ANSI_RESET);
}

int main(int argc, char **argv) {
    
	bool handled_key = false;
	uint8_t i,j,new_key,new_keys;
    #ifdef VSYNCWAIT 
    uint8_t v;
    #endif

    if(argc == 1 && strcmp(argv[0], "/?") == 0) {
        // notice lack of filename extension
        printf (NEWLINE 
                "OS Shell > Keyboard Visualiser " NEWLINE
                NEWLINE
                "EXIT - press and hold both Shift keys" NEWLINE
                NEWLINE
            ); 
        #ifdef DEBUG
        printf("--------------\r\nargc=%d\r\n", argc);
        for(i = 0; i < argc; i++) {
            printf("argv[%d]=\"%s\"\r\n", i, argv[i]);
        }
        #endif
        return 0;
    }

    printf("\x1b" "c" "\x1b[?25l");
    printf("\x1b" POS_APPHEADER "OS Shell > Keyboard Visualiser");
    
    #ifdef VSYNCWAIT 
    v = RIA.vsync;
    #endif

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
                    printf("\x1b" POS_KEYPRESS " keycode 0x%02X %s", code, (new_key ?  CHAR_DOWN : CHAR_UP));
                }
            }
            keystates[i] = new_keys;
        }
        
        if ((key(KEY_LEFTSHIFT) != 0) && (key(KEY_RIGHTSHIFT) != 0)) {
            break;
        }
        
        render_keyboard_view();

    }

    drop_console_rx();
    printf("\x1b" "c" "\x1b[?25h");
    return 0;

}
