/*
 * midi_out.c - Sterownik MIDI-OUT dla Picocomputer 6502
 * VIA W65C22S @ $FFD0, Zegar 8 MHz
 * Handler przerwan w asemblerze
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
    
    /* Oblicz nastepna pozycje head */
    next_head = (midi_head + 1) & MIDI_BUFFER_MASK;
    
    /* Sprawdz czy bufor jest pelny */
    if (next_head == midi_tail) {
        return MIDI_BUFFER_FULL;
    }
    
    /* Sekcja krytyczna - wylacz przerwania */
    __asm__("php");         /* Zachowaj flagi */
    __asm__("sei");         /* Wylacz przerwania */
    
    /* Dodaj dane do bufora */
    midi_buffer[midi_head] = data;
    midi_head = next_head;
    
    /* Zapamietaj stan przed ewentualnym uruchomieniem */
    current_state = midi_tx_state;
    
    /* Wlacz przerwania przed wywolaniem funkcji */
    __asm__("plp");         /* Przywroc flagi (w tym I) */
    
    /* Jesli transmisja nie jest aktywna, rozpocznij ja */
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
 * Sprawdz czy transmisja jest w toku
 */
unsigned char midi_is_busy(void)
{
    unsigned char busy;
    
    /* Atomowy odczyt */
    __asm__("php");
    __asm__("sei");
    
    busy = (midi_tx_state != TX_STATE_IDLE) || (midi_head != midi_tail);
    
    __asm__("plp");
    
    return busy;
}

/*
 * Zwroc liczbe wolnych miejsc w buforze
 */
unsigned char midi_buffer_free(void)
{
    unsigned char head;
    unsigned char tail;
    unsigned char used;
    
    /* Atomowy odczyt */
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

/*
 * Poczekaj na wyslanie wszystkich danych (blokujace)
 */
void midi_flush(void)
{
    while (midi_is_busy()) {
        /* 
         * Instrukcja WAI (Wait for Interrupt) - 65C02
         * Oszczedza energie i czeka na przerwanie
         */
        __asm__("wai");
    }
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
    /* CC #123 = All Notes Off */
    midi_control_change(channel, 123, 0);
}

void midi_all_sound_off(unsigned char channel)
{
    /* CC #120 = All Sound Off */
    midi_control_change(channel, 120, 0);
}

void midi_reset_controllers(unsigned char channel)
{
    /* CC #121 = Reset All Controllers */
    midi_control_change(channel, 121, 0);
}

/* ============================================ */
/* Program glowny - przyklad                    */
/* ============================================ */

/*
 * Opoznienie w milisekundach (przyblizone dla 8 MHz)
 * Nieblokujace dla MIDI - transmisja dziala w tle
 */
static void delay_ms(unsigned int ms)
{
    unsigned int i;
    unsigned int j;
    
    for (i = 0; i < ms; ++i) {
        /* Okolo 800 cykli = 0.1ms przy 8MHz, wiec 10 iteracji ~= 1ms */
        for (j = 0; j < 200; ++j) {
            __asm__("nop");
            __asm__("nop");
            __asm__("nop");
            __asm__("nop");
        }
    }
}

/* Nuty skali C-dur */
static const unsigned char c_major_scale[] = {
    60, 62, 64, 65, 67, 69, 71, 72  /* C4 D4 E4 F4 G4 A4 B4 C5 */
};

/* Akord C-dur */
static const unsigned char chord_c[] = { 60, 64, 67 };

/* Akord F-dur */
static const unsigned char chord_f[] = { 65, 69, 72 };

/* Akord G-dur */
static const unsigned char chord_g[] = { 67, 71, 74 };

/* Zagraj akord */
static void play_chord(const unsigned char *notes, 
                       unsigned char count,
                       unsigned char velocity)
{
    unsigned char i;
    for (i = 0; i < count; ++i) {
        midi_note_on(0, notes[i], velocity);
    }
}

/* Zwolnij akord */
static void release_chord(const unsigned char *notes,
                          unsigned char count)
{
    unsigned char i;
    for (i = 0; i < count; ++i) {
        midi_note_off(0, notes[i], 0);
    }
}

int main(void)
{
    unsigned char i;
    unsigned char note;
    unsigned char octave;
    
    /* Inicjalizacja MIDI */
    midi_init();
    
    /* Czekaj na stabilizacje */
    delay_ms(100);
    
    /* Ustaw instrument - Grand Piano */
    midi_program_change(0, 0);
    
    /* Ustaw glosnosc */
    midi_control_change(0, 7, 100);  /* Volume */
    midi_control_change(0, 10, 64);  /* Pan - center */
    
    /* Glowna petla */
    while (1) {
        
        /* === Sekwencja 1: Skala C-dur w gore === */
        for (i = 0; i < 8; ++i) {
            note = c_major_scale[i];
            midi_note_on(0, note, 80);
            delay_ms(150);
            midi_note_off(0, note, 0);
            delay_ms(20);
        }
        
        /* === Sekwencja 2: Skala C-dur w dol === */
        for (i = 8; i > 0; --i) {
            note = c_major_scale[i - 1];
            midi_note_on(0, note, 80);
            delay_ms(150);
            midi_note_off(0, note, 0);
            delay_ms(20);
        }
        
        delay_ms(300);
        
        /* === Sekwencja 3: Progresja akordow === */
        
        /* C-dur */
        play_chord(chord_c, 3, 90);
        delay_ms(400);
        release_chord(chord_c, 3);
        delay_ms(50);
        
        /* F-dur */
        play_chord(chord_f, 3, 90);
        delay_ms(400);
        release_chord(chord_f, 3);
        delay_ms(50);
        
        /* G-dur */
        play_chord(chord_g, 3, 90);
        delay_ms(400);
        release_chord(chord_g, 3);
        delay_ms(50);
        
        /* C-dur (zakonczenie) */
        play_chord(chord_c, 3, 100);
        delay_ms(800);
        release_chord(chord_c, 3);
        
        delay_ms(500);
        
        /* === Sekwencja 4: Arpeggio z pitch bend === */
        for (octave = 0; octave < 3; ++octave) {
            for (i = 0; i < 3; ++i) {
                note = chord_c[i] + (octave * 12);
                if (note <= 127) {
                    midi_note_on(0, note, 70);
                    delay_ms(100);
                    midi_note_off(0, note, 0);
                }
            }
        }
        
        delay_ms(500);
        
        /* Czekaj na wyslanie wszystkich danych */
        midi_flush();
        
        delay_ms(1000);
    }
    
    return 0;
}
