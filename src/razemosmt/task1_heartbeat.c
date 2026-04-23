/*
 * task1_heartbeat.c — TASK1 heartbeat for razemOSmt
 *
 * Blinks every 500ms regardless of irqfreq, using kern_sleep_ms.
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

void __fastcall__ kern_sleep_frames(unsigned int n);
void __fastcall__ kern_sleep_ms(unsigned int ms);

#define KDATA_IRQ_HZ_LO (*(volatile unsigned char *)0x0D10)
#define KDATA_IRQ_HZ_HI (*(volatile unsigned char *)0x0D11)

int main(void)
{
    int beat = 1;
    unsigned int hz, frames;

    for (;;) {
        hz = KDATA_IRQ_HZ_LO | ((unsigned int)KDATA_IRQ_HZ_HI << 8);
        frames = hz / 2;   /* 500ms = hz/2 frames */
        if (frames == 0) frames = 1;
        kern_sleep_frames(frames);
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
