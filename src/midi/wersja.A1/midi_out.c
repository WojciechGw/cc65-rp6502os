/*
 * MIDI-OUT dla Picocomputer 6502
 * Kompilator: cc65 (standard C89)
 * Procesor: W65C02S
 * VIA: W65C22S
 * 
 * Transmisja MIDI: 31250 baud, 8N1
 * Wyjście: Port B bit 0
 * 
 * Kompilacja:
 *   cl65 -t none -C picocomputer.cfg midi_out.c -o midi_out.bin
 */

#include <stdint.h>
#include <string.h>

/* ============================================================
 * DEFINICJE ADRESÓW W65C22S VIA
 * Dostosuj VIA_BASE do swojej konfiguracji Picocomputer
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
 * STAŁE MIDI I KONFIGURACJA
 * ============================================================ */

/* 
 * Obliczenie wartości timera dla 31250 baud:
 * Przy zegarze 4 MHz: 4000000 / 31250 = 128 cykli na bit
 * Przy zegarze 8 MHz: 8000000 / 31250 = 256 cykli na bit
 * Timer wymaga wartości N-2 dla N cykli
 */
#define CPU_CLOCK       8000000UL
#define MIDI_BAUD       31250UL
#define TIMER_VALUE     ((CPU_CLOCK / MIDI_BAUD) - 2)

#define MIDI_BUFFER_SIZE    64      /* Musi być potęgą 2 */
#define MIDI_BUFFER_MASK    (MIDI_BUFFER_SIZE - 1)
#define MIDI_PIN            0x01    /* Port B bit 0 */

/* Stany maszyny stanów transmisji */
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

/* ============================================================
 * ZMIENNE GLOBALNE (volatile dla obsługi przerwań)
 * ============================================================ */

/* Bufor cykliczny MIDI */
static volatile unsigned char midi_buffer[MIDI_BUFFER_SIZE];
static volatile unsigned char midi_head;    /* Indeks zapisu */
static volatile unsigned char midi_tail;    /* Indeks odczytu */

/* Stan transmisji */
static volatile unsigned char tx_state;
static volatile unsigned char tx_byte;
static volatile unsigned char tx_bit_count;
static volatile unsigned char tx_active;

/* ============================================================
 * MAKRA INLINE ASM
 * ============================================================ */

#define SEI() __asm__("sei")
#define CLI() __asm__("cli")

/* ============================================================
 * FUNKCJE POMOCNICZE BUFORA
 * ============================================================ */

/* Sprawdza czy bufor jest pusty */
static unsigned char buffer_is_empty(void)
{
    return (midi_head == midi_tail);
}

/* Sprawdza czy bufor jest pełny */
static unsigned char buffer_is_full(void)
{
    return (((midi_head + 1) & MIDI_BUFFER_MASK) == midi_tail);
}

/* Zwraca liczbę bajtów w buforze */
static unsigned char buffer_count(void)
{
    return ((midi_head - midi_tail) & MIDI_BUFFER_MASK);
}

/* ============================================================
 * INICJALIZACJA MIDI
 * ============================================================ */

void midi_init(void)
{
    /* Wyłącz przerwania podczas konfiguracji */
    SEI();
    
    /* Inicjalizacja bufora */
    midi_head = 0;
    midi_tail = 0;
    tx_state = TX_STATE_IDLE;
    tx_active = 0;
    tx_bit_count = 0;
    
    /* Konfiguracja Port B bit 0 jako wyjście */
    VIA_DDRB = VIA_DDRB | MIDI_PIN;
    
    /* Ustaw linię w stan idle (wysoki) - MIDI idle = mark = high */
    VIA_ORB = VIA_ORB | MIDI_PIN;
    
    /* Konfiguracja ACR - Timer 1 w trybie free-running */
    /* Bit 7: 0 - bez PB7 toggle
     * Bit 6: 1 - Timer 1 free-running mode
     */
    VIA_ACR = (VIA_ACR & 0x3F) | 0x40;
    
    /* Załaduj wartość timera do latch */
    VIA_T1LL = (unsigned char)(TIMER_VALUE & 0xFF);
    VIA_T1LH = (unsigned char)((TIMER_VALUE >> 8) & 0xFF);
    
    /* Wyczyść flagę przerwania Timer 1 przez odczyt T1CL */
    (void)VIA_T1CL;
    
    /* Na razie NIE włączamy przerwania Timer 1 - 
     * włączymy gdy będą dane do wysłania */
    VIA_IER = 0x40;  /* Wyłącz (bit 7 = 0 = clear) */
}

