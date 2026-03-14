/* ext-rx.c - C89, cc65 (Picocomputer RP6502-RIA UART) */

#include "commons.h"

#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)

#define NEWLINE "\r\n"

#define APPVER "20260314.132146"
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

/* XRAM staging area dla zapisu zdekodowanych bajtow (64 B, za XRAM_RX_MAX) */
#define XRAM_DECODE_STAGE      0xF000u
#define XRAM_DECODE_STAGE_SIZE 64u

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

/* ======================================================================
 * Dekoder IntelHEX
 * ====================================================================== */

/* Konwertuje znak ASCII hex ('0'-'9','A'-'F','a'-'f') na wartosc 0-15. */
static uint8_t hex_nibble(uint8_t ch)
{
    if (ch >= '0' && ch <= '9') return (uint8_t)(ch - '0');
    if (ch >= 'A' && ch <= 'F') return (uint8_t)(ch - 'A' + 10u);
    if (ch >= 'a' && ch <= 'f') return (uint8_t)(ch - 'a' + 10u);
    return 0u;
}

/*
 * Dekoduje IntelHEX z XRAM[XRAM_RX_BASE .. +ihx_len] i zapisuje surowe bajty
 * do pliku fd_out. Dane czytane przez portal 1 (addr1/step1), zapis przez
 * portal 0 (addr0/step0) do obszaru staging. Adresy AAAA sa ignorowane
 * (dane sekwencyjne). Zwraca 1 przy sukcesie, 0 przy bledzie zapisu.
 */
static int decode_ihex_to_file(u16 ihx_len, int fd_out)
{
    uint8_t line[80];   /* bufor linii IntelHEX (RAM) */
    uint8_t out[64];    /* bufor zdekodowanych bajtow (RAM) */
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
    uint8_t dp; /* data pointer: indeks w line[] do pary hex */

    out_len = 0;
    pos     = 0;
    result  = 1;

    RIA.addr1 = XRAM_RX_BASE;
    RIA.step1 = 1;

    while (pos < ihx_len) {

        /* --- wczytaj jedna linie z XRAM do RAM --- */
        llen = 0;
        while (pos < ihx_len && llen < (uint8_t)(sizeof(line) - 1u)) {
            b = RIA.rw1;
            pos++;
            if (b == '\n') break;
            if (b != '\r') line[llen++] = b;
        }
        line[llen] = 0;

        /* minimum: ':' + LL(2) + AAAA(4) + TT(2) = 9 znakow */
        if (llen < 9u || line[0] != ':') continue;

        byte_count = (uint8_t)((hex_nibble(line[1]) << 4) | hex_nibble(line[2]));
        /* line[3..6] = AAAA (adres) — pomijamy */
        rec_type   = (uint8_t)((hex_nibble(line[7]) << 4) | hex_nibble(line[8]));

        if (rec_type == 0x01u) break; /* rekord EOF — koniec */

        if (rec_type == 0x00u) {
            /* rekord danych: linia musi zawierac 9 + byte_count*2 znakow */
            if (byte_count > 35u) continue;             /* za duzy dla bufora */
            if (llen < (uint8_t)(9u + byte_count + byte_count)) continue;

            for (i = 0; i < byte_count; i++) {
                dp = (uint8_t)(9u + i + i);             /* i*2, bez mnozenia */
                hi = line[dp];
                lo = line[(uint8_t)(dp + 1u)];
                out[out_len++] = (uint8_t)((hex_nibble(hi) << 4) | hex_nibble(lo));

                if (out_len >= XRAM_DECODE_STAGE_SIZE) {
                    /* flush: out[] -> XRAM staging -> plik */
                    RIA.addr0 = XRAM_DECODE_STAGE;
                    RIA.step0 = 1;
                    for (j = 0; j < out_len; j++) RIA.rw0 = out[j];
                    if (write_xram(XRAM_DECODE_STAGE, (unsigned)out_len, fd_out) < 0) {
                        result = 0;
                        goto decode_done;
                    }
                    out_len = 0;
                    /* write_xram (OS call) moze nadpisac portale — przywroc */
                    RIA.addr1 = (u16)(XRAM_RX_BASE + pos);
                    RIA.step1 = 1;
                }
            }
        }
        /* inne typy rekordow (02, 04...) sa ignorowane */
    }

    /* flush koncowy */
    if (out_len > 0) {
        RIA.addr0 = XRAM_DECODE_STAGE;
        RIA.step0 = 1;
        for (j = 0; j < out_len; j++) RIA.rw0 = out[j];
        if (write_xram(XRAM_DECODE_STAGE, (unsigned)out_len, fd_out) < 0) result = 0;
    }

decode_done:
    return result;
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
        ria_tx_puts(NEWLINE "SUCCESS: ");
        ria_tx_put_u16(rx_count);
        ria_tx_puts(" bytes received, saved to MSC0:/RX/rx.ihx" NEWLINE);
        /* Dekodowanie IntelHEX -> rx.txt (dane wciaz sa w XRAM) */
        {
            int fd_txt = open("MSC0:/RX/rx.txt", O_WRONLY | O_CREAT | O_TRUNC, 0666);
            if (fd_txt < 0) {
                ria_tx_puts("WARNING: cannot create rx.txt" NEWLINE NEWLINE);
            } else {
                if (decode_ihex_to_file(xram_rx_len, fd_txt)) {
                    ria_tx_puts("Decoded:  MSC0:/RX/rx.txt" NEWLINE NEWLINE);
                } else {
                    ria_tx_puts("WARNING: decode error, rx.txt incomplete" NEWLINE NEWLINE);
                }
                close(fd_txt);
            }
        }
        break;
    }

    ria_tx_puts(CSI_CURSOR_SHOW);
    return 0;
}
