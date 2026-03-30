/*
Courier TX
OS Shell File Sender
ext-ctx.c - C89, cc65 (Picocomputer RP6502-RIA UART => PC)
*/
#include "commons.h"

#define NEWLINE "\r\n"

#define APPVER "20260330.1925"
#define APPDIRDEFAULT "MSC0:/"
#define APP_MSG_TITLE "\x1b[2;1H\x1b" HIGHLIGHT_COLOR " OS Shell > " ANSI_RESET " Courier TX" ANSI_DARK_GRAY "\x1b[2;60Hversion " APPVER ANSI_RESET
#define APP_MSG_START ANSI_DARK_GRAY "\x1b[4;1HSending file in Intel HEX format." ANSI_RESET

#define HDR_NAME_MAX 31   /* max filename chars in header (+ null = 32 B) */

#define RX_READY (RIA.ready & RIA_READY_RX_BIT)
#define TX_READY (RIA.ready & RIA_READY_TX_BIT)
#define TX_READY_SPIN while(!TX_READY)
#define RX_READY_SPIN while(!RX_READY)

#define HEX_LINE_MAX 64 /* :LLAAAATT + 2*16 data + CC + CRLF */

static char    hex_line[HEX_LINE_MAX];
static uint8_t hex_bytes[16];
static uint16_t hex_addr = 0;

static void send_char(char c) {
    TX_READY_SPIN;
    RIA.tx = c;
}

/* ======================================================================
 * related to UART
 * ====================================================================== */

static void ria_tx_putc_blocking(unsigned char b)
{
    TX_READY_SPIN;
    RIA.tx = b;
}

static void ria_tx_puts(const char* s)
{
    while (*s) {
        ria_tx_putc_blocking((unsigned char)*s++);
    }
}

static void put_hex(char *dst, uint8_t v) {
    static const char h[] = "0123456789ABCDEF";
    dst[0] = h[v >> 4];
    dst[1] = h[v & 0x0F];
}

/* Send one complete Intel HEX record via UART */
static void send_hex_record(uint8_t type, uint16_t addr,
                             const uint8_t *data, uint8_t len)
{
    uint8_t ck;
    int pos, j;
    ck  = len;
    ck += (uint8_t)(addr >> 8);
    ck += (uint8_t)(addr & 0xFF);
    ck += type;
    pos = 0;
    hex_line[pos++] = ':';
    put_hex(&hex_line[pos], len);                    pos += 2;
    put_hex(&hex_line[pos], (uint8_t)(addr >> 8));   pos += 2;
    put_hex(&hex_line[pos], (uint8_t)(addr & 0xFF)); pos += 2;
    put_hex(&hex_line[pos], type);                   pos += 2;
    for (j = 0; j < (int)len; j++) {
        put_hex(&hex_line[pos], data[j]);            pos += 2;
        ck += data[j];
    }
    ck = (uint8_t)(-((int)ck));
    put_hex(&hex_line[pos], ck);                     pos += 2;
    hex_line[pos++] = '\r';
    hex_line[pos++] = '\n';
    for (j = 0; j < pos; j++) send_char(hex_line[j]);
}

/*
 * Header stream: [basename\0][filesize 4B LE]  then EOF record.
 * Receiver reads this stream first to learn the output filename and size.
 */
static void send_header(const char *filepath, long filesize)
{
    uint8_t hdr[40];   /* max HDR_NAME_MAX + null + 4 bytes = 36 */
    uint8_t hlen, chunk, j;
    uint16_t addr;
    const char *name;

    /* strip path prefix — keep basename only */
    name = filepath;
    for (j = 0; filepath[j]; j++) {
        if (filepath[j] == '/' || filepath[j] == ':' || filepath[j] == '\\')
            name = &filepath[j + 1];
    }

    hlen = 0;
    for (j = 0; name[j] && j < HDR_NAME_MAX; j++)
        hdr[hlen++] = (uint8_t)name[j];
    hdr[hlen++] = 0;                                      /* null terminator  */
    hdr[hlen++] = (uint8_t)(filesize & 0xFF);             /* filesize LE      */
    hdr[hlen++] = (uint8_t)((filesize >>  8) & 0xFF);
    hdr[hlen++] = (uint8_t)((filesize >> 16) & 0xFF);
    hdr[hlen++] = (uint8_t)(((unsigned long)filesize >> 24) & 0xFF);

    addr = 0;
    j    = 0;
    while (j < hlen) {
        chunk = (hlen - j > 16) ? 16 : (hlen - j);
        send_hex_record(0x00, addr, &hdr[j], chunk);
        addr += chunk;
        j    += chunk;
    }
    send_hex_record(0x01, 0x0000, NULL, 0);  /* EOF — closes header stream */
}

