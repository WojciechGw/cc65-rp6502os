/* ext-rx-echo.c - C89, cc65 (Picocomputer RP6502-RIA UART) */

#include <rp6502.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include "colors.h"

#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)

#define NEWLINE "\r\n"

#define APPVER "20251223.1803"
#define APPDIRDEFAULT "USB0:/SHELL/RX"
#define APP_MSG_TITLE "\x1b[1;1HOS Shell > Courier RX                                      version " APPVER
#define APP_MSG_START ANSI_DARK_GRAY "\x1b[3;1HWaiting for incoming data or [Esc] to exit " ANSI_RESET
    
typedef struct {
    volatile unsigned char READY; /* $FFE0 */
    volatile unsigned char TX;    /* $FFE1 */
    volatile unsigned char RX;    /* $FFE2 */
} ria_uart_t;

typedef unsigned int u16;

#define ria_push_char(v) RIA.xstack = v

#define RIA_BASE_ADDR ((u16)0xFFE0u)
#define RIA_PTR       ((ria_uart_t*)(RIA_BASE_ADDR))
#define RRIA          (*RIA_PTR)

#define RRIA_READY_TX_BIT 0x80u /* bit 7 */
#define RRIA_READY_RX_BIT 0x40u /* bit 6 */

#define RX_READY() (RRIA.READY & RRIA_READY_RX_BIT)
#define TX_READY() (RRIA.READY & RRIA_READY_TX_BIT)

/* timeout w sekundach */
#define RX_TIMEOUT_SECONDS 1
#define TICKS_PER_SEC 100

#define ESC 0x1B
#define SOH 0x01
#define STX 0x02
#define ETX 0x03
#define EOT 0x04

#define TAB         "\t"
#define CSI_RESET   "\x1b" "c"
#define CSI_CURSOR_SHOW "\x1b[?25h"
#define CSI_CURSOR_HIDE "\x1b[?25l"
#define CSI_CURSOR_AT "\x1b" "n20;m15H"

unsigned char buffer[256];
unsigned buf_len = 0;
unsigned char c;

// wait on clock
uint32_t ticks = 0; // for PAUSE(millis)
#define PAUSE(millis) ticks=clock(); while(clock() < (ticks + millis)){}

static u16 rx_count = 0;

/* Czeka aż TX FIFO nie będzie pełne i wysyła bajt. */
static void ria_tx_putc_blocking(unsigned char c)
{
    while (TX_READY() == 0) { }
    RRIA.TX = c;
}

static void ria_tx_puts(const char* s)
{
    while (*s) {
        ria_tx_putc_blocking((unsigned char)*s++);
    }
}

/* wypisuje u16 w ASCII (dziesiętnie) */
static void ria_tx_put_u16(u16 v)
{
    char buf[6];
    char i = 0;

    if (v == 0) {
        ria_tx_putc_blocking('0');
        return;
    }

    while (v != 0 && i < (char)sizeof(buf)) {
        buf[i++] = (char)('0' + (v % 10u));
        v /= 10u;
    }
    while (i) {
        ria_tx_putc_blocking((unsigned char)buf[--i]);
    }
}

/* Opróżnia RX FIFO (echo na terminal). */
static void drop_console_rx(void)
{
    while (RX_READY()) {
        unsigned char c = RRIA.RX;

        /* echo */
        if (c == '\n') ria_tx_putc_blocking('\r');
        ria_tx_putc_blocking(c);

        rx_count++;
    }
}

static int wait_for_marker(void)
{
    for (;;) {

        if (RX_READY()) {
            c = RRIA.RX;
            if (c == SOH) {
                ria_tx_puts(NEWLINE NEWLINE "SOH marker. Begin transmission." NEWLINE);
                return 1;
            } else if (c == STX) {
                ria_tx_puts(NEWLINE NEWLINE "STX marker. Start block of data." NEWLINE);
                return 1;

            } else if (c == ESC) {
                return -1;
            }
        }

    }
}

int main(void)
{
    clock_t start = clock();
    clock_t timeout_ticks = (clock_t)(RX_TIMEOUT_SECONDS * TICKS_PER_SEC);
    int action = 0;
    
    ria_tx_puts(CSI_RESET);
    ria_tx_puts(CSI_CURSOR_HIDE); // hide cursor
    ria_tx_puts(APP_MSG_TITLE);
    ria_tx_puts(APP_MSG_START);

    action = wait_for_marker(); // wait for STX to start transmission or [Esc]
    if (action != -1)
    {
        for (;;) {
            if (RX_READY()) {
                c = RRIA.RX;
                // end of transmission part

                // if (c == '\n') ria_tx_putc_blocking('\r');
                ria_tx_putc_blocking(c);
                buffer[buf_len++] = c;
                rx_count++;
                if (buf_len == sizeof(buffer)) {
                    buf_len = 0;
                }
                start = clock();
            } else {
                if ((clock() - start) >= timeout_ticks) {
                    break;
                }
            }
        }
    }
    
    switch(action)
    {
    case -1:
        ria_tx_puts(CSI_RESET "Bye, bye!" NEWLINE NEWLINE);
        break;
    case 0:
        ria_tx_puts("ERROR! Timeout or transmission error." NEWLINE NEWLINE);
        break;
    case 1:
        drop_console_rx();
        ria_tx_puts(">" NEWLINE NEWLINE "SUCCESS! End of transmission, ");
        ria_tx_put_u16(rx_count);
        ria_tx_puts(" bytes received" NEWLINE NEWLINE);
        if (buf_len < sizeof(buffer)) buffer[buf_len] = '\0';
        printf("%s", (char*)buffer);
        break;
    }
    ria_tx_puts(CSI_CURSOR_SHOW);
    return 0;

}
