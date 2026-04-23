/*
 * Courier RX
 * razemOS File Receiver
 * ext-crx.c - C89, cc65 (PC => Picocomputer RP6502-RIA UART)
 */

#include "commons.h"

#define _NEED_DRAWBAR
#include "commons/courier-gfx.h"

#define APPVER "20260423.1026"

/* ---- RIA UART access ---------------------------------------------------- */

typedef struct {
    volatile unsigned char READY; /* $FFE0 */
    volatile unsigned char TX;    /* $FFE1 */
    volatile unsigned char RX;    /* $FFE2 */
} ria_uart_t;

typedef unsigned int u16;

#define RIA_BASE_ADDR ((u16)KEYBOARD_INPUT)
#define RIA_PTR       ((ria_uart_t*)(RIA_BASE_ADDR))
#define RRIA          (*RIA_PTR)

#define RRIA_READY_TX_BIT 0x80u  /* bit 7 */
#define RRIA_READY_RX_BIT 0x40u  /* bit 6 */

#define RRX_READY() (RRIA.READY & RRIA_READY_RX_BIT)
#define RTX_READY() (RRIA.READY & RRIA_READY_TX_BIT)

/* ---- protocol constants ------------------------------------------------- */

#define RX_TIMEOUT_SECONDS 10
#define TICKS_PER_SEC      100

#define ESC 0x1B
#define NUL 0x00
#define SOT 0x01  /* Start Of Transmission */
#define EOT 0x02  /* End Of Transmission   */
#define SOH 0x03  /* Start Of Header       */
#define EOH 0x04  /* End Of Header         */
#define STX 0x05  /* Start of Text (IHX)   */
#define ETX 0x06  /* End of Text           */

/* ---- XRAM staging area -------------------------------------------------- */

#define XRAM_DECODE_STAGE      0xF000u
#define XRAM_DECODE_STAGE_SIZE 16u

/* ---- file name / path --------------------------------------------------- */

#define RX_FILENAME_MAX 32u
#define RX_OUTPATH_MAX  48u

/* ---- IHX state machine -------------------------------------------------- */

#define IHX_WAIT  0  /* wait for ':' */
#define IHX_BC    1  /* byte count: 2 hex chars */
#define IHX_ADDR  2  /* address: 4 chars, ignored */
#define IHX_TYPE  3  /* record type: 2 chars */
#define IHX_DATA  4  /* data bytes: byte_count*2 chars */
#define IHX_SKIP  5  /* skip to end of line (\n) */

#define IHX_RTYPE_DATA 0x00u  /* data record */
#define IHX_RTYPE_EOF  0x01u  /* end-of-file record */
#define IHX_RTYPE_ELA  0x04u  /* extended linear address — skipped by IHX_SKIP */

unsigned char c;

/* ---- CRC32 bit-at-a-time (polynomial 0xEDB88320, no table) -------------- */

static unsigned long crc32_update(unsigned long crc, unsigned char b)
{
    uint8_t i;
    crc ^= (unsigned long)b;
    for (i = 0; i < 8u; i++) {
        if (crc & 1UL)
            crc = (crc >> 1) ^ 0xEDB88320UL;
        else
            crc >>= 1;
    }
    return crc;
}

static unsigned long rx_count         = 0;
static unsigned long rx_decoded       = 0;
static unsigned long rx_filesize      = 0;
static unsigned long rx_checksum_exp  = 0;  /* CRC32 from header     */
static unsigned long rx_checksum_calc = 0;  /* CRC32 of received data */
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
 * Protocol: send one byte via UART (ACK / READY signals to PC)
 * ====================================================================== */

static void ria_tx_byte(unsigned char b)
{
    while (RTX_READY() == 0) { }
    RRIA.TX = b;
}

/* ======================================================================
 * Protocol: build output file path from received filename
 * ====================================================================== */

static void build_rx_outpath(void)
{
    const char *fn;
    char       *dst;
    uint8_t     i;

    fn  = (rx_filename[0] != '\0') ? rx_filename : "rx.bin";
    dst = rx_outpath;
    for (i = 0; fn[i] != '\0' && (u16)(dst - rx_outpath) < (RX_OUTPATH_MAX - 1u); i++)
        *dst++ = fn[i];
    *dst = '\0';
}

