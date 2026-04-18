/*
 * task1_heartbeat.c — TASK1 heartbeat for razemOSmt
 *
 * Every 60 VSync frames (~1 second at 60 Hz) sends a '.' to UART.
 * Uses KERN_SLEEP_FRAMES ($020F) to yield CPU between beats.
 */

#include <rp6502.h>

#define RIA_READY_RX  0x40
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

static char task_getc(void)
{
    while (!(RIA.ready & RIA_READY_RX))
        ;
    return (char)RIA.rx;
}

/* Thin wrapper — sys_sleep_frames expects A=lo X=hi */
void __fastcall__ kern_sleep_frames(unsigned int n);

int main(void)
{
    int beat = 1;

    for (;;) {
        kern_sleep_frames(120);
        task_puts("\x1b[s\x1b[1;80H");
        if(beat == 1){
            task_putc('\x07');
            task_putc('\x03');
        } else {
            task_putc(' ');
        }
        task_puts("\x1b[u");
        beat = (beat == 0 ? 1 : 0);
    }
    return 0;
}
