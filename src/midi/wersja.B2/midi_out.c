/*
 * midi_out.c - Sterownik MIDI-OUT dla Picocomputer 6502
 * VIA W65C22S @ $FFD0, Zegar 8 MHz
 *
 * Kompilator: cc65 (standard C89)
 */

#include "midi_out.h"

/* Zmienne zdefiniowane w asemblerze (zero page) */
extern volatile unsigned char midi_buffer[];
extern volatile unsigned char midi_head;
extern volatile unsigned char midi_tail;
extern volatile unsigned char midi_tx_state;

/*
 * Dodaj bajt do bufora MIDI
 * Zwraca: MIDI_OK lub MIDI_BUFFER_FULL
 */
unsigned char midi_send_byte(unsigned char data)
{
    unsigned char next_head;
    unsigned char current_state;

    next_head = (midi_head + 1) & MIDI_BUFFER_MASK;

    if (next_head == midi_tail) {
        return MIDI_BUFFER_FULL;
    }

    __asm__("php");
    __asm__("sei");

    midi_buffer[midi_head] = data;
    midi_head = next_head;
    current_state = midi_tx_state;

    __asm__("plp");

    if (current_state == TX_STATE_IDLE) {
        midi_start_transmission();
    }

    return MIDI_OK;
}

/*
 * Wyslij blok danych MIDI
 * Zwraca liczbe bajtow dodanych do bufora
 */
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

/*
 * Zwroc liczbe wolnych miejsc w buforze
 */
unsigned char midi_buffer_free(void)
{
    unsigned char head;
    unsigned char tail;
    unsigned char used;

    __asm__("php");
    __asm__("sei");

    head = midi_head;
    tail = midi_tail;

    __asm__("plp");

    if (head >= tail) {
        used = head - tail;
    } else {
        used = MIDI_BUFFER_SIZE - tail + head;
    }

    return MIDI_BUFFER_SIZE - 1 - used;
}

/* ============================================ */
/* Funkcje pomocnicze MIDI                      */
/* ============================================ */

void midi_note_on(unsigned char channel,
                  unsigned char note,
                  unsigned char velocity)
{
    midi_send_byte(0x90 | (channel & 0x0F));
    midi_send_byte(note & 0x7F);
    midi_send_byte(velocity & 0x7F);
}

void midi_note_off(unsigned char channel,
                   unsigned char note,
                   unsigned char velocity)
{
    midi_send_byte(0x80 | (channel & 0x0F));
    midi_send_byte(note & 0x7F);
    midi_send_byte(velocity & 0x7F);
}

void midi_program_change(unsigned char channel,
                         unsigned char program)
{
    midi_send_byte(0xC0 | (channel & 0x0F));
    midi_send_byte(program & 0x7F);
}

void midi_control_change(unsigned char channel,
                         unsigned char controller,
                         unsigned char value)
{
    midi_send_byte(0xB0 | (channel & 0x0F));
    midi_send_byte(controller & 0x7F);
    midi_send_byte(value & 0x7F);
}

void midi_pitch_bend(unsigned char channel,
                     unsigned int value)
{
    midi_send_byte(0xE0 | (channel & 0x0F));
    midi_send_byte(value & 0x7F);
    midi_send_byte((value >> 7) & 0x7F);
}

void midi_all_notes_off(unsigned char channel)
{
    midi_control_change(channel, 123, 0);
}

void midi_all_sound_off(unsigned char channel)
{
    midi_control_change(channel, 120, 0);
}

void midi_reset_controllers(unsigned char channel)
{
    midi_control_change(channel, 121, 0);
}

/* ============================================ */
/* Program glowny - przyklad                    */
/* ============================================ */

static void delay_ms(unsigned int ms)
{
    unsigned int i;
    unsigned int j;

    for (i = 0; i < ms; ++i) {
        for (j = 0; j < 200; ++j) {
            __asm__("nop");
            __asm__("nop");
            __asm__("nop");
            __asm__("nop");
        }
    }
}

static const unsigned char c_major_scale[] = {
    60, 62, 64, 65, 67, 69, 71, 72
};

int main(void)
{
    unsigned char i;

    midi_init();

    delay_ms(100);

    midi_program_change(0, 0);
    midi_control_change(0, 7, 100);

    while (1) {
        for (i = 0; i < 8; ++i) {
            midi_note_on(0, c_major_scale[i], 80);
            delay_ms(150);
            midi_note_off(0, c_major_scale[i], 0);
            delay_ms(20);
        }

        midi_flush();
        delay_ms(500);
    }

    return 0;
}