/* ======================================================================
 * Protocol: receive header SOH + filename + EOH + 4B size + STX
 * Returns 1 on success, -1 if ESC received.
 * ====================================================================== */

static int receive_header(void)
{
    uint8_t fn_len = 0;
    uint8_t sz_idx = 0;
    uint8_t state  = 0;

    rx_filename[0]    = '\0';
    rx_filesize       = 0;
    rx_checksum_exp   = 0;

    for (;;) {
        if (!RRX_READY()) continue;
        c = RRIA.RX;

        switch (state) {
        case 0:
            if (c == SOH) state = 1;
            else if (c == ESC) return -1;  /* cancel only before header starts */
            break;
        case 1:
            if (c == EOH) {
                rx_filename[fn_len] = '\0';
                state = 2;
            } else if (fn_len < (uint8_t)(RX_FILENAME_MAX - 1u)) {
                rx_filename[fn_len++] = (char)c;
            }
            break;
        case 2:  /* 4 LE size bytes */
            rx_filesize |= ((unsigned long)c << (sz_idx * 8u));
            if (++sz_idx >= 4u) { sz_idx = 0; state = 3; }
            break;
        case 3:  /* 4 LE checksum bytes */
            rx_checksum_exp |= ((unsigned long)c << (sz_idx * 8u));
            if (++sz_idx >= 4u) state = 4;
            break;
        case 4:
            if (c == STX) return 1;
            break;
        }
    }
}

/* ======================================================================
 * IHX on-the-fly decoder
 * Returns: 0=in progress, 1=data record ready (ihx_bc bytes in ihx_buf),
 *          2=EOF record
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
                if (ihx_type == IHX_RTYPE_DATA) return 1;
                if (ihx_type == IHX_RTYPE_EOF)  return 2;
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
 * Screen helpers
 * ====================================================================== */

static void draw_title(void)
{
    ClearLine(0, WHITE, BLACK);
    DrawText(1,  0, " razemOS > ",    WHITE,     DARK_GREEN);
    DrawText(1, 13, "Courier RX",      WHITE,     BLACK);
    DrawText(1, 59, "version " APPVER, DARK_GRAY, BLACK);
}

/* ======================================================================
 * main
 * ====================================================================== */

