/* 
Courier RX
OS Shell File Receiver
ext-crx.c - C89, cc65 (Picocomputer RP6502-RIA UART) 
*/

#include "commons.h"

#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)

#define NEWLINE "\r\n"

#define APPVER "20260314.1418"
#define APPDIRDEFAULT "MSC0:/SHELL/RX"
#define APP_MSG_TITLE "\x1b[2;1H\x1b" HIGHLIGHT_COLOR " OS Shell > " ANSI_RESET " Courier RX" ANSI_DARK_GRAY "\x1b[2;60Hversion " APPVER ANSI_RESET
#define APP_MSG_START ANSI_DARK_GRAY "\x1b[4;1HWaiting for incoming data or [Esc] to exit " ANSI_RESET

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

#define NUL 0x00
#define SOT 0x01 /* Start Of Transmission */
#define EOT 0x02 /* End Of Transmission   */
#define SOH 0x03 /* Start Of Header       */
#define EOH 0x04 /* End Of Header         */
#define STX 0x05 /* Start of TeXt (IHX)  */
#define ETX 0x06 /* End of TeXt           */

/* XRAM receive buffer */
#define XRAM_RX_BASE  0x0000u
#define XRAM_RX_MAX   0xEC00u  /* 60 KB */

/* XRAM staging area dla zdekodowanych bajtow */
#define XRAM_DECODE_STAGE      0xF000u
#define XRAM_DECODE_STAGE_SIZE 64u

/* max 31 znakow + null */
#define RX_FILENAME_MAX 32u
#define RX_OUTPATH_MAX  48u

unsigned char c;

static u16  rx_count = 0;
static char rx_filename[RX_FILENAME_MAX];
static char rx_outpath[RX_OUTPATH_MAX];

/* ======================================================================
 * Narzedzia UART TX
 * ====================================================================== */

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

/* Opróznia RX FIFO z echem — uzywac tylko gdy UART juz milczy. */
static void drop_console_rx(void)
{
    while (RX_READY()) {
        unsigned char ch = RRIA.RX;
        if (ch == '\n') ria_tx_putc_blocking('\r');
        ria_tx_putc_blocking(ch);
        rx_count++;
    }
}

/* ======================================================================
 * Protokol: budowanie sciezki wyjsciowej
 * ====================================================================== */

/*
 * Sklada rx_outpath = "MSC0:/RX/" + rx_filename.
 * Jesli rx_filename pusty -> fallback "rx.bin".
 */
static void build_rx_outpath(void)
{
    const char* prefix = "MSC0:/RX/";
    const char* fn;
    char* dst;
    uint8_t i;

    fn = (rx_filename[0] != '\0') ? rx_filename : "rx.bin";

    dst = rx_outpath;
    for (i = 0; prefix[i] != '\0'; i++) *dst++ = prefix[i];
    for (i = 0; fn[i] != '\0' && (u16)(dst - rx_outpath) < (RX_OUTPATH_MAX - 1u); i++)
        *dst++ = fn[i];
    *dst = '\0';
}

/* ======================================================================
 * Protokol: odbior naglowka SOH..EOH..STX
 * Zwraca 1 przy sukcesie, -1 jesli ESC.
 * Brak OS calls, brak druku — minimalne opoznienie przed petla odbiorczza.
 * ====================================================================== */
static int receive_header(void)
{
    uint8_t fn_len = 0;
    uint8_t state  = 0; /* 0=czekaj SOH, 1=czytaj filename, 2=czekaj STX */

    rx_filename[0] = '\0';

    for (;;) {
        if (!RX_READY()) continue;
        c = RRIA.RX;

        if (c == ESC) return -1;

        switch (state) {
        case 0: /* czekaj na SOH */
            if (c == SOH) state = 1;
            break;
        case 1: /* czytaj nazwe pliku az EOH */
            if (c == EOH) {
                rx_filename[fn_len] = '\0';
                state = 2;
            } else if (fn_len < (uint8_t)(RX_FILENAME_MAX - 1u)) {
                rx_filename[fn_len++] = (char)c;
            }
            break;
        case 2: /* czekaj na STX */
            if (c == STX) return 1;
            break;
        }
    }
}

/* ======================================================================
 * Dekoder IntelHEX
 * ====================================================================== */

static uint8_t hex_nibble(uint8_t ch)
{
    if (ch >= '0' && ch <= '9') return (uint8_t)(ch - '0');
    if (ch >= 'A' && ch <= 'F') return (uint8_t)(ch - 'A' + 10u);
    if (ch >= 'a' && ch <= 'f') return (uint8_t)(ch - 'a' + 10u);
    return 0u;
}

/*
 * Dekoduje IntelHEX z XRAM[XRAM_RX_BASE .. +ihx_len] i zapisuje surowe bajty
 * do pliku fd_out przez staging w XRAM.
 * Zwraca 1 przy sukcesie, 0 przy bledzie zapisu.
 */
