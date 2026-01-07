#include <rp6502.h>
#include <6502.h>
#include <stdint.h>
#include <stdio.h>

static uint8_t via_irq_stack[256];

static volatile uint8_t tx_byte = 0;
//static volatile uint8_t tx_state = 10; // 10 = idle
static volatile uint16_t delay_counter = 0;
//static volatile uint8_t notes_index = 0;
// static volatile uint8_t send_stage = 0;
//static const uint8_t midi_cmd_prev[]   = { 0xB0, 0xB0, 0xC0,   90,   90,   90,   90,   90,   90,   90,   90 };
//static const uint8_t notes[]           = {  121,  123, 0x0C,   60,   62,   64,   65,   67,   69,   71,   72 };
//static const uint8_t midi_cmd_after[]  = { 0x00, 0x00, 0x00, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F };

//static const uint8_t midi_cmd_prev[]   = {   90 };
//static const uint8_t notes[]           = { 0x55 };
//static const uint8_t midi_cmd_after[]  = { 0x7F };

static volatile uint16_t tx_shift = 0;   // start + data + stop
static volatile uint8_t  bits_left = 0;  // ile bitów do wysłania (0=idle)

#define T1_BIT 0x40
#define T1_RUN 0xC0

unsigned char midi_irq_fn(void)
{
    uint8_t save_step0 = RIA.step0;
    uint16_t save_addr0 = RIA.addr0;
    VIA.ifr = 0x40;

    if (bits_left) {
        // wyślij LSB ramki
        VIA.prb = (VIA.prb & ~0x01) | (tx_shift & 0x01);
        tx_shift >>= 1;
        --bits_left;
    } else if (delay_counter == 0) {
        // załaduj nową ramkę: start=0, data=tx_byte (LSB first), stop=1
        tx_byte = 0x22;              // lub z bufora/sekwencji
        tx_shift = (1u << 9) | (tx_byte << 1); // [stop][data...][start]
        bits_left = 10;
        delay_counter = 157;
    } else {
        --delay_counter;
    }

    RIA.addr0 = save_addr0;
    RIA.step0 = save_step0;
    return IRQ_HANDLED;
}

void main()
{
    uint8_t cnt=0;
    delay_counter = 0;

    VIA.ddrb = 0b00000001;     // PA0 as output
    VIA.prb  = 0b00000001;     // idle state = HIGH

    VIA.t1_lo = 0xFF;
    VIA.t1_hi = 0x00;
    VIA.t1l_lo = 0xFF;
    VIA.t1l_hi = 0x00;

    VIA.acr = 0x40;            // Timer1 free-run
    VIA.ier = T1_RUN;            // Enable T1 interrupt
    set_irq(midi_irq_fn, &via_irq_stack, sizeof(via_irq_stack));

    while(1)
    {
        SEI();
        putchar('.');
        CLI();
    }
}
