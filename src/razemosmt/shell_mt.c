/*
 * shell_mt.c — razemOSmt TASK0 shell (minimal, no stdlib)
 */

#include <rp6502.h>

#define RIA_READY_TX  0x80
#define RIA_READY_RX  0x40
#define CRLF          "\r\n"
#define PROMPT_STR    "razem> "
#define CMD_BUF_MAX   80
#define TOKEN_MAX     8

static void uart_putc(char c)
{
    while (!(RIA.ready & RIA_READY_TX))
        ;
    RIA.tx = c;
}

static void uart_puts(const char *s)
{
    while (*s)
        uart_putc(*s++);
}

static char uart_getc(void)
{
    while (!(RIA.ready & RIA_READY_RX))
        ;
    return (char)RIA.rx;
}

static int read_line(char *buf, int maxlen)
{
    int len = 0;
    char c;
    for (;;) {
        c = uart_getc();
        if (c == '\r' || c == '\n') {
            uart_puts(CRLF);
            buf[len] = '\0';
            return len;
        }
        if ((c == '\b' || c == 0x7f) && len > 0) {
            len--;
            uart_puts("\b \b");
            continue;
        }
        if (c >= 0x20 && len < maxlen - 1) {
            buf[len++] = c;
            uart_putc(c);
        }
    }
}

static int strequal(const char *a, const char *b)
{
    while (*a && *b && *a == *b) { a++; b++; }
    return *a == *b;
}

static void execute(char *buf)
{
    if (buf[0] == '\0') return;
    if (strequal(buf, "ver"))
        uart_puts("razemOSmt v0.1" CRLF);
    else if (strequal(buf, "cls"))
        uart_puts("\x1b[2J\x1b[H");
    else {
        uart_puts("? ");
        uart_puts(buf);
        uart_puts(CRLF);
    }
}

int main(void)
{
    char buf[CMD_BUF_MAX];

    uart_puts("\r\nrazemOSmt OK\r\n");

    for (;;) {
        uart_puts(PROMPT_STR);
        read_line(buf, CMD_BUF_MAX);
        execute(buf);
    }

    return 0;
}
