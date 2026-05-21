#include <rp6502.h>
#include <stdio.h>

#define ESC "\x1b"
#define CSI ESC "["

// #define BASIC
// #define ALTSCREEN
#define DECTEST

#ifdef DECTEST

#define SO "\x0E"   /* Shift Out  - wybierz G1 */
#define SI "\x0F"    /* Shift In   - wybierz G0 */

#define CHAR_SO 0x0E   /* Shift Out  - wybierz G1 */
#define CHAR_SI 0x0F    /* Shift In   - wybierz G0 */

static void enter_alt_screen(void)
{
    printf(CSI "?1049h");  /* alternate screen on */
    printf(CSI "?25l");    /* hide cursor */
    printf(CSI "0m");      /* reset attributes */
    printf(CSI "2J");      /* clear screen */
    printf(CSI "H");       /* home */
}

static void leave_alt_screen(void)
{
    printf(CSI "0m");
    printf(CSI "?25h");    /* show cursor */
    printf(CSI "?1049l");  /* alternate screen off */
}

static void load_dec_special_graphics_to_g1(void)
{
    printf("\x1b(B");   /* G1 = DEC Special Graphics */
}

static void select_g0(void)
{
    printf(SI);
}

static void select_g1(void)
{
    printf(SO);
}


int main(void)
{
    unsigned char c;
    unsigned char col;

    // printf(CSI "0 q"); // cursor system default
    // printf(CSI "1 q"); // cursor as a block
    // printf(CSI "3 q"); // cursor as a underline
    // printf(CSI "5 q"); // cursor as a bar

    enter_alt_screen();
    
    // set font banks
    printf("\x1b(\xB"); /* G0 = ASCII */
    printf("\x1b)0");   /* G1 = DEC Special Graphics */
    
    printf("\r\nnow SO G1 DEC active\r\n");
    printf(SO); /* SO: aktywny G1 */

    printf("lqqqk\r\n");
    printf("x   x\r\n");
    printf("mqqqj\r\n");

    printf(" abcdefghijklmnopqrstuvwxyz\r\n"); // a = dotblock, f = degree, g = plus/minus, y = <=, z = >=
    printf(" ABCDEFGHIJKLMNOPQRSTUVWXYZ\r\n");
    printf(" 0123456789\r\n");
    printf(" !@#$%^&*()_+-={}[];:""'|\\\r\n"); // { = pi, } = pound, _ = space

    printf(SI); /* SI: aktywny G0 */
    printf("\r\nnow SI and back to ASCII\r\n");

    getchar(); // wait for Enter

    leave_alt_screen();

    enter_alt_screen();

    select_g0();

    printf("Picocomputer 6502 / VGA terminal - G1 DEC Special Graphics\r\n");
    printf("Codes 0x20 - 0x7E\r\n\r\n");

    col = 0;

    for (c = 0x20; c <= 0x7E; ++c) {

        select_g0();
        printf("%02X [", c);

        select_g1();
        putchar(c);

        select_g0();
        printf("]  ");

        ++col;
        if (col == 4) {
            printf("\r\n");
            col = 0;
        }
    }

    select_g0();

    printf("\r\n\r\nPress Enter to return...");
    getchar(); // wait for Enter

    leave_alt_screen();

    return 0;
}
#endif

#ifdef ALTSCREEN
#define ALT_SCREEN_ON   CSI "?1049h"
#define ALT_SCREEN_OFF  CSI "?1049l"
#define CURSOR_HIDE     CSI "?25l"
#define CURSOR_SHOW     CSI "?25h"
#define CLEAR_SCREEN    CSI "2J"
#define HOME            CSI "H"
#define RESET_ATTR      CSI "0m"

int main(void)
{
    /* Wejście do trybu aplikacyjnego */
    printf(ALT_SCREEN_ON);
    printf(CURSOR_HIDE);
    printf(CLEAR_SCREEN);
    printf(HOME);

    printf(CSI "1;37;44m");
    printf(" Picocomputer 6502 - alternate screen demo ");
    printf(RESET_ATTR);

    printf(CSI "3;1H");
    printf("To jest osobny ekran aplikacji.");

    printf(CSI "5;1H");
    printf(CSI "7m Nacisnij Enter, aby wrocic do poprzedniego ekranu... ");

    getchar();

    /* Przywrócenie normalnego ekranu */
    printf(CURSOR_SHOW);
    printf(RESET_ATTR);
    printf(ALT_SCREEN_OFF);

    return 0;
}
#endif

#ifdef BASIC
#define ESC "\033"

int main(void)
{
    printf(ESC ")0");     /* G1 = DEC Special Graphics */

    putchar(0x0E);        /* SO: przełącz na G1 */
    printf("lqqqqk\n");   /* znaki DEC: narożniki i linie */
    printf("xMENUx\n");
    printf("mqqqqj\n");
    putchar(0x0F);        /* SI: wróć do G0 */
#define  TESTCHAR "\xB2"
    printf("\033[30m" TESTCHAR); /* black foreground */
    printf("\033[31m" TESTCHAR); /* red */
    printf("\033[32m" TESTCHAR); /* green */
    printf("\033[33m" TESTCHAR); /* yellow */
    printf("\033[34m" TESTCHAR); /* blue */
    printf("\033[35m" TESTCHAR); /* magenta */
    printf("\033[36m" TESTCHAR); /* cyan */
    printf("\033[37m" TESTCHAR); /* white */
    printf("\033[40m" TESTCHAR); /* black background */
    printf("\033[44m" TESTCHAR); /* blue background */
    printf("\033[90m" TESTCHAR);  /* bright black / gray */
    printf("\033[97m" TESTCHAR);  /* bright white */
    printf("\033[100m" TESTCHAR); /* bright black background */
    printf("\n");

    printf("\033[38;5;220mYellow-orange 256-color text\033[0m\n");
    printf("\033[38;2;255;128;0mTruecolor orange text\033[0m\n");

    return 0;
}
#endif