/* ============================================================
 * ROZPOCZĘCIE TRANSMISJI
 * ============================================================ */

static void start_transmission(void)
{
    if (tx_active || buffer_is_empty()) {
        return;
    }
    
    /* Pobierz bajt z bufora */
    tx_byte = midi_buffer[midi_tail];
    midi_tail = (midi_tail + 1) & MIDI_BUFFER_MASK;
    
    /* Ustaw stan początkowy */
    tx_state = TX_STATE_START;
    tx_bit_count = 0;
    tx_active = 1;
    
    /* Załaduj i uruchom timer */
    VIA_T1CL = (unsigned char)(TIMER_VALUE & 0xFF);
    VIA_T1CH = (unsigned char)((TIMER_VALUE >> 8) & 0xFF);
    
    /* Włącz przerwanie Timer 1 */
    VIA_IER = 0xC0;  /* Bit 7 = 1 (set), bit 6 = Timer 1 */
}

/* ============================================================
 * DODAWANIE DANYCH DO BUFORA (NIEBLOKUJĄCE)
 * ============================================================ */

unsigned char midi_send_byte(unsigned char byte)
{
    unsigned char next_head;
    unsigned char sreg;
    
    next_head = (midi_head + 1) & MIDI_BUFFER_MASK;
    
    /* Sprawdź czy bufor pełny */
    if (next_head == midi_tail) {
        return 0;  /* Bufor pełny - odrzuć */
    }
    
    /* Sekcja krytyczna - wyłącz przerwania */
    SEI();
    
    /* Dodaj do bufora */
    midi_buffer[midi_head] = byte;
    midi_head = next_head;
    
    /* Uruchom transmisję jeśli nieaktywna */
    if (!tx_active) {
        start_transmission();
    }
    
    /* Włącz przerwania */
    CLI();
    
    return 1;  /* Sukces */
}

/* ============================================================
 * HANDLER PRZERWANIA - TRANSMISJA BITOWA
 * Ta funkcja jest wywoływana przez główny handler IRQ
 * ============================================================ */

void midi_irq_handler(void)
{
    unsigned char ifr;
    
    /* Sprawdź czy to przerwanie od Timer 1 */
    ifr = VIA_IFR;
    if (!(ifr & 0x40)) {
        return;  /* Nie nasze przerwanie */
    }
    
    /* Wyczyść flagę przerwania przez odczyt T1CL */
    (void)VIA_T1CL;
    
    /* Maszyna stanów transmisji MIDI */
    switch (tx_state) {
        
        case TX_STATE_START:
            /* Wyślij start bit (niski stan = space) */
            VIA_ORB = VIA_ORB & ~MIDI_PIN;
            tx_state = TX_STATE_DATA;
            tx_bit_count = 0;
            break;
            
        case TX_STATE_DATA:
            /* Wyślij bit danych (LSB first) */
            if (tx_byte & 0x01) {
                VIA_ORB = VIA_ORB | MIDI_PIN;   /* 1 = mark = high */
            } else {
                VIA_ORB = VIA_ORB & ~MIDI_PIN;  /* 0 = space = low */
            }
            tx_byte = tx_byte >> 1;
            tx_bit_count++;
            
            if (tx_bit_count >= 8) {
                tx_state = TX_STATE_STOP;
            }
            break;
            
        case TX_STATE_STOP:
            /* Wyślij stop bit (wysoki stan = mark) */
            VIA_ORB = VIA_ORB | MIDI_PIN;
            
            /* Sprawdź czy są kolejne dane */
            if (midi_head != midi_tail) {
                /* Pobierz następny bajt */
                tx_byte = midi_buffer[midi_tail];
                midi_tail = (midi_tail + 1) & MIDI_BUFFER_MASK;
                tx_state = TX_STATE_START;
                tx_bit_count = 0;
            } else {
                /* Brak danych - przejdź do idle */
                tx_state = TX_STATE_IDLE;
                tx_active = 0;
                /* Wyłącz przerwanie Timer 1 */
                VIA_IER = 0x40;
            }
            break;
            
        case TX_STATE_IDLE:
        default:
            /* Nie powinno się zdarzyć - wyłącz timer */
            tx_active = 0;
            VIA_IER = 0x40;
            break;
    }
}

