#include <rp6502.h>
#include <6502.h>
#include <stdbool.h>
#include <stdint.h>

#define NOTE_COUNT 8
const uint8_t notes[NOTE_COUNT] = {60, 62, 64, 65, 67, 69, 71, 72};

#define MIDI_CH 0 // kanał 1 → 0 w bajcie statusu
#define MIDI_NOTE_ON  (0x90 | MIDI_CH)
#define MIDI_NOTE_OFF (0x80 | MIDI_CH)
#define MIDI_PROG_CHG (0xC0 | MIDI_CH)
#define MIDI_PROG_NUM 7

#define NULL ((void*)0)

static uint8_t tx_byte = 0;
static uint8_t tx_state = 10;
static uint8_t note_index = 0;
static uint16_t timer = 0;

unsigned char midi_irq_handler(void)
{
    uint8_t save_step0;
    uint16_t save_addr0;

    // RP6502 state save
    save_addr0 = RIA.addr0;
    save_step0 = RIA.step0;

    // Acknowledge interrupt
    VIA.ifr = 0x40;

    switch (tx_state) {
        case 0:
            VIA.pra &= ~0x01; // Start bit = 0
            tx_state++;
            break;
        case 9:
            VIA.pra |= 0x01;  // Stop bit = 1
            tx_state++;
            break;
        case 10:
            VIA.pra |= 0x01;  // Idle
            break;
        default:
            if (tx_byte & 0x01)
                VIA.pra |= 0x01;
            else
                VIA.pra &= ~0x01;
            tx_byte >>= 1;
            tx_state++;
            break;
    }

    // Restore RP6502 state
    RIA.addr0 = save_addr0;
    RIA.step0 = save_step0;
    return IRQ_HANDLED;

}

void midi_send(uint8_t b)
{
    while (tx_state < 10);
    tx_byte = b;
    tx_state = 0;
}

void delay_ticks(uint16_t ticks)
{
    timer = 0;
    while (timer < ticks);
}

void midi_init(void)
{
    VIA.ddra = 0x01;
    VIA.pra = 0x01;

    VIA.t1l_lo = 0x00;
    VIA.t1l_hi = 0x01;
    VIA.t1_lo  = 0x00;
    VIA.t1_hi  = 0x01;

    VIA.acr = 0x40;
    VIA.ier = 0xC0;

    set_irq(midi_irq_handler, NULL, 0);

}

void main(void)
{
    midi_init();

    // Program Change 7 (Harpsichord, usually)
    midi_send(MIDI_PROG_CHG);
    midi_send(MIDI_PROG_NUM);
    delay_ticks(10000); // ~300 ms

    while (1)
    {
        uint8_t note = notes[note_index++ % NOTE_COUNT];

        // Note On
        midi_send(MIDI_NOTE_ON);
        midi_send(note);
        midi_send(0x7F);

        delay_ticks(15000); // ~500 ms

        // Note Off
        midi_send(MIDI_NOTE_OFF);
        midi_send(note);
        midi_send(0x00);

        delay_ticks(5000); // short gap
    }
}
