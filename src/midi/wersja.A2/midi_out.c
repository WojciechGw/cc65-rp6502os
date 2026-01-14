/*
 * MIDI-OUT dla Picocomputer 6502
 * Procesor: W65C02S @ 8 MHz
 * VIA: W65C22S @ $FFD0
 * Pamięć: 64kB RAM
 * 
 * Handler przerwań w asemblerze (midi_irq.s)
 */

/* ============================================================
 * DEFINICJE ADRESÓW W65C22S VIA @ $FFD0
 * ============================================================ */
#define VIA_BASE    0xFFD0

#define VIA_ORB     (*(volatile unsigned char*)(VIA_BASE + 0x00))
#define VIA_ORA     (*(volatile unsigned char*)(VIA_BASE + 0x01))
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

/* ============================================================
 * STAŁE KONFIGURACYJNE
 * ============================================================ */

/* 
 * Timer dla 31250 baud @ 8 MHz:
 * 8000000 / 31250 = 256 cykli na bit
 * Wartość timera = 256 - 2 = 254
 */
#define TIMER_VALUE     254

#define MIDI_BUFFER_SIZE    64
#define MIDI_BUFFER_MASK    (MIDI_BUFFER_SIZE - 1)
#define MIDI_PIN            0x01

/* Stany maszyny stanów */
#define TX_STATE_IDLE       0
#define TX_STATE_START      1
#define TX_STATE_DATA       2
#define TX_STATE_STOP       3

/* Komendy MIDI */
#define MIDI_NOTE_OFF       0x80
#define MIDI_NOTE_ON        0x90
#define MIDI_POLY_PRESSURE  0xA0
#define MIDI_CONTROL_CHANGE 0xB0
#define MIDI_PROGRAM_CHANGE 0xC0
#define MIDI_CHAN_PRESSURE  0xD0
#define MIDI_PITCH_BEND     0xE0
#define MIDI_SYSTEM         0xF0

/* ============================================================
 * ZMIENNE ZEWNĘTRZNE (zdefiniowane w midi_irq.s)
 * ============================================================ */

extern volatile unsigned char midi_buffer[];
extern volatile unsigned char midi_head;
extern volatile unsigned char midi_tail;

extern volatile unsigned char tx_state;
extern volatile unsigned char tx_byte;
extern volatile unsigned char tx_bit_count;
extern volatile unsigned char tx_active;

/* ============================================================
 * MAKRA ASM
 * ============================================================ */

#define SEI() __asm__("sei")
#define CLI() __asm__("cli")

/* ============================================================
 * FUNKCJE POMOCNICZE BUFORA
 * ============================================================ */

static unsigned char buffer_is_empty(void)
{
    return (midi_head == midi_tail);
}

static unsigned char buffer_count(void)
{
    return ((midi_head - midi_tail) & MIDI_BUFFER_MASK);
}

/* ============================================================
 * INICJALIZACJA MIDI
 * ============================================================ */

void midi_init(void)
{
    SEI();
    
    /* Inicjalizacja zmiennych stanu */
    midi_head = 0;
    midi_tail = 0;
    tx_state = TX_STATE_IDLE;
    tx_active = 0;
    tx_bit_count = 0;
    tx_byte = 0;
    
    /* Konfiguracja Port B bit 0 jako wyjście */
    VIA_DDRB = VIA_DDRB | MIDI_PIN;
    
    /* Ustaw linię MIDI w stan idle (mark = wysoki) */
    VIA_ORB = VIA_ORB | MIDI_PIN;
    
    /* 
     * Konfiguracja ACR - Timer 1 free-running mode
     * Bit 7: 0 - wyłącz PB7 toggle
     * Bit 6: 1 - Timer 1 free-running (continuous)
     */
    VIA_ACR = (VIA_ACR & 0x3F) | 0x40;
    
    /* Załaduj wartość timera do latch (254 = $FE) */
    VIA_T1LL = TIMER_VALUE;
    VIA_T1LH = 0;
    
    /* Wyczyść ewentualną flagę przerwania */
    (void)VIA_T1CL;
    
    /* Wyłącz przerwanie Timer 1 (włączy się przy wysyłaniu) */
    VIA_IER = 0x40;  /* Bit 7=0 -> clear, bit 6 -> Timer 1 */
}

/* ============================================================
 * URUCHOMIENIE TRANSMISJI
 * ============================================================ */

