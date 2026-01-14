/*
 * midi_out.c - Sterownik MIDI-OUT dla Picocomputer 6502
 * Wykorzystuje Timer 2 W65C22S do generowania bitow UART
 * Transmisja 31250 baud, 8N1
 * 
 * Kompilator: cc65 (standard C89)
 */

#include "midi_out.h"

/* Zmienne globalne - volatile bo uzywane w przerwaniu */
static volatile unsigned char midi_buffer[MIDI_BUFFER_SIZE];
static volatile unsigned char midi_head;          /* Indeks zapisu */
static volatile unsigned char midi_tail;          /* Indeks odczytu */
static volatile unsigned char midi_tx_state;      /* Stan maszyny TX */
static volatile unsigned char midi_current_byte;  /* Aktualnie wysylany bajt */
static volatile unsigned char midi_bit_count;     /* Licznik bitow danych */

/* Deklaracja handlera przerwania w asemblerze */
extern void irq_handler(void);

/*
 * Inicjalizacja systemu MIDI
 */
void midi_init(void)
{
    /* Wylacz przerwania na czas konfiguracji */
    __asm__("sei");
    
    /* Inicjalizacja bufora kolowego */
    midi_head = 0;
    midi_tail = 0;
    midi_tx_state = TX_STATE_IDLE;
    midi_current_byte = 0;
    midi_bit_count = 0;
    
    /* Konfiguracja portu B bit 0 jako wyjscie */
    VIA_DDRB |= MIDI_TX_BIT;
    
    /* Ustaw linie TX w stan wysoki (idle) */
    VIA_PORTB |= MIDI_TX_HIGH;
    
    /* Konfiguracja Timer 2 - tryb one-shot */
    /* ACR bit 5 = 0: Timer 2 w trybie one-shot */
    VIA_ACR &= ~0x20;
    
    /* Wylacz przerwanie Timer 2 na poczatek */
    VIA_IER = VIA_IRQ_T2;  /* Zapis bez bitu 7 = wylaczenie */
    
    /* Wyczysc flage przerwania Timer 2 */
    (void)VIA_T2CL;
    
    /* Wlacz przerwania */
    __asm__("cli");
}

/*
 * Dodaj bajt do bufora MIDI
 * Zwraca: MIDI_OK lub MIDI_BUFFER_FULL
 */
unsigned char midi_send_byte(unsigned char data)
{
    unsigned char next_head;
    
    /* Oblicz nastepna pozycje head */
    next_head = (midi_head + 1) & MIDI_BUFFER_MASK;
    
    /* Sprawdz czy bufor jest pelny */
    if (next_head == midi_tail) {
        return MIDI_BUFFER_FULL;
    }
    
    /* Dodaj dane do bufora */
    midi_buffer[midi_head] = data;
    midi_head = next_head;
    
    /* Jesli transmisja nie jest aktywna, rozpocznij ja */
    if (midi_tx_state == TX_STATE_IDLE) {
        midi_start_transmission();
    }
    
    return MIDI_OK;
}

/*
 * Wyslij bufor danych MIDI
 * Zwraca liczbe bajtow dodanych do bufora
 */
unsigned char midi_send_buffer(const unsigned char *data, unsigned char len)
{
    unsigned char i;
    unsigned char sent = 0;
    
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
    return (midi_tx_state != TX_STATE_IDLE) || (midi_head != midi_tail);
}

/*
 * Zwroc liczbe wolnych miejsc w buforze
 */
unsigned char midi_buffer_free(void)
{
    unsigned char used;
    
    if (midi_head >= midi_tail) {
        used = midi_head - midi_tail;
    } else {
        used = MIDI_BUFFER_SIZE - midi_tail + midi_head;
    }
    
    return MIDI_BUFFER_SIZE - 1 - used;
}

/*
 * Poczekaj na wyslanie wszystkich danych
 */
void midi_flush(void)
{
    while (midi_is_busy()) {
        /* Czekaj - mozna tu dodac oszczedzanie energii */
    }
}

/*
 * Rozpocznij transmisje - pobierz bajt z bufora
 */
void midi_start_transmission(void)
{
    /* Sprawdz czy sa dane do wyslania */
    if (midi_head == midi_tail) {
        midi_tx_state = TX_STATE_IDLE;
        return;
    }
    
    /* Pobierz bajt z bufora */
    midi_current_byte = midi_buffer[midi_tail];
    midi_tail = (midi_tail + 1) & MIDI_BUFFER_MASK;
    
    /* Ustaw stan na START */
    midi_tx_state = TX_STATE_START;
    midi_bit_count = 0;
    
    /* Wlacz przerwanie Timer 2 */
    VIA_IER = VIA_IRQ_ENABLE | VIA_IRQ_T2;
    
    /* Uruchom timer z pierwszym opoznieniem */
    VIA_T2CL = MIDI_BIT_TIMER_VALUE & 0xFF;
    VIA_T2CH = (MIDI_BIT_TIMER_VALUE >> 8) & 0xFF;
}

/*
 * Zatrzymaj transmisje
 */
void midi_stop_transmission(void)
{
    /* Wylacz przerwanie Timer 2 */
    VIA_IER = VIA_IRQ_T2;  /* Bez bitu 7 = wylaczenie */
    
    /* Wyczysc flage przerwania */
    (void)VIA_T2CL;
    
    /* Ustaw linie TX w stan wysoki */
    VIA_PORTB |= MIDI_TX_HIGH;
    
    midi_tx_state = TX_STATE_IDLE;
}