int main(int argc, char **argv)
{
    int in_fd, n;
    uint8_t ck;
    int pos, i;
    long filesize;

    if (argc < 1 || argv[0][0] == 0) {
        printf("Usage: ctx <filename>" NEWLINE NEWLINE);
        return -1;
    }

    if(argc == 1 && strcmp(argv[0], "/?") == 0) {
        printf(NEWLINE
               "Courier TX - send file to PC"
               NEWLINE NEWLINE
               "Usage:" NEWLINE
               "first start crx.py script on target machine" NEWLINE
               "ctx <filename> - send a file <filename> via RIA UART" NEWLINE
               NEWLINE);
        return 0;
    }

    in_fd = open(argv[0], O_RDONLY);
    if (in_fd < 0)
    {
        printf("Cannot open output file.\r\n");
        return -1;
    }

    filesize = lseek(in_fd, 0L, SEEK_END);
    if (filesize < 0L) filesize = 0L;
    close(in_fd);
    
    {
        char sb[12];
        sprintf(sb, "%ld", filesize);
        ria_tx_puts(CSI_RESET);
        ria_tx_puts(APP_MSG_TITLE);
        ria_tx_puts("\x1b[4;1H" ANSI_DARK_GRAY "Run on PC script " ANSI_RESET "crx.py");
        ria_tx_puts("\x1b[6;1H" ANSI_DARK_GRAY "File: " ANSI_RESET);
        ria_tx_puts(argv[0]);
        ria_tx_puts("  " ANSI_DARK_GRAY "(size: ");
        ria_tx_puts(sb);
        ria_tx_puts(" B)" ANSI_RESET);
        ria_tx_puts("\x1b[8;1H" ANSI_GREEN "[Y]" ANSI_RESET " start transfer   "
                ANSI_RED "[Q]" ANSI_RESET " quit to shell" CSI_CURSOR_HIDE);
    
    }
    
    {
        unsigned char key;
        do {
            RX_READY_SPIN;
            key = RIA.rx;
            if (key == 'q' || key == 'Q') {
                close(in_fd);
                ria_tx_puts(NEWLINE NEWLINE CSI_CURSOR_SHOW);
                return 0;
            }
        } while (key != 'y' && key != 'Y');
    }
    ria_tx_puts("\x1b[?25l\x1b[8;1H" ANSI_DARK_GRAY "Sending...        " ANSI_RESET NEWLINE NEWLINE);

    in_fd = open(argv[0], O_RDONLY);   /* reopen — guaranteed position 0 */

    /* 1. header stream */
    send_header(argv[0], filesize);

    /* 2. file data stream — no console output inside this loop */
    while (true)
    {
        n = read(in_fd, hex_bytes, sizeof(hex_bytes));
        if (n <= 0) break;

        ck = (uint8_t)n;
        ck += (uint8_t)(hex_addr >> 8);
        ck += (uint8_t)(hex_addr & 0xFF);
        ck += 0x00; /* type */

        pos = 0;
        hex_line[pos++] = ':';
        put_hex(&hex_line[pos], (uint8_t)n); pos += 2;
        put_hex(&hex_line[pos], (uint8_t)(hex_addr >> 8)); pos += 2;
        put_hex(&hex_line[pos], (uint8_t)(hex_addr & 0xFF)); pos += 2;
        put_hex(&hex_line[pos], 0x00); pos += 2; /* type */

        for (i = 0; i < n; i++) {
            put_hex(&hex_line[pos], hex_bytes[i]); pos += 2;
            ck += hex_bytes[i];
        }

        ck = (uint8_t)(-((int)ck));
        put_hex(&hex_line[pos], ck); pos += 2;
        hex_line[pos++] = '\r';
        hex_line[pos++] = '\n';

        for (i = 0; i < pos; i++) send_char(hex_line[i]);

        hex_addr += (uint16_t)n;
    }

    send_hex_record(0x01, 0x0000, NULL, 0);  /* EOF — closes data stream */

    close(in_fd);
    {
        char sb[12];
        sprintf(sb, "%ld", filesize);
        ria_tx_puts(ANSI_CLS APP_MSG_TITLE);
        ria_tx_puts("\x1b[4;1H" ANSI_DARK_GRAY NEWLINE "Sent: " ANSI_RESET);
        ria_tx_puts(argv[0]);
        ria_tx_puts("  " ANSI_DARK_GRAY "(size: ");
        ria_tx_puts(sb);
        ria_tx_puts(" B)" ANSI_RESET "\r\n\x1b[6;1H\x1b[?25h" ANSI_GREEN "Transfer completed." ANSI_RESET NEWLINE NEWLINE CSI_CURSOR_SHOW);
    }
    return 0;
}
