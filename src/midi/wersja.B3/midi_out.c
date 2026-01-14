/*
 * midi_out.c - Sterownik MIDI-OUT dla RP6502
 */

#include <rp6502.h>
#include "midi_out.h"

/* Zmienne zdefiniowane w asemblerze */
extern unsigned char midi_buffer[];
extern unsigned char midi_head;
extern unsigned char midi_tail;
extern unsigned char midi_tx_state;

/* Informacja dla kompilatora ze zmienne sa w zero page */
#pragma zpsym("midi_head")
#pragma zpsym("midi_tail")
#pragma zpsym("midi_tx_state")

unsigned char midi_send_byte(unsigned char data)
{
    unsigned char next_head;
    unsigned char state;

    next_head = (midi_head + 1) & MIDI_BUFFER_MASK;

    if (next_head == midi_tail) {
        return MIDI_BUFFER_FULL;
    }

    __asm__("sei");

    midi_buffer[midi_head] = data;
    midi_head = next_head;
    state = midi_tx_state;

    __asm__("cli");

    if (state == TX_STATE_IDLE) {
        midi_start_transmission();
    }

    return MIDI_OK;
}

unsigned char midi_send_buffer(const unsigned char *data, unsigned char len)
{
    unsigned char i;
    unsigned char sent;

    sent = 0;
    for (i = 0; i < len; ++i) {
        if (midi_send_byte(data[i]) != MIDI_OK) {
            break;
        }
        ++sent;
    }

    return sent;
}

unsigned char midi_buffer_free(void)
{
    unsigned char head;
    unsigned char tail;
    unsigned char used;

    __asm__("sei");
    head = midi_head;
    tail = midi_tail;
    __asm__("cli");

    if (head >= tail) {
        used = head - tail;
    } else {
        used = MIDI_BUFFER_SIZE - tail + head;
    }

    return MIDI_BUFFER_SIZE - 1 - used;
}

void midi_note_on(unsigned char channel, unsigned char note, unsigned char velocity)
{
    midi_send_byte(0x90 | (channel & 0x0F));
    midi_send_byte(note & 0x7F);
    midi_send_byte(velocity & 0x7F);
}

void midi_note_off(unsigned char channel, unsigned char note, unsigned char velocity)
{
    midi_send_byte(0x80 | (channel & 0x0F));
    midi_send_byte(note & 0x7F);
    midi_send_byte(velocity & 0x7F);
}

void midi_program_change(unsigned char channel, unsigned char program)
{
    midi_send_byte(0xC0 | (channel & 0x0F));
    midi_send_byte(program & 0x7F);
}

void midi_control_change(unsigned char channel, unsigned char controller, unsigned char value)
{
    midi_send_byte(0xB0 | (channel & 0x0F));
    midi_send_byte(controller & 0x7F);
    midi_send_byte(value & 0x7F);
}

void midi_pitch_bend(unsigned char channel, unsigned int value)
{
    midi_send_byte(0xE0 | (channel & 0x0F));
    midi_send_byte(value & 0x7F);
    midi_send_byte((value >> 7) & 0x7F);
}

void midi_all_notes_off(unsigned char channel)
{
    midi_control_change(channel, 123, 0);
}

static void delay_ms(unsigned int ms)
{
    unsigned int i, j;
    for (i = 0; i < ms; ++i) {
        for (j = 0; j < 800; ++j) {
            __asm__("nop");
        }
    }
}

static const unsigned char scale[] = { 60, 62, 64, 65, 67, 69, 71, 72 };

int main(void)
{
    unsigned char i;

    midi_init();
    delay_ms(100);

    midi_program_change(0, 0);
    midi_control_change(0, 7, 100);

    for (;;) {
        for (i = 0; i < 8; ++i) {
            midi_note_on(0, scale[i], 80);
            delay_ms(150);
            midi_note_off(0, scale[i], 0);
            delay_ms(30);
        }

        for (i = 8; i > 0; --i) {
            midi_note_on(0, scale[i-1], 80);
            delay_ms(150);
            midi_note_off(0, scale[i-1], 0);
            delay_ms(30);
        }

        midi_flush();
        delay_ms(500);
    }
}
