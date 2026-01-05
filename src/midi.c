#include <rp6502.h>
#include <6502.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define MIDI_BUF_SIZE 32

static volatile uint8_t midi_buf[MIDI_BUF_SIZE];
static volatile uint8_t midi_head = 0;
static volatile uint8_t midi_tail = 0;

// Wysyłanie danych MIDI w ISR
static volatile uint8_t tx_byte = 0;
static volatile uint8_t tx_state = 10; // 10 = idle (nic do wysłania)
static volatile uint16_t delay_counter = 0;

static uint8_t midi_stack[MIDI_BUF_SIZE];

// Skala C-dur
static const uint8_t notes[] = {
    60, 62, 64, 65, 67, 69, 71, 72
};
static uint8_t note_index = 0;
static uint8_t step = 0; // 0 = Note On, 1 = Note Off

// ============================
// Funkcja wysyłająca bajt MIDI (inicjuje transmisję)
// ============================
static void send_midi(uint8_t b)
{
    uint8_t next = (midi_head + 1) % MIDI_BUF_SIZE;
    if (next != midi_tail) // unikaj przepełnienia
    {
        midi_buf[midi_head] = b;
        midi_head = next;
    }
}

// ============================
// Przerwanie z Timer1 VIA
// ============================
unsigned char midi_irq_fn(void)
{
    uint8_t save_step0;
    uint16_t save_addr0;

    VIA.ifr = 0x40;
    save_addr0 = RIA.addr0;
    save_step0 = RIA.step0;

    // Wysyłaj tylko gdy coś jest w buforze
    if (tx_state < 10)  {
        
        if (tx_state == 0)
        {
            // start bit
            VIA.pra &= ~0b00000001;
        } else if (tx_state >= 1 && tx_state <= 8) {
            // data bits
            if (tx_byte & 0x01)
                VIA.pra |= 0b00000001;
            else
                VIA.pra &= ~0b00000001;

            tx_byte >>= 1;
        } else if (tx_state == 9) {
            // stop bit
            VIA.pra |= 0b00000001;
        }

        ++tx_state;

    } else if (midi_head != midi_tail) {
        
        tx_byte = midi_buf[midi_tail];
        midi_tail = (midi_tail + 1) % MIDI_BUF_SIZE;
        tx_state = 0;
    
    } else {

        VIA.pra |= 0b00000001;
    }

    RIA.addr0 = save_addr0;
    RIA.step0 = save_step0;
    return IRQ_HANDLED;
}

// ============================
// Inicjalizacja VIA
// ============================
static void midi_init(void)
{

    printf("MIDI init - phase 1\r\n");

    delay_counter = 0;

    VIA.ddra = 0b00000001; // PA0 jako wyjście
    VIA.pra |= 0b00000001; // Linia idle = HIGH

    VIA.acr = 0x40;        // Timer1 free-run z przerwaniami

    VIA.t1l_lo = 0x00;
    VIA.t1l_hi = 0x01;     // 256 cykli przy 8MHz = 31250 przerwań/s
    VIA.t1_lo = 0x00;
    VIA.t1_hi = 0x01;
    VIA.ier = 0xC0;        // Włącz przerwania Timer1

    printf("MIDI init - phase 2\r\n");
    set_irq(midi_irq_fn, &midi_stack[0], sizeof(midi_stack));

    printf("MIDI init - phase 3\r\n");

    printf("MIDI init - end\r\n");

}

// ============================
// Wysyłanie gamy co 500 ms
// ============================
static void midi(void)
{

    ++delay_counter;

    if (delay_counter >= 15625) // ~500 ms (31250 * 0.5)
    {

        if (step == 0)
        {
            // Note On
            send_midi(0x90);            // status
            send_midi(notes[note_index]);
            send_midi(0x7F);            // velocity
            step = 1;
            printf("MIDI play %i-%i\r\n",note_index, notes[note_index]);
        }
        else
        {
            // Note Off
            send_midi(0x80);
            send_midi(notes[note_index]);
            send_midi(0x00);

            ++note_index;
            if (note_index >= sizeof(notes))
                note_index = 0;

            step = 0;
        }
    }
}

void main()
{
    midi_init();

    printf("MIDI tests\r\n---------------\r\n");
    printf("MIDI init\r\n");

    // MIDI INIT SEQUENCE
    printf("MIDI reset controlers\r\n");
    send_midi(0xB0); send_midi(121); send_midi(0);
    printf("MIDI all notes off\r\n");
    send_midi(0xB0); send_midi(123); send_midi(0);
    printf("MIDI set program Vibraphone[12]\r\n");
    send_midi(0xC0); send_midi(12);

    while (1)
        midi();
}
