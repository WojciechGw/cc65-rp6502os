/*
Courier RX
OS Shell File Receiver
ext-crx.c - C89, cc65 (PC => Picocomputer RP6502-RIA UART)
*/

#include "commons.h"

#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)

#define NEWLINE "\r\n"

#define APPVER "20260330.1925"
#define APPDIRDEFAULT "MSC0:/"
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

/* XRAM staging area dla zdekodowanych bajtow */
#define XRAM_DECODE_STAGE      0xF000u
#define XRAM_DECODE_STAGE_SIZE 16u

/* max 31 znakow + null */
#define RX_FILENAME_MAX 32u
#define RX_OUTPATH_MAX  48u

/* ======================================================================
 * IHX state machine
 * ====================================================================== */
#define IHX_WAIT  0   /* czekaj na ':' */
#define IHX_BC    1   /* byte count: 2 znaki hex */
#define IHX_ADDR  2   /* adres: 4 znaki, ignoruj */
#define IHX_TYPE  3   /* typ rekordu: 2 znaki */
#define IHX_DATA  4   /* dane: byte_count*2 znaki */
#define IHX_SKIP  5   /* pominij do konca linii (\n) */

unsigned char c;

static unsigned long rx_count    = 0;
static unsigned long rx_decoded  = 0;
static unsigned long rx_filesize = 0;
static char rx_filename[RX_FILENAME_MAX];
static char rx_outpath[RX_OUTPATH_MAX];

static uint8_t ihx_state = IHX_WAIT;
static uint8_t ihx_pos   = 0;
static uint8_t ihx_bc    = 0;
static uint8_t ihx_type  = 0;
static uint8_t ihx_di    = 0;
static uint8_t ihx_hn    = 0;
static uint8_t ihx_buf[16];

/* ======================================================================
 * related to UART
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

static void ria_tx_put_ulong(unsigned long v)
{
    char buf[11];
    char i = 0;

    if (v == 0) {
        ria_tx_putc_blocking('0');
        return;
    }

    while (v != 0 && i < (char)sizeof(buf)) {
        buf[i++] = (char)('0' + (int)(v % 10u));
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
    }
}

/* ======================================================================
 * Protokol: budowanie sciezki wyjsciowej
 * ====================================================================== */

