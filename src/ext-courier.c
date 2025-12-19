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

// wait on clock
uint32_t ticks = 0; // for PAUSE(millis)
#define PAUSE(millis) ticks=clock(); while(clock() < (ticks + millis)){}

#define RX_READY (RIA.ready & RIA_READY_RX_BIT)
#define TX_READY (RIA.ready & RIA_READY_TX_BIT)

#define B64_LINE_MAX 78 /* 76 chars + CRLF margin */

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

static int b64_val(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

/* Decode Base64 chunk; returns bytes written to out_fd */
static int b64_decode_and_write(const char* buf, int len, int out_fd) {
    uint8_t out[76];
    int o = 0;
    int i = 0;
    while (i + 3 < len) {
        int v0 = b64_val(buf[i]);
        int v1 = b64_val(buf[i+1]);
        int v2 = (buf[i+2] == '=') ? -2 : b64_val(buf[i+2]);
        int v3 = (buf[i+3] == '=') ? -2 : b64_val(buf[i+3]);
        if (v0 < 0 || v1 < 0 || v2 == -1 || v3 == -1) break;
        out[o++] = (uint8_t)((v0 << 2) | (v1 >> 4));
        if (buf[i+2] != '=') {
            out[o++] = (uint8_t)((v1 << 4) | (v2 >> 2));
        }
        if (buf[i+3] != '=') {
            out[o++] = (uint8_t)((v2 << 6) | v3);
        }
        i += 4;
    }
    if (o) write(out_fd, out, o);
    return o;
}

int main(int argc, char **argv)
{
    int fd,cp,out_fd;
    char rx_char;
    bool got_data = false;
    bool done = false;
    const uint32_t idle_timeout_ms = 2000; /* stop after 2s bez danych */
    char line[B64_LINE_MAX];
    int line_len = 0;

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

        if (RX_READY)
        {
            rx_char = RIA.rx;
            got_data = true;
            if (rx_char == '\r') continue;
            if (rx_char == '=')
            {
                if (line_len < (B64_LINE_MAX - 1)) line[line_len++] = rx_char;
                done = true;
            }
            else if (rx_char != '\n' && line_len < (B64_LINE_MAX - 1))
            {
                line[line_len++] = rx_char;
            }
            if (rx_char == '\n' || done || line_len >= (B64_LINE_MAX - 1))
            {
                if (line_len > 0) b64_decode_and_write(line, line_len, out_fd);
                line_len = 0;
                if (done) break;
            }
        }
    }

    // drop_console_rx();
    close(out_fd);
    printf("\x1b" "c" "\x1b[?25h");
    return 0;

}
