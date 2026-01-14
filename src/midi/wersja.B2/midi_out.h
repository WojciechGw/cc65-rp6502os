/*
 * midi_out.h - Naglowki dla MIDI-OUT Picocomputer 6502
 * VIA @ $FFD0, Zegar 8 MHz
 * Wersja C89 dla cc65
 */

#ifndef MIDI_OUT_H
#define MIDI_OUT_H

/* Rozmiar bufora MIDI (musi byc potega 2) */
#define MIDI_BUFFER_SIZE        64
#define MIDI_BUFFER_MASK        (MIDI_BUFFER_SIZE - 1)

/* Stany maszyny stanow transmisji */
#define TX_STATE_IDLE       0
#define TX_STATE_START      1
#define TX_STATE_DATA       2
#define TX_STATE_STOP       3

/* Kody statusow */
#define MIDI_OK             0
#define MIDI_BUFFER_FULL    1

/* === Funkcje zdefiniowane w asemblerze === */
extern void  midi_init(void);
extern void  midi_start_transmission(void);
extern void  midi_stop_transmission(void);
extern unsigned char midi_is_busy(void);
extern void midi_flush(void);

/* === Funkcje bufora (w C) === */
unsigned char midi_send_byte(unsigned char data);
unsigned char midi_send_buffer(const unsigned char *data, unsigned char len);
unsigned char midi_buffer_free(void);

/* === Funkcje pomocnicze MIDI (w C) === */
void midi_note_on(unsigned char channel,
                  unsigned char note,
                  unsigned char velocity);

void midi_note_off(unsigned char channel,
                   unsigned char note,
                   unsigned char velocity);

void midi_program_change(unsigned char channel,
                         unsigned char program);

void midi_control_change(unsigned char channel,
                         unsigned char controller,
                         unsigned char value);

void midi_pitch_bend(unsigned char channel,
                     unsigned int value);

void midi_all_notes_off(unsigned char channel);

void midi_all_sound_off(unsigned char channel);

void midi_reset_controllers(unsigned char channel);

#endif /* MIDI_OUT_H */