/*
 * Copyright (c) 2025 Rumbledethumps
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * SPDX-License-Identifier: Unlicense
 */

#include <rp6502.h>
#include <stdbool.h>
#include <stdio.h>
#include <fcntl.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <time.h>

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

void print(char *s)
{
    while (*s) {
        send_char(*s++);
    }
}

static void put_hex(char *dst, uint8_t v) {
    static const char h[] = "0123456789ABCDEF";
    dst[0] = h[v >> 4];
    dst[1] = h[v & 0x0F];
}

int main(int argc, char **argv)
{
    int in_fd, n;
    uint8_t ck;
    int pos, i;

    if (argc < 1 || argv[0][0] == 0) {
        printf("Usage: courier <inputfile>\r\n");
        return -1;
    }

    if(argc == 1 && strcmp(argv[0], "/?") == 0) {
        printf("Courier - send file\r\n\r\nUsage: courier <inputfile>\r\n\r\n");
        return 0;
    }

    in_fd = open(argv[0], O_RDONLY);
    if (in_fd < 0)
    {
        printf("Cannot open input file.\r\n");
        return -1;
    }

    printf("OS Shell > Courier\r\n\r\nSending Intel HEX.\r\n");

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

    /* EOF record */
    pos = 0;
    ck = 0x00 + 0x00 + 0x00 + 0x01;
    hex_line[pos++] = ':';
    put_hex(&hex_line[pos], 0); pos += 2;
    put_hex(&hex_line[pos], 0); pos += 2;
    put_hex(&hex_line[pos], 0); pos += 2;
    put_hex(&hex_line[pos], 0x01); pos += 2;
    ck = (uint8_t)(-((int)ck));
    put_hex(&hex_line[pos], ck); pos += 2;
    hex_line[pos++] = '\r';
    hex_line[pos++] = '\n';
    for (i = 0; i < pos; i++) send_char(hex_line[i]);

    close(in_fd);
    printf("\x1b" "c" "\x1b[?25hTransmission done.");
    return 0;

}
