#ifndef MIDI_H
#define MIDI_H

#include <stdint.h>

/* inicjalizacja */
void midi_init(void);

/* nieblokujące: zwraca 0=OK, 1=pełny bufor */
uint8_t __fastcall__ midi_tx_put(uint8_t b);

/* diagnostyka bufora (0..255, efektywna pojemność 255) */
uint8_t midi_tx_used(void);
uint8_t midi_tx_free(void);

/* opcjonalnie: czekaj aż wszystko wyjdzie */
void midi_tx_flush(void);

#endif