static void start_transmission(void)
{
    if (tx_active || buffer_is_empty()) {
        return;
    }
    
    /* Pobierz pierwszy bajt z bufora */
    tx_byte = midi_buffer[midi_tail];
    midi_tail = (midi_tail + 1) & MIDI_BUFFER_MASK;
    
    /* Ustaw stan początkowy */
    tx_state = TX_STATE_START;
    tx_bit_count = 0;
    tx_active = 1;
    
    /* Załaduj timer i uruchom (zapis do T1CH startuje timer) */
    VIA_T1CL = TIMER_VALUE;
    VIA_T1CH = 0;
    
    /* Włącz przerwanie Timer 1 */
    VIA_IER = 0xC0;  /* Bit 7=1 -> set, bit 6 -> Timer 1 */
}

/* ============================================================
 * WYSYŁANIE BAJTU (NIEBLOKUJĄCE)
 * ============================================================ */

unsigned char midi_send_byte(unsigned char data)
{
    unsigned char next_head;
    
    next_head = (midi_head + 1) & MIDI_BUFFER_MASK;
    
    /* Sprawdź czy bufor jest pełny */
    if (next_head == midi_tail) {
        return 0;  /* Bufor pełny - odrzuć bajt */
    }
    
    /* Sekcja krytyczna */
    SEI();
    
    /* Dodaj bajt do bufora */
    midi_buffer[midi_head] = data;
    midi_head = next_head;
    
    /* Uruchom transmisję jeśli nieaktywna */
    if (!tx_active) {
        start_transmission();
    }
    
    CLI();
    
    return 1;  /* Sukces */
}

/* ============================================================
 * WYSYŁANIE WIELU BAJTÓW
 * ============================================================ */

unsigned char midi_send_bytes(const unsigned char *data, unsigned char len)
{
    unsigned char i;
    unsigned char sent = 0;
    
    for (i = 0; i < len; i++) {
        if (midi_send_byte(data[i])) {
            sent++;
        } else {
            break;  /* Bufor pełny */
        }
    }
    
    return sent;
}

/* ============================================================
 * FUNKCJE MIDI - WIADOMOŚCI KANAŁOWE
 * ============================================================ */

void midi_note_on(unsigned char channel, unsigned char note,
                  unsigned char velocity)
{
    midi_send_byte(MIDI_NOTE_ON | (channel & 0x0F));
    midi_send_byte(note & 0x7F);
    midi_send_byte(velocity & 0x7F);
}

void midi_note_off(unsigned char channel, unsigned char note,
                   unsigned char velocity)
{
    midi_send_byte(MIDI_NOTE_OFF | (channel & 0x0F));
    midi_send_byte(note & 0x7F);
    midi_send_byte(velocity & 0x7F);
}

void midi_control_change(unsigned char channel, unsigned char controller,
                         unsigned char value)
{
    midi_send_byte(MIDI_CONTROL_CHANGE | (channel & 0x0F));
    midi_send_byte(controller & 0x7F);
    midi_send_byte(value & 0x7F);
}

void midi_program_change(unsigned char channel, unsigned char program)
{
    midi_send_byte(MIDI_PROGRAM_CHANGE | (channel & 0x0F));
    midi_send_byte(program & 0x7F);
}

void midi_pitch_bend(unsigned char channel, unsigned int bend)
{
    midi_send_byte(MIDI_PITCH_BEND | (channel & 0x0F));
    midi_send_byte((unsigned char)(bend & 0x7F));
    midi_send_byte((unsigned char)((bend >> 7) & 0x7F));
}

void midi_aftertouch(unsigned char channel, unsigned char pressure)
{
    midi_send_byte(MIDI_CHAN_PRESSURE | (channel & 0x0F));
    midi_send_byte(pressure & 0x7F);
}

void midi_poly_pressure(unsigned char channel, unsigned char note,
                        unsigned char pressure)
{
    midi_send_byte(MIDI_POLY_PRESSURE | (channel & 0x0F));
    midi_send_byte(note & 0x7F);
    midi_send_byte(pressure & 0x7F);
}

/* ============================================================
 * FUNKCJE MIDI - CONTROL CHANGE SKRÓTY
 * ============================================================ */

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

void midi_volume(unsigned char channel, unsigned char volume)
{
    midi_control_change(channel, 7, volume & 0x7F);
}