static void build_rx_outpath(void)
{
    const char* prefix = "\0";
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
 * ====================================================================== */
static int receive_header(void)
{
    uint8_t fn_len = 0;
    uint8_t sz_idx = 0;
    uint8_t state  = 0;

    rx_filename[0] = '\0';
    rx_filesize    = 0;

    for (;;) {
        if (!RX_READY()) continue;
        c = RRIA.RX;

        if (c == ESC) return -1;

        switch (state) {
        case 0:
            if (c == SOH) state = 1;
            break;
        case 1:
            if (c == EOH) {
                rx_filename[fn_len] = '\0';
                state = 2;
            } else if (fn_len < (uint8_t)(RX_FILENAME_MAX - 1u)) {
                rx_filename[fn_len++] = (char)c;
            }
            break;
        case 2: /* 4 bajty rozmiaru LE */
            rx_filesize |= ((unsigned long)c << (sz_idx * 8u));
            if (++sz_idx >= 4u) state = 3;
            break;
        case 3:
            if (c == STX) return 1;
            break;
        }
    }
}

/* ======================================================================
 * IHX dekoder on-the-fly
 * Zwraca: 0=w trakcie, 1=rekord danych gotowy (ihx_bc bajtow w ihx_buf),
 *         2=rekord EOF
 * ====================================================================== */
static uint8_t ihx_feed(uint8_t ch)
{
    switch (ihx_state) {
    case IHX_WAIT:
        if (ch == ':') { ihx_state = IHX_BC; ihx_pos = 0; }
        break;
    case IHX_BC:
        if (ihx_pos == 0) { ihx_hn = ch; ihx_pos = 1; }
        else {
            ihx_bc    = (uint8_t)(((uint8_t)((ihx_hn>='a'?ihx_hn-87:(ihx_hn>='A'?ihx_hn-55:ihx_hn-48))&0x0Fu) << 4)
                                | ((uint8_t)((ch  >='a'?ch  -87:(ch  >='A'?ch  -55:ch  -48))&0x0Fu)));
            ihx_state = IHX_ADDR; ihx_pos = 0;
        }
        break;
    case IHX_ADDR:
        if (++ihx_pos >= 4u) { ihx_state = IHX_TYPE; ihx_pos = 0; }
        break;
    case IHX_TYPE:
        if (ihx_pos == 0) { ihx_hn = ch; ihx_pos = 1; }
        else {
            ihx_type  = (uint8_t)(((uint8_t)((ihx_hn>='a'?ihx_hn-87:(ihx_hn>='A'?ihx_hn-55:ihx_hn-48))&0x0Fu) << 4)
                                | ((uint8_t)((ch  >='a'?ch  -87:(ch  >='A'?ch  -55:ch  -48))&0x0Fu)));
            ihx_di = 0; ihx_pos = 0;
            ihx_state = (ihx_bc > 0u) ? IHX_DATA : IHX_SKIP;
        }
        break;
    case IHX_DATA:
        if (ihx_pos == 0) { ihx_hn = ch; ihx_pos = 1; }
        else {
            if (ihx_di < (uint8_t)sizeof(ihx_buf))
                ihx_buf[ihx_di] = (uint8_t)(((uint8_t)((ihx_hn>='a'?ihx_hn-87:(ihx_hn>='A'?ihx_hn-55:ihx_hn-48))&0x0Fu) << 4)
                                           | ((uint8_t)((ch  >='a'?ch  -87:(ch  >='A'?ch  -55:ch  -48))&0x0Fu)));
            ihx_di++; ihx_pos = 0;
            if (ihx_di >= ihx_bc) {
                ihx_state = IHX_SKIP;
                if (ihx_type == 0x00u) return 1;
                if (ihx_type == 0x01u) return 2;
            }
        }
        break;
    case IHX_SKIP:
        if (ch == '\n') ihx_state = IHX_WAIT;
        break;
    }
    return 0;
}

/* ======================================================================
 * main
 * ====================================================================== */

int main(void)
{
    clock_t start;
    clock_t timeout_ticks = (clock_t)(RX_TIMEOUT_SECONDS * TICKS_PER_SEC);
    int action = 0;
    int fd_out;

    ria_tx_puts(CSI_RESET);
    ria_tx_puts(CSI_CURSOR_HIDE);
    ria_tx_puts(APP_MSG_TITLE);
    ria_tx_puts(APP_MSG_START);

    /* --- czekaj na SOT --- */
    for (;;) {
        if (RX_READY()) {
            c = RRIA.RX;
            if (c == SOT) break;
            if (c == ESC) { action = -1; goto done_pre; }
        }
    }

    /* --- odbierz naglowek SOH + nazwa + EOH + STX --- */
    if (receive_header() < 0) {
        action = -1;
        goto done_pre;
    }

    /* --- otwórz plik wyjsciowy PRZED petla odbiorcza --- */
    build_rx_outpath();
    fd_out = open(rx_outpath, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd_out < 0) {
        ria_tx_puts(NEWLINE "ERROR: cannot create " NEWLINE);
        ria_tx_puts(rx_outpath);
        ria_tx_puts(NEWLINE);
        action = 0;
        goto done_pre;
    }
    ria_tx_puts("\x1b[4;1H" ANSI_DARK_GRAY "Receiving: " ANSI_RESET);
    ria_tx_puts(rx_outpath);
    ria_tx_puts("  " ANSI_DARK_GRAY "(");
    ria_tx_put_ulong(rx_filesize);
    ria_tx_puts(" B)" ANSI_RESET "          ");
    ria_tx_putc_blocking('\x00'); /* READY — plik otwarty, gotowy na dane */

    /* ------------------------------------------------------------------ *
     * Petla odbiorcza — IHX dekodowanie on-the-fly.                      *
     * Po kazdym write_xram wysylamy ACK '+' do nadawcy (flow control).   *
     * ------------------------------------------------------------------ */
    ihx_state = IHX_WAIT;
    ihx_pos   = 0;
    rx_count   = 0;
    rx_decoded = 0;
    start = clock();

    for (;;) {
        if (RX_READY()) {
            uint8_t r;
            c = RRIA.RX;
            rx_count++;
            start = clock();

            if (c == ETX) { action = 1; break; }

            r = ihx_feed(c);
            if (r == 1) {
                uint8_t j;
                RIA.addr0 = XRAM_DECODE_STAGE;
                RIA.step0 = 1;
                for (j = 0; j < ihx_bc; j++) RIA.rw0 = ihx_buf[j];
                if (write_xram(XRAM_DECODE_STAGE, (unsigned)ihx_bc, fd_out) < 0) {
                    action = 0;
                    break;
                }
                rx_decoded += (unsigned long)ihx_bc;
                ria_tx_putc_blocking('\x00'); /* ACK — zezwol nadawcy na kolejna linie */
            } else if (r == 2) {
                ria_tx_putc_blocking('\x00'); /* ACK dla rekordu EOF */
                action = 1;
                break;
            }
        } else {
            if ((clock() - start) >= timeout_ticks) {
                action = 1;
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

    close(fd_out);

    switch (action) {
    case 0:
        ria_tx_puts(NEWLINE "ERROR: transmission or write error." NEWLINE NEWLINE);
        break;
    case 1:
        drop_console_rx();
        ria_tx_puts(NEWLINE "SUCCESS: ");
        ria_tx_put_ulong(rx_decoded);
        ria_tx_puts(" bytes received" NEWLINE);
        ria_tx_puts("Saved:   ");
        ria_tx_puts(rx_outpath);
        ria_tx_puts(NEWLINE NEWLINE);
        break;
    }

    ria_tx_puts(CSI_CURSOR_SHOW);
    return 0;

done_pre:
    switch (action) {
    case -1:
        ria_tx_puts(CSI_RESET "Bye, bye!" NEWLINE NEWLINE);
        break;
    case 0:
        ria_tx_puts(NEWLINE "ERROR: cannot open output file." NEWLINE NEWLINE);
        break;
    }
    ria_tx_puts(CSI_CURSOR_SHOW);
    return 0;
}
