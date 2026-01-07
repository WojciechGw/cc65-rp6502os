#include "drv-midiout.h"

static void midi_note_on(uint8_t ch, uint8_t note, uint8_t vel) {
    (void)midi_tx_put(0x90 | (ch & 0x0F));
    (void)midi_tx_put(note);
    (void)midi_tx_put(vel);
}

int main(void) {
    midi_init();

    midi_note_on(0x90, 60, 0x7F);

    for (;;) {
        /* reszta programu */
    }
}