/* ============================================================
 * FUNKCJE POMOCNICZE MIDI
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

/* Wszystkie nuty off na kanale */
void midi_all_notes_off(unsigned char channel)
{
    midi_control_change(channel, 123, 0);
}

/* ============================================================
 * SPRAWDZANIE STANU (NIEBLOKUJĄCE)
 * ============================================================ */

unsigned char midi_is_busy(void)
{
    return tx_active;
}

unsigned char midi_buffer_free(void)
{
    return MIDI_BUFFER_SIZE - buffer_count() - 1;
}

/* ============================================================
 * GŁÓWNY HANDLER IRQ (do umieszczenia w wektorze przerwań)
 * ============================================================ */

/* 
 * Ten kod musi być połączony z wektorem IRQ systemu.
 * Dla cc65, używamy pragma lub ręcznej konfiguracji.
 */

#pragma code-name (push, "LOWCODE")

void irq_handler(void)
{
    /* Obsłuż przerwanie MIDI */
    midi_irq_handler();
    
    /* Tu można dodać obsługę innych przerwań */
}

#pragma code-name (pop)

/* ============================================================
 * GŁÓWNY PROGRAM - PRZYKŁAD UŻYCIA
 * ============================================================ */

/* Prosta funkcja opóźnienia (nieblokująca wobec przerwań) */
static void delay(unsigned int count)
{
    volatile unsigned int i;
    for (i = 0; i < count; i++) {
        /* Pętla pusta - przerwania nadal działają */
    }
}

/* Przykładowa sekwencja MIDI */
static const unsigned char melody[] = {
    60, 62, 64, 65, 67, 69, 71, 72  /* C major scale */
};

int main(void)
{
    unsigned char i;
    unsigned char note_index;
    
    /* Inicjalizacja systemu MIDI */
    midi_init();
    
    /* 
     * WAŻNE: Musisz zainstalować irq_handler w wektorze IRQ.
     * Dla Picocomputer, zależnie od konfiguracji:
     * - Wektor IRQ pod adresem $FFFE-$FFFF
     * - Lub użyj mechanizmu systemu
     */
    
    /* Włącz globalne przerwania */
    CLI();
    
    /* Wyślij wiadomość inicjalizującą - All Notes Off */
    midi_all_notes_off(0);
    
    /* Ustaw instrument (Program Change) */
    midi_program_change(0, 0);  /* Piano */
    
    /* Główna pętla - całkowicie nieblokująca */
    note_index = 0;
    
    while (1) {
        /* Sprawdź czy jest miejsce w buforze */
        if (midi_buffer_free() >= 6) {
            
            /* Zagraj nutę */
            midi_note_on(0, melody[note_index], 100);
            
            /* Krótkie opóźnienie (przerwania działają!) */
            delay(20000);
            
            /* Wyłącz nutę */
            midi_note_off(0, melody[note_index], 0);
            
            /* Krótka pauza między nutami */
            delay(5000);
            
            /* Następna nuta w sekwencji */
            note_index++;
            if (note_index >= sizeof(melody)) {
                note_index = 0;
                delay(30000);  /* Dłuższa pauza na końcu */
            }
        }
        
        /* 
         * Tu możesz wykonywać inne zadania.
         * Transmisja MIDI działa w tle dzięki przerwaniom!
         */
    }
    
    return 0;
}