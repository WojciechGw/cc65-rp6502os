/*
 * midi_out.h - Naglowki dla MIDI-OUT Picocomputer 6502
 * VIA @ $FFD0, Zegar 8 MHz
 * Wersja C89 dla cc65
 */

#ifndef MIDI_OUT_H
#define MIDI_OUT_H

/* Adresy rejestrow W65C22S VIA @ $FFD0 */
#define VIA_BASE    0xFFD0

#define VIA_PORTB   (*(volatile unsigned char*)(VIA_BASE + 0x00))
#define VIA_PORTA   (*(volatile unsigned char*)(VIA_BASE + 0x01))
#define VIA_DDRB    (*(volatile unsigned char*)(VIA_BASE + 0x02))
#define VIA_DDRA    (*(volatile unsigned char*)(VIA_BASE + 0x03))
#define VIA_T1CL    (*(volatile unsigned char*)(VIA_BASE + 0x04))
#define VIA_T1CH    (*(volatile unsigned char*)(VIA_BASE + 0x05))
#define VIA_T1LL    (*(volatile unsigned char*)(VIA_BASE + 0x06))
#define VIA_T1LH    (*(volatile unsigned char*)(VIA_BASE + 0x07))
#define VIA_T2CL    (*(volatile unsigned char*)(VIA_BASE + 0x08))
#define VIA_T2CH    (*(volatile unsigned char*)(VIA_BASE + 0x09))
#define VIA_SR      (*(volatile unsigned char*)(VIA_BASE + 0x0A))
#define VIA_ACR     (*(volatile unsigned char*)(VIA_BASE + 0x0B))
#define VIA_PCR     (*(volatile unsigned char*)(VIA_BASE + 0x0C))
#define VIA_IFR     (*(volatile unsigned char*)(VIA_BASE + 0x0D))
#define VIA_IER     (*(volatile unsigned char*)(VIA_BASE + 0x0E))
#define VIA_PORTA_NH (*(volatile unsigned char*)(VIA_BASE + 0x0F))

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
extern void midi_init(void);
extern void midi_start_transmission(void);
extern void midi_stop_transmission(void);

/* === Funkcje bufora === */
unsigned char midi_send_byte(unsigned char data);
unsigned char midi_send_buffer(const unsigned char *data, unsigned char len);
unsigned char midi_is_busy(void);
unsigned char midi_buffer_free(void);
void midi_flush(void);

/* === Funkcje pomocnicze MIDI === */
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

/* All Notes Off na kanale */
void midi_all_notes_off(unsigned char channel);

/* All Sound Off na kanale */
void midi_all_sound_off(unsigned char channel);

/* Reset wszystkich kontrolerow */
void midi_reset_controllers(unsigned char channel);

#endif /* MIDI_OUT_H */
