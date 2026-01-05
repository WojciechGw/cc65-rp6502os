#include <rp6502.h>
#include <6502.h>
#include <stdint.h>
#include <stdio.h>

static uint8_t midi_stack[8];

static volatile uint8_t tx_byte = 0;
static volatile uint8_t tx_state = 10; // 10 = idle
static volatile uint16_t delay_counter = 0;

unsigned char midi_irq_fn(void)
{
    uint8_t save_step0;
    uint16_t save_addr0;

    // Reset interrupt flag
    VIA.ifr = 0x40;
    save_addr0 = RIA.addr0;
    save_step0 = RIA.step0;

    if (tx_state < 10)
    {
        if (tx_state == 0)
        {
            // Start bit
            VIA.pra &= ~0b00000001;
        }
        else if (tx_state >= 1 && tx_state <= 8)
        {
            if (tx_byte & 0x01)
                VIA.pra |= 0b00000001;
            else
                VIA.pra &= ~0b00000001;
            tx_byte >>= 1;
        }
        else if (tx_state == 9)
        {
            // Stop bit
            VIA.pra |= 0b00000001;
        }

        ++tx_state;
    }
    else
    {
        if (delay_counter > 0)
        {
            --delay_counter;
        }
        else
        {
            // Restart transmission: send 0x90 again
            tx_byte = 0xAA;
            tx_state = 0;
            delay_counter = 157; // ~5 ms delay @ 31250 Hz
        }
    }

    // Restore RP6502 state
    RIA.addr0 = save_addr0;
    RIA.step0 = save_step0;
    return IRQ_HANDLED;
}

void midi_init(void)
{
    VIA.ddra = 0b00000001;     // PA0 as output
    VIA.pra |= 0b00000001;     // idle state = HIGH

    VIA.t1l_lo = 0x00;
    VIA.t1l_hi = 0x01;
    VIA.t1_lo = 0x00;
    VIA.t1_hi = 0x01;

    VIA.acr = 0x40;            // Timer1 free-run
    VIA.ier = 0xC0;            // Enable T1 interrupt

    set_irq(midi_irq_fn, &midi_stack, sizeof(midi_stack));
}

void main()
{
    midi_init();
    while (1)
    {
        // nothing — MIDI TX handled entirely in IRQ
    }
}