int main(int argc, char **argv)
{
    clock_t start;
    clock_t timeout_ticks = (clock_t)(RX_TIMEOUT_SECONDS * TICKS_PER_SEC);
    int     action = 0;
    int     fd_out;
    int     auto_mode = (argc >= 1 && strcmp(argv[0], "/auto") == 0);

    /* --- switch to Character Mode 1 (8x16) --- */
    cgx_init();
    draw_title();

    if (!auto_mode) {
        /* --- interactive: wait for SOT or Esc --- */
        DrawText(3, 13, "Waiting for incoming data or [Esc] to exit", DARK_GRAY, BLACK);
        for (;;) {
            if (RRX_READY()) {
                c = RRIA.RX;
                if (c == SOT) break;
                if (c == ESC) { action = -1; goto done_pre; }
            }
        }
    } else {
        /* --- auto mode: SOT was consumed by shell, proceed directly --- */
        DrawText(3, 13, "Receiving...", DARK_GRAY, BLACK);
    }

    /* Signal PC: crx is running and ready to receive header */
    ria_tx_byte('\x00');

    /* --- receive header: SOH + name + EOH + 4B size + STX --- */
    if (receive_header() < 0) {
        action = -1;
        goto done_pre;
    }

    /* --- open output file BEFORE the receive loop --- */
    build_rx_outpath();
    fd_out = open(rx_outpath, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd_out < 0) {
        ClearLine(3, WHITE, BLACK);
        ClearLine(4, WHITE, BLACK);
        DrawText(3, 13, EXCLAMATION "Cannot create output file", RED, BLACK);
        DrawText(4, 13, rx_outpath, WHITE, BLACK);
        action = 0;
        goto done_pre;
    }

    {
        char     sb[12];
        char     sb_crc[12];
        uint8_t  col;
        sprintf(sb, "%lu", rx_filesize);
        sprintf(sb_crc, "%08lX", rx_checksum_exp);
        ClearLine(3, WHITE, BLACK);
        DrawText(3, 13, "Receiving: ",  DARK_GRAY, BLACK);
        DrawText(3, 24, rx_outpath,     WHITE,     BLACK);
        col = (uint8_t)(24 + strlen(rx_outpath));
        DrawText(3, col, "  (", DARK_GRAY, BLACK); col += 3;
        DrawText(3, col, sb,    WHITE,     BLACK);  col += (uint8_t)strlen(sb);
        DrawText(3, col, " B, CRC32: ", DARK_GRAY, BLACK); col += 11;
        DrawText(3, col, sb_crc, WHITE,  BLACK);    col += 8;
        DrawText(3, col, ")", DARK_GRAY, BLACK);
    }

    DrawBar(5, 12L, (long)rx_filesize);

    ria_tx_byte('\x00');  /* READY — file open, ready for data */

    /* ------------------------------------------------------------------
     * Receive loop — IHX on-the-fly decode.
     * Send ACK (NUL) after each write_xram to control sender flow.
     * ------------------------------------------------------------------ */
    ihx_state        = IHX_WAIT;
    ihx_pos          = 0;
    rx_count         = 0;
    rx_decoded       = 0;
    rx_checksum_calc = 0xFFFFFFFFUL;  /* CRC32 initial value */
    start            = clock();

    {
        int prev_pct = -1;
        for (;;) {
            if (RRX_READY()) {
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
                    for (j = 0; j < ihx_bc; j++) {
                        RIA.rw0 = ihx_buf[j];
                        rx_checksum_calc = crc32_update(rx_checksum_calc, ihx_buf[j]);
                    }
                    if (write_xram(XRAM_DECODE_STAGE, (unsigned)ihx_bc, fd_out) < 0) {
                        action = 0;
                        break;
                    }
                    rx_decoded += (unsigned long)ihx_bc;
                    {
                        int cur_pct = (rx_filesize > 0UL)
                                    ? (int)(rx_decoded * 100UL / rx_filesize) : 0;
                        if (cur_pct != prev_pct) {
                            prev_pct = cur_pct;
                            DrawBar(5, (long)rx_decoded, (long)rx_filesize);
                        }
                    }
                    ria_tx_byte('\x00');  /* ACK — allow sender to send next line */
                
                } else if (r == 2) {
                    ria_tx_byte('\x00');  /* ACK for EOF record */
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
    }
    /* --- drain EOT (max ~100 ms) --- */
    {
        clock_t drain_start = clock();
        while ((clock() - drain_start) < 10) {
            if (RRX_READY()) { c = RRIA.RX; if (c == EOT) break; }
        }
    }

    close(fd_out);
    if (!auto_mode) {
        /* --- result screen --- */
        switch (action) {
        case 0:
            ClearLine(5, WHITE, BLACK);
            DrawText(5, 13, EXCLAMATION "Transmission or write error", RED, BLACK);
            break;
        case 1:
            {
                char sb[12];
                uint32_t crc32_final = rx_checksum_calc ^ 0xFFFFFFFFUL;
                uint8_t ck_ok = (crc32_final == rx_checksum_exp);
                ClearLine(5, WHITE, BLACK);
                DrawText(5, 13, "Saved: ", DARK_GRAY, BLACK);
                DrawText(5, 20, rx_outpath, WHITE, BLACK);
                sprintf(sb, "%lu", rx_decoded);
                DrawText(6, 13, "Transfer complete.", GREEN, BLACK);
                DrawText(6, 32, sb, WHITE, BLACK);
                DrawText(6, (uint8_t)(32 + strlen(sb)), " bytes received", GREEN, BLACK);
                {
                    char sb2[24];
                    sprintf(sb2, " Checksum [%08lX] ", crc32_final);
                    if (ck_ok) {
                        DrawText(7, 13, sb2, WHITE, DARK_GREEN);
                    } else {
                        DrawText(7, 13, sb2, WHITE, DARK_RED);
                    }
                }
                break;
            }
        }
    }
    
    cgx_restore();
    
    return 0;

done_pre:

    ClearLine(3,WHITE,BLACK);
    ClearLine(4,WHITE,BLACK);
    ClearLine(5,WHITE,BLACK);
    switch (action) {
    case -1:
        DrawText(3, 13, "Cancelled",  YELLOW, BLACK);
        break;
    case 0:
        DrawText(3, 13, EXCLAMATION "Cannot open output file", RED, BLACK);
        break;
    }
    
    cgx_restore();
    
    return 0;
}