static int decode_ihex_to_file(u16 ihx_len, int fd_out)
{
    uint8_t line[80];
    uint8_t out[64];
    uint8_t out_len;
    u16     pos;
    int     result;
    uint8_t llen;
    uint8_t b;
    uint8_t byte_count;
    uint8_t rec_type;
    uint8_t i;
    uint8_t j;
    uint8_t hi;
    uint8_t lo;
    uint8_t dp;

    out_len = 0;
    pos     = 0;
    result  = 1;

    RIA.addr1 = XRAM_RX_BASE;
    RIA.step1 = 1;

    while (pos < ihx_len) {

        llen = 0;
        while (pos < ihx_len && llen < (uint8_t)(sizeof(line) - 1u)) {
            b = RIA.rw1;
            pos++;
            if (b == '\n') break;
            if (b != '\r') line[llen++] = b;
        }
        line[llen] = 0;

        if (llen < 9u || line[0] != ':') continue;

        byte_count = (uint8_t)((hex_nibble(line[1]) << 4) | hex_nibble(line[2]));
        rec_type   = (uint8_t)((hex_nibble(line[7]) << 4) | hex_nibble(line[8]));

        if (rec_type == 0x01u) break;

        if (rec_type == 0x00u) {
            if (byte_count > 35u) continue;
            if (llen < (uint8_t)(9u + byte_count + byte_count)) continue;

            for (i = 0; i < byte_count; i++) {
                dp = (uint8_t)(9u + i + i);
                hi = line[dp];
                lo = line[(uint8_t)(dp + 1u)];
                out[out_len++] = (uint8_t)((hex_nibble(hi) << 4) | hex_nibble(lo));

                if (out_len >= XRAM_DECODE_STAGE_SIZE) {
                    RIA.addr0 = XRAM_DECODE_STAGE;
                    RIA.step0 = 1;
                    for (j = 0; j < out_len; j++) RIA.rw0 = out[j];
                    if (write_xram(XRAM_DECODE_STAGE, (unsigned)out_len, fd_out) < 0) {
                        result = 0;
                        goto decode_done;
                    }
                    out_len = 0;
                    RIA.addr1 = (u16)(XRAM_RX_BASE + pos);
                    RIA.step1 = 1;
                }
            }
        }
    }

    if (out_len > 0) {
        RIA.addr0 = XRAM_DECODE_STAGE;
        RIA.step0 = 1;
        for (j = 0; j < out_len; j++) RIA.rw0 = out[j];
        if (write_xram(XRAM_DECODE_STAGE, (unsigned)out_len, fd_out) < 0) result = 0;
    }

decode_done:
    return result;
}

/* ======================================================================
 * main
 * ====================================================================== */

int main(void)
{
    clock_t start;
    clock_t timeout_ticks = (clock_t)(RX_TIMEOUT_SECONDS * TICKS_PER_SEC);
    int action = 0;
    int fd;
    u16 xram_rx_len = 0;

    ria_tx_puts(CSI_RESET);
    ria_tx_puts(CSI_CURSOR_HIDE);
    ria_tx_puts(APP_MSG_TITLE);
    ria_tx_puts(APP_MSG_START);

    /* --- czekaj na SOT --- */
    for (;;) {
        if (RX_READY()) {
            c = RRIA.RX;
            if (c == SOT) break;
            if (c == ESC) { action = -1; goto done; }
        }
    }

    /* --- odbierz naglowek SOH + nazwa + EOH + STX --- */
    if (receive_header() < 0) {
        action = -1;
        goto done;
    }

    /* ------------------------------------------------------------------ *
     * Petla odbiorcza — zadnych OS calls, zadnego echa.                  *
     * Kazdy bajt trafia bezposrednio do XRAM. Koniec: ETX.               *
     * ------------------------------------------------------------------ */
    RIA.addr1 = XRAM_RX_BASE;
    RIA.step1 = 1;
    start = clock();

    for (;;) {
        if (RX_READY()) {
            c = RRIA.RX;

            if (c == ETX) {
                action = 1;
                break;
            }

            if (xram_rx_len < XRAM_RX_MAX) {
                RIA.rw1 = c;
                xram_rx_len++;
            }

            rx_count++;
            start = clock();
        } else {
            if ((clock() - start) >= timeout_ticks) {
                action = 1; /* timeout = koniec transmisji */
                break;
            }
        }
    }

    /* --- drenaż EOT (max ~100 ms) --- */
    {
        clock_t drain_start = clock();
        while ((clock() - drain_start) < 10) {
            if (RX_READY()) { c = RRIA.RX; if (c == EOT) break; }
        }
    }

    /* ------------------------------------------------------------------ *
     * Zapis XRAM -> rx.ihx                                               *
     * ------------------------------------------------------------------ */
    fd = open("MSC0:/RX/rx.ihx", O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) {
        ria_tx_puts("ERROR: cannot open MSC0:/RX/rx.ihx" NEWLINE);
        return -1;
    }
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
        ria_tx_puts(NEWLINE "SUCCESS: ");
        ria_tx_put_u16(rx_count);
        ria_tx_puts(" bytes received" NEWLINE);
        ria_tx_puts("Saved:   MSC0:/RX/rx.ihx" NEWLINE);

        /* Dekodowanie IntelHEX -> rx_outpath */
        build_rx_outpath();
        {
            int fd_out = open(rx_outpath, O_WRONLY | O_CREAT | O_TRUNC, 0666);
            if (fd_out < 0) {
                ria_tx_puts("WARNING: cannot create " NEWLINE NEWLINE);
            } else {
                if (decode_ihex_to_file(xram_rx_len, fd_out)) {
                    ria_tx_puts("Decoded: ");
                    ria_tx_puts(rx_outpath);
                    ria_tx_puts(NEWLINE NEWLINE);
                } else {
                    ria_tx_puts("WARNING: decode error, output incomplete" NEWLINE NEWLINE);
                }
                close(fd_out);
            }
        }
        break;
    }

    ria_tx_puts(CSI_CURSOR_SHOW);
    return 0;
}