/*
 * Handler przerwania Timer 2 - wywoływany z asemblera
 * Ta funkcja realizuje maszyne stanow transmisji UART
 */
void midi_timer_isr(void)
{
    /* Wyczysc flage przerwania Timer 2 przez odczyt T2CL */
    (void)VIA_T2CL;
    
    switch (midi_tx_state) {
        case TX_STATE_START:
            /* Wyslij bit startu (LOW) */
            VIA_PORTB &= ~MIDI_TX_BIT;
            midi_tx_state = TX_STATE_DATA;
            midi_bit_count = 0;
            break;
            
        case TX_STATE_DATA:
            /* Wyslij bit danych (LSB first) */
            if (midi_current_byte & 0x01) {
                VIA_PORTB |= MIDI_TX_HIGH;
            } else {
                VIA_PORTB &= ~MIDI_TX_BIT;
            }
            
            /* Przesun do nastepnego bitu */
            midi_current_byte >>= 1;
            ++midi_bit_count;
            
            /* Sprawdz czy wyslano 8 bitow */
            if (midi_bit_count >= 8) {
                midi_tx_state = TX_STATE_STOP;
            }
            break;
            
        case TX_STATE_STOP:
            /* Wyslij bit stopu (HIGH) */
            VIA_PORTB |= MIDI_TX_HIGH;
            
            /* Sprawdz czy sa nastepne dane do wyslania */
            if (midi_head != midi_tail) {
                /* Pobierz nastepny bajt */
                midi_current_byte = midi_buffer[midi_tail];
                midi_tail = (midi_tail + 1) & MIDI_BUFFER_MASK;
                midi_tx_state = TX_STATE_START;
            } else {
                /* Koniec transmisji */
                midi_tx_state = TX_STATE_IDLE;
                /* Wylacz przerwanie Timer 2 */
                VIA_IER = VIA_IRQ_T2;
                return;  /* Nie uruchamiaj ponownie timera */
            }
            break;
            
        default:
            /* Stan nieznany - zatrzymaj */
            midi_stop_transmission();
            return;
    }
    
    /* Uruchom timer dla nastepnego bitu */
    VIA_T2CL = MIDI_BIT_TIMER_VALUE & 0xFF;
    VIA_T2CH = (MIDI_BIT_TIMER_VALUE >> 8) & 0xFF;
}

/* ============================================ */
/* Przykladowy program glowny                   */
/* ============================================ */

/* Komendy MIDI */
#define MIDI_NOTE_ON(ch)    (0x90 | ((ch) & 0x0F))
#define MIDI_NOTE_OFF(ch)   (0x80 | ((ch) & 0x0F))
#define MIDI_CTRL_CHG(ch)   (0xB0 | ((ch) & 0x0F))
#define MIDI_PROG_CHG(ch)   (0xC0 | ((ch) & 0x0F))

/* Prosta funkcja opoznienia bez blokowania MIDI */
static void delay_ms(unsigned int ms)
{
    unsigned int i, j;
    for (i = 0; i < ms; ++i) {
        for (j = 0; j < 100; ++j) {
            /* Pusta petla - ok. 1ms przy 4MHz */
            __asm__("nop");
        }
    }
}

/* Wyslij Note On */
static void midi_note_on(unsigned char channel, 
                         unsigned char note, 
                         unsigned char velocity)
{
    midi_send_byte(MIDI_NOTE_ON(channel));
    midi_send_byte(note & 0x7F);
    midi_send_byte(velocity & 0x7F);
}

/* Wyslij Note Off */
static void midi_note_off(unsigned char channel, 
                          unsigned char note, 
                          unsigned char velocity)
{
    midi_send_byte(MIDI_NOTE_OFF(channel));
    midi_send_byte(note & 0x7F);
    midi_send_byte(velocity & 0x7F);
}

/* Wyslij Program Change */
static void midi_program_change(unsigned char channel, 
                                unsigned char program)
{
    midi_send_byte(MIDI_PROG_CHG(channel));
    midi_send_byte(program & 0x7F);
}

/* Przykladowa sekwencja MIDI - arpeggio C-dur */
static const unsigned char arpeggio[] = {
    60, 64, 67, 72, 67, 64  /* C4, E4, G4, C5, G4, E4 */
};

int main(void)
{
    unsigned char i;
    unsigned char note_idx;
    
    /* Inicjalizacja MIDI */
    midi_init();
    
    /* Ustaw instrument na Piano (program 0) */
    midi_program_change(0, 0);
    
    /* Glowna petla programu */
    note_idx = 0;
    
    while (1) {
        /* Zagraj arpeggio w sposob nieblokujacy */
        for (i = 0; i < sizeof(arpeggio); ++i) {
            /* Note On */
            midi_note_on(0, arpeggio[i], 100);
            
            /* Czekaj 200ms - program moze robic inne rzeczy */
            delay_ms(200);
            
            /* Note Off */
            midi_note_off(0, arpeggio[i], 0);
            
            /* Krotka przerwa miedzy nutami */
            delay_ms(50);
        }
        
        /* Przerwa miedzy powtorzeniami */
        delay_ms(500);
    }
    
    return 0;
}