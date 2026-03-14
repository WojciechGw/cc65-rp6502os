/* ext-rx.c - C89, cc65 (Picocomputer RP6502-RIA UART) */

#include "commons.h"

#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)

#define NEWLINE "\r\n"

#define APPVER "20260314.2"
#define APPDIRDEFAULT "MSC0:/SHELL/RX"
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

/* XRAM receive buffer — calkowity odbiur bez OS calls podczas transmisji */
#define XRAM_RX_BASE  0x0000u
#define XRAM_RX_MAX   0xEC00u  /* 60 KB — bezpieczny margines pod 0xF000 */

unsigned char c;

/* licznik odebranych bajtow */
static u16 rx_count = 0;

/* Czeka az TX FIFO nie bedzie pelne i wysyla bajt. */
static void ria_tx_putc_blocking(unsigned char b)
{
    while (TX_READY() == 0) { }
    RRIA.TX = b;
}

static void ria_tx_puts(const char* s)
{
    while (*s) {
        ria_tx_putc_blocking((unsigned char)*s++);
    }
}

/* wypisuje u16 w ASCII (dziesietnie) */
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

/* Opróznia RX FIFO (echo na terminal) — uzywac tylko gdy UART juz milczy. */
static void drop_console_rx(void)
{
    while (RX_READY()) {
        unsigned char ch = RRIA.RX;
        if (ch == '\n') ria_tx_putc_blocking('\r');
        ria_tx_putc_blocking(ch);
        rx_count++;
    }
}

/*
 * Czeka na marker startu/stopu.
 * Zwraca marker (SOH/STX/EOT) lub -1 jesli ESC.
 * UWAGA: nie drukuje nic po wykryciu markera — zerowy czas do pętli odbiorczej.
 */
static int wait_for_marker(void)
{
    for (;;) {
        if (RX_READY()) {
            c = RRIA.RX;
            if (c == SOH || c == STX || c == EOT) return (int)c;
            if (c == ESC) return -1;
        }
    }
}

int main(void)
{
    clock_t start;
    clock_t timeout_ticks = (clock_t)(RX_TIMEOUT_SECONDS * TICKS_PER_SEC);
    int action = 0;
    int fd;
    u16 xram_rx_len = 0;
    int marker;

    ria_tx_puts(CSI_RESET);
    ria_tx_puts(CSI_CURSOR_HIDE);
    ria_tx_puts(APP_MSG_TITLE);
    ria_tx_puts(APP_MSG_START);

    fd = open("MSC0:/RX/rx.ihx", O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) {
        ria_tx_puts("ERROR: cannot open MSC0:/RX/rx.ihx" NEWLINE);
        return -1;
    }

    marker = wait_for_marker();
    if (marker == -1) {
        action = -1;
        goto done;
    }

    /* Jesli marker to EOT od razu — plik pusty */
    if (marker == EOT) {
        action = 1;
        goto save;
    }

    /* ------------------------------------------------------------------ *
     * Petla odbiorcza — zadnych OS calls, zadnego echa.                  *
     * Kazdy bajt trafia bezposrednio do XRAM przez portal (auto-inc).    *
     * ------------------------------------------------------------------ */
    RIA.addr1 = XRAM_RX_BASE;
    RIA.step1 = 1;
    start = clock();

    for (;;) {
        if (RX_READY()) {
            c = RRIA.RX;

            if (c == EOT) {
                action = 1;
                break;
            }

            if (xram_rx_len < XRAM_RX_MAX) {
                RIA.rw1 = c;   /* zapis do XRAM, addr1 auto-increment */
                xram_rx_len++;
            }
            /* jesli overflow: dalej czytamy UART zeby nie blokować FIFO,
               ale nie zapisujemy do XRAM */

            rx_count++;
            start = clock();
        } else {
            if ((clock() - start) >= timeout_ticks) {
                action = 1; /* timeout = koniec transmisji */
                break;
            }
        }
    }

save:
    /* ------------------------------------------------------------------ *
     * Zapis XRAM -> plik — dopiero tu, gdy UART juz milczy.              *
     * write_xram obsluguje max 32 KB naraz.                              *
     * ------------------------------------------------------------------ */
    if (action == 1 && xram_rx_len > 0) {
        u16 remaining = xram_rx_len;
        u16 addr = XRAM_RX_BASE;
        while (remaining > 0) {
            u16 chunk = (remaining > 0x7E00u) ? 0x7E00u : remaining;
            if (write_xram(addr, chunk, fd) < 0) {
                action = 0;
                break;
            }
            addr += chunk;
            remaining -= chunk;
        }
    }

done:
    close(fd);

    switch (action) {
    case -1:
        ria_tx_puts(CSI_RESET "Bye, bye!" NEWLINE NEWLINE);
        break;
    case 0:
        ria_tx_puts(NEWLINE "ERROR: transmission or write error." NEWLINE NEWLINE);
        break;
    case 1:
        drop_console_rx();
        ria_tx_puts(NEWLINE "SUCCESS: " );
        ria_tx_put_u16(rx_count);
        ria_tx_puts(" bytes received, saved to MSC0:/RX/rx.ihx" NEWLINE NEWLINE);
        break;
    }

    ria_tx_puts(CSI_CURSOR_SHOW);
    return 0;
}
