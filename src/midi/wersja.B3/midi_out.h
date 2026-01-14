/*
 * midi_out.h - Naglowki dla MIDI-OUT RP6502
 */

#ifndef MIDI_OUT_H
#define MIDI_OUT_H

#define MIDI_BUFFER_SIZE    64
#define MIDI_BUFFER_MASK    (MIDI_BUFFER_SIZE - 1)

#define TX_STATE_IDLE       0
#define TX_STATE_START      1
#define TX_STATE_DATA       2
#define TX_STATE_STOP       3

#define MIDI_OK             0
#define MIDI_BUFFER_FULL    1

/* Funkcje w asemblerze */
void midi_init(void);
void midi_start_transmission(void);
void midi_stop_transmission(void);
unsigned char midi_is_busy(void);
void midi_flush(void);

/* Funkcje w C */
unsigned char midi_send_byte(unsigned char data);
unsigned char midi_send_buffer(const unsigned char *data, unsigned char len);
unsigned char midi_buffer_free(void);

void midi_note_on(unsigned char channel, unsigned char note, unsigned char velocity);
void midi_note_off(unsigned char channel, unsigned char note, unsigned char velocity);
void midi_program_change(unsigned char channel, unsigned char program);
void midi_control_change(unsigned char channel, unsigned char controller, unsigned char value);
void midi_pitch_bend(unsigned char channel, unsigned int value);
void midi_all_notes_off(unsigned char channel);

#endif
