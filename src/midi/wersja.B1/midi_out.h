/*
 * midi_out.h - Naglowki dla MIDI-OUT Picocomputer 6502
 * Wersja C89 dla cc65
 */

#ifndef MIDI_OUT_H
#define MIDI_OUT_H

/* Adresy rejestrow W65C22S VIA */
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

/* Bity rejestru IFR/IER */
#define VIA_IRQ_T1      0x40
#define VIA_IRQ_T2      0x20
#define VIA_IRQ_CB1     0x10
#define VIA_IRQ_CB2     0x08
#define VIA_IRQ_SR      0x04
#define VIA_IRQ_CA1     0x02
#define VIA_IRQ_CA2     0x01
#define VIA_IRQ_ENABLE  0x80

/* Bit MIDI TX na porcie B */
#define MIDI_TX_BIT     0x01
#define MIDI_TX_HIGH    0x01
#define MIDI_TX_LOW     0x00

/* 
 * MIDI baudrate: 31250 bps
 * Czas jednego bitu: 32 us
 * Przy zegarze 1 MHz: 32 cykli na bit
 * Przy zegarze 4 MHz: 128 cykli na bit
 * Przy zegarze 8 MHz: 256 cykli na bit
 *
 * Dla Picocomputer z zegarem 4 MHz:
 * Timer value = 128 - 2 = 126 (kompensacja opoznienia)
 */
#define MIDI_BIT_TIMER_VALUE    126

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
#define MIDI_BUSY           2

/* Prototypy funkcji */
void midi_init(void);
unsigned char midi_send_byte(unsigned char data);
unsigned char midi_send_buffer(const unsigned char *data, unsigned char len);
unsigned char midi_is_busy(void);
unsigned char midi_buffer_free(void);
void midi_flush(void);

/* Funkcje wewnetrzne */
void midi_start_transmission(void);
void midi_stop_transmission(void);

#endif /* MIDI_OUT_H */