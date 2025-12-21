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

#define RX_READY (RIA.ready & RIA_READY_RX_BIT)
#define TX_READY (RIA.ready & RIA_READY_TX_BIT)
#define TX_READY_SPIN while(!TX_READY)
#define RX_READY_SPIN while(!RX_READY)

#define HEX_LINE_MAX 520 /* wystarcza na max LL=255 (2+4+2+510+2) */

static char    hex_line[HEX_LINE_MAX];
static int     hex_line_len = 0;
static uint8_t hex_bytes[256];

void print(char *s)
{
    while (*s)
        if (TX_READY)
            RIA.tx = *s++;
}

static void drop_console_rx(void) {
    int i;
    while (RX_READY) i = RIA.rx;
}

static int hex_nibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

static int hex_byte(const char *s) {
    int hi = hex_nibble(s[0]);
    int lo = hex_nibble(s[1]);
    if (hi < 0 || lo < 0) return -1;
    return (hi << 4) | lo;
}

/* Parsuje linię Intel HEX, zapisuje dane (typ 00) do pliku.
   Zwraca 1 jeśli EOF (typ 01), 0 w przeciwnym razie. */
static int process_hex_line(const char *buf, int len, int out_fd) {
    uint8_t sum = 0;
    int i, byte_count, type, data_len, idx;

    if (len < 11) return 0; /* za krótka */
    if (buf[0] != ':') return 0;

    byte_count = hex_byte(&buf[1]);
    if (byte_count < 0) return 0;
    if (len < (11 + (byte_count * 2))) return 0;

    sum += (uint8_t)byte_count;
    sum += (uint8_t)hex_byte(&buf[3]); /* addr hi */
    sum += (uint8_t)hex_byte(&buf[5]); /* addr lo */
    type = hex_byte(&buf[7]);
    if (type < 0) return 0;
    sum += (uint8_t)type;

    data_len = byte_count;
    if (data_len > (int)sizeof(hex_bytes)) data_len = (int)sizeof(hex_bytes);
    for (i = 0; i < byte_count; i++) {
        int b = hex_byte(&buf[9 + (i * 2)]);
        if (b < 0) return 0;
        sum += (uint8_t)b;
        if (i < (int)sizeof(hex_bytes)) hex_bytes[i] = (uint8_t)b;
    }

    idx = 9 + (byte_count * 2);
    {
        int cks = hex_byte(&buf[idx]);
        if (cks < 0) return 0;
        sum += (uint8_t)cks;
        if (sum != 0) return 0; /* zła suma kontrolna */
    }

    if (type == 0x00 && data_len > 0) {
        write(out_fd, hex_bytes, data_len);
    } else if (type == 0x01) {
        return 1; /* EOF */
    }
    return 0;
}

int main(int argc, char **argv)
{
    int out_fd;
    char rx_char;
    bool done = false;

    if (argc < 1 || argv[0][0] == 0) {
        printf("Usage: courier <outputfile>\r\n");
        return -1;
    }

    if(argc == 1 && strcmp(argv[0], "/?") == 0) {
        printf("Courier - receive file\r\n\r\nUsage: courier <outputfile>\r\n\r\n");
        return 0;
    }

    // printf("\x1b[c]");

    out_fd = open(argv[0], O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (out_fd < 0)
    {
        printf("Cannot open output file.\r\n");
        return -1;
    }

    printf("OS Shell > Courier\r\n\r\nReceiving binary data.\r\nAuto-stop after idle timeout.\r\n");

    while (true)
    {
        RX_READY_SPIN;
        rx_char = (char)RIA.rx;
        if (rx_char == '\r') continue;
        if (rx_char != '\n' && hex_line_len < (HEX_LINE_MAX - 1)) {
            hex_line[hex_line_len++] = rx_char;
        }
        if (rx_char == '\n' || hex_line_len >= (HEX_LINE_MAX - 1)) {
            if (hex_line_len > 0) {
                hex_line[hex_line_len] = '\0';
                done = process_hex_line(hex_line, hex_line_len, out_fd);
            }
            hex_line_len = 0;
            if (done) break;
        }
    }

    // drop_console_rx();
    close(out_fd);
    //printf("\x1b" "c" "\x1b[?25h");
    return 0;

}