void midi_pan(unsigned char channel, unsigned char pan)
{
    midi_control_change(channel, 10, pan & 0x7F);
}

void midi_modulation(unsigned char channel, unsigned char mod)
{
    midi_control_change(channel, 1, mod & 0x7F);
}

void midi_sustain(unsigned char channel, unsigned char on)
{
    midi_control_change(channel, 64, on ? 127 : 0);
}

/* ============================================================
 * FUNKCJE MIDI - SYSTEM
 * ============================================================ */

void midi_clock(void)
{
    midi_send_byte(0xF8);
}

void midi_start(void)
{
    midi_send_byte(0xFA);
}

void midi_continue(void)
{
    midi_send_byte(0xFB);
}

void midi_stop(void)
{
    midi_send_byte(0xFC);
}

void midi_active_sensing(void)
{
    midi_send_byte(0xFE);
}

void midi_system_reset(void)
{
    midi_send_byte(0xFF);
}

/* ============================================================
 * STATUS I KONTROLA
 * ============================================================ */

unsigned char midi_is_busy(void)
{
    return tx_active;
}

unsigned char midi_buffer_free(void)
{
    return MIDI_BUFFER_SIZE - buffer_count() - 1;
}

unsigned char midi_buffer_used(void)
{
    return buffer_count();
}

/* Czeka aż bufor się opróżni (blokujące - używać ostrożnie) */
void midi_flush(void)
{
    while (tx_active) {
        /* Przerwania obsługują transmisję */
    }
}

/* ============================================================
 * PROGRAM GŁÓWNY - PRZYKŁAD
 * ============================================================ */

static void delay_ms(unsigned int ms)
{
    /* 
     * Przybliżone opóźnienie dla 8 MHz
     * Pętla wewnętrzna ~10 cykli = 1.25 µs
     * 800 iteracji ≈ 1 ms
     */
    volatile unsigned int i, j;
    for (i = 0; i < ms; i++) {
        for (j = 0; j < 800; j++) {
            /* Przerwania działają w tle */
        }
    }
}

/* Melodia testowa - "Ode to Joy" (fragment) */
static const unsigned char melody[] = {
    64, 64, 65, 67,  /* E E F G */
    67, 65, 64, 62,  /* G F E D */
    60, 60, 62, 64,  /* C C D E */
    64, 62, 62       /* E D D */
};

static const unsigned char durations[] = {
    250, 250, 250, 250,
    250, 250, 250, 250,
    250, 250, 250, 250,
    375, 125, 500
};

int main(void)
{
    unsigned char i;
    unsigned char note_idx;
    unsigned char octave;
    
    /* Inicjalizacja systemu MIDI */
    midi_init();
    
    /* Włącz przerwania globalne */
    CLI();
    
    /* Krótka pauza na inicjalizację */
    delay_ms(100);
    
    /* Reset syntezatora */
    midi_all_sound_off(0);
    midi_reset_controllers(0);
    
    /* Ustaw instrument - Piano */
    midi_program_change(0, 0);
    
    /* Ustaw głośność */
    midi_volume(0, 100);
    
    /* Ustaw panoramę na środek */
    midi_pan(0, 64);
    
    /* Pauza przed rozpoczęciem */
    delay_ms(500);
    
    /* Główna pętla - graj melodię w nieskończoność */
    octave = 0;
    
    while (1) {
        /* Zagraj całą melodię */
        for (i = 0; i < sizeof(melody); i++) {
            
            /* Sprawdź czy jest miejsce w buforze */
            while (midi_buffer_free() < 6) {
                /* Czekaj na miejsce - przerwania opróżniają bufor */
            }
            
            /* Zagraj nutę (z transpozycją oktawy) */
            note_idx = melody[i] + (octave * 12);
            midi_note_on(0, note_idx, 80);
            
            /* Czas trwania nuty */
            delay_ms(durations[i]);
            
            /* Wyłącz nutę */
            midi_note_off(0, note_idx, 0);
            
            /* Krótka pauza między nutami (staccato) */
            delay_ms(20);
        }
        
        /* Pauza między powtórzeniami */
        delay_ms(1000);
        
        /* Zmień oktawę dla urozmaicenia */
        octave++;
        if (octave > 2) {
            octave = 0;
        }
    }
    
    return 0;
}