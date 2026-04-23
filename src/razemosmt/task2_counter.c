/*
 * task2_counter.c — TASK2 counter for razemOSmt
 *
 * Displays an incrementing counter at fixed screen position (col 70, row 1)
 * every second, demonstrating concurrent execution alongside TASK0 shell
 * and TASK1 heartbeat.
 */

#include <rp6502.h>

#define RIA_READY_TX  0x80

static void task_putc(char c)
{
    while (!(RIA.ready & RIA_READY_TX))
        ;
    RIA.tx = c;
}

static void task_puts(const char *s)
{
    while (*s)
        task_putc(*s++);
}

/* Print unsigned int without stdlib */
static void task_putuint(unsigned int n)
{
    char buf[6];
    unsigned char i = 0;
    if (n == 0) { task_putc('0'); return; }
    while (n) { buf[i++] = '0' + (n % 10); n /= 10; }
    while (i--) task_putc(buf[i]);
}

void __fastcall__ kern_sleep_ms(unsigned int ms);

int main(void)
{
    unsigned int count = 0;

    for (;;) {
        kern_sleep_ms(1000);
        task_puts("\x1b[s\x1b[1;70H[");
        task_putuint(count);
        task_puts("]\x1b[u");
        count++;
    }
}
