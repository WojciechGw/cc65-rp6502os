/*
 * Courier TX
 * OS Shell File Sender
 * ext-ctx.c - C89, cc65 (Picocomputer RP6502-RIA UART => PC)
 */
#include "commons.h"
#include "commons/courier-gfx.h"

#define NEWLINE "\r\n"

#define APPVER "20260330.2200"

#define HDR_NAME_MAX 31   /* max filename chars in header (+ null = 32 B) */

#define RX_READY (RIA.ready & RIA_READY_RX_BIT)
#define TX_READY (RIA.ready & RIA_READY_TX_BIT)
#define TX_READY_SPIN  while (!TX_READY)
#define RX_READY_SPIN  while (!RX_READY)

#define HEX_LINE_MAX 64  /* :LLAAAATT + 2*16 data + CC + CRLF */

static char     hex_line[HEX_LINE_MAX];
static uint8_t  hex_bytes[16];
static uint16_t hex_addr = 0;

/* ======================================================================
 * UART helpers (protocol data only — UI goes to XRAM in graphics mode)
 * ====================================================================== */

static void send_char(char c)
{
    TX_READY_SPIN;
    RIA.tx = c;
}

static void put_hex(char *dst, uint8_t v)
{
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
 * Header stream: [basename\0][filesize 4B LE] then EOF record.
 * Receiver reads this first to learn the output filename and size.
 */
static void send_header(const char *filepath, long filesize)
{
    uint8_t  hdr[40];   /* max HDR_NAME_MAX + null + 4 bytes = 36 */
    uint8_t  hlen, chunk, j;
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
    hdr[hlen++] = 0;                                       /* null terminator */
    hdr[hlen++] = (uint8_t)(filesize & 0xFF);              /* filesize LE     */
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

/* ======================================================================
 * Screen helpers
 * ====================================================================== */

static void draw_title(void)
{
    ClearLine(0, WHITE, BLACK);
    DrawText(1,  0, " OS Shell > ",    WHITE,     DARK_GREEN);
    DrawText(1, 12, " Courier TX",      WHITE,     BLACK);
    DrawText(1, 59, "version " APPVER, DARK_GRAY, BLACK     );
}

/* ======================================================================
 * main
 * ====================================================================== */

int main(int argc, char **argv)
{
    int     in_fd, n;
    uint8_t ck;
    int     pos, i;
    long    filesize;
    long    done_bytes = 0L;
    int     prev_pct   = -1;

    if (argc < 1 || argv[0][0] == 0) {
        printf("Usage: ctx <filename>" NEWLINE NEWLINE);
        return -1;
    }

    if (argc == 1 && strcmp(argv[0], "/?") == 0) {
        printf(NEWLINE
               "Courier TX - send file to PC" NEWLINE NEWLINE
               "Usage:" NEWLINE
               "  first start crx.py on the target machine" NEWLINE
               "  ctx <filename>  - send file via RIA UART" NEWLINE
               NEWLINE);
        return 0;
    }

    in_fd = open(argv[0], O_RDONLY);
    if (in_fd < 0) {
        printf("Cannot open file.\r\n");
        return -1;
    }

    filesize = lseek(in_fd, 0L, SEEK_END);
    if (filesize < 0L) filesize = 0L;
    close(in_fd);

    /* --- switch to Character Mode 1 (8x16) --- */
    cgx_init();
    draw_title();

    {
        char     sb[12];
        uint8_t  col;
        sprintf(sb, "%ld", filesize);
        DrawText(3,  0, "Run the receiver script on the PC.", DARK_GRAY, BLACK);
        DrawText(3, 34, " (python crx.py)",   DARK_GRAY,     BLACK);
        DrawText(5,  0, "File to transfer:",            DARK_GRAY, BLACK);
        DrawText(5,  18, argv[0],             WHITE,     BLACK);
        col = (uint8_t)(18 + strlen(argv[0]));
        DrawText(5, col, "  (size: ", DARK_GRAY, BLACK);
        col += 9;
        DrawText(5, col, sb,  WHITE,     BLACK);
        col += (uint8_t)strlen(sb);
        DrawText(5, col, " B)", DARK_GRAY, BLACK);
    }

    DrawText(9,  0, "[Y]",               GREEN,      BLACK);
    DrawText(9,  3, " start transfer   ", LIGHT_GRAY, BLACK);
    DrawText(9, 21, "[Q]",               RED,        BLACK);
    DrawText(9, 24, " quit to shell",    LIGHT_GRAY, BLACK);

    /* --- Y / Q prompt --- */
    {
        unsigned char key;
        do {
            RX_READY_SPIN;
            key = RIA.rx;
            if (key == 'q' || key == 'Q') {
                cgx_restore();
                return 0;
            }
        } while (key != 'y' && key != 'Y');
    }

    ClearLine(9, WHITE, BLACK);
    DrawBar(7, 0L, filesize);
    DrawText(9, 0, "Sending...", DARK_GRAY, BLACK);

    in_fd = open(argv[0], O_RDONLY);  /* reopen — guaranteed position 0 */

    /* 1. header stream */
    send_header(argv[0], filesize);

    /* 2. file data stream */
    while (true) {
        n = read(in_fd, hex_bytes, sizeof(hex_bytes));
        if (n <= 0) break;

        ck  = (uint8_t)n;
        ck += (uint8_t)(hex_addr >> 8);
        ck += (uint8_t)(hex_addr & 0xFF);
        ck += 0x00;  /* type */

        pos = 0;
        hex_line[pos++] = ':';
        put_hex(&hex_line[pos], (uint8_t)n);                 pos += 2;
        put_hex(&hex_line[pos], (uint8_t)(hex_addr >> 8));   pos += 2;
        put_hex(&hex_line[pos], (uint8_t)(hex_addr & 0xFF)); pos += 2;
        put_hex(&hex_line[pos], 0x00);                       pos += 2;  /* type */

        for (i = 0; i < n; i++) {
            put_hex(&hex_line[pos], hex_bytes[i]); pos += 2;
            ck += hex_bytes[i];
        }

        ck = (uint8_t)(-((int)ck));
        put_hex(&hex_line[pos], ck); pos += 2;
        hex_line[pos++] = '\r';
        hex_line[pos++] = '\n';

        for (i = 0; i < pos; i++) send_char(hex_line[i]);

        hex_addr   += (uint16_t)n;
        done_bytes += (long)n;

        /* update bar only when percentage changes */
        {
            int cur_pct = (filesize > 0L)
                          ? (int)(done_bytes * 100L / filesize) : 0;
            if (cur_pct != prev_pct) {
                prev_pct = cur_pct;
                DrawBar(7, done_bytes, filesize);
            }
        }
    }

    send_hex_record(0x01, 0x0000, NULL, 0);  /* EOF — closes data stream */

    close(in_fd);

    /* --- completion screen --- */
    {
        uint8_t r, col;
        char    sb[12];
        draw_title();
        sprintf(sb, "%ld", filesize);
        for (r = 1; r < (uint8_t)CGX_ROWS; r++) ClearLine(r, WHITE, BLACK);
        DrawText(3, 0, "Sent: ",   DARK_GRAY, BLACK);
        DrawText(3, 6, argv[0],    WHITE,     BLACK);
        col = (uint8_t)(6 + strlen(argv[0]));
        DrawText(3, col, "  (size: ", DARK_GRAY, BLACK);
        col += 9;
        DrawText(3, col, sb,  WHITE,     BLACK);
        col += (uint8_t)strlen(sb);
        DrawText(3, col, " B)", DARK_GRAY, BLACK);
        DrawText(5, 0, "Transfer completed.", GREEN, BLACK);
        DrawText((uint8_t)(CGX_ROWS - 2), 0, "Press any key...", DARK_GRAY, BLACK);
    }

    RX_READY_SPIN;
    (void)RIA.rx;

    cgx_restore();
    return 0;
}
