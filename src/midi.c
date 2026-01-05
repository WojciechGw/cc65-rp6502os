/*
 * Copyright (c) 2025 Rumbledethumps
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * SPDX-License-Identifier: Unlicense
 */

#include <rp6502.h>
#include <6502.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

static uint8_t midi_stack[8];
static int16_t counter;

unsigned char midi_irq_fn(void)
{
    uint8_t save_step0;
    uint16_t save_addr0;
    
    // ISR stuff
    VIA.ifr = 0x40;
    save_addr0 = RIA.addr0;
    save_step0 = RIA.step0;

    if ( counter > 120 ) {
        counter = 0;
    } else {
        ++counter;
    }

    // ISR stuff
    RIA.addr0 = save_addr0;
    RIA.step0 = save_step0;
    return IRQ_HANDLED;
}

static void midi_init(void)
{
    // 31250Hz from the VIA 256 cycles @ 8Mhz
    // t1l_lo 0x00
    // t1l_hi 0x01
    counter = 0;

    VIA.ddra = 0b00000001;
    VIA.t1l_lo = 0xFF;
    VIA.t1l_hi = 0xFF;
    VIA.t1_lo = 0xFF;
    VIA.t1_hi = 0xFF;
    VIA.acr = 0x40;
    VIA.ier = 0xC0;
    set_irq(midi_irq_fn, &midi_stack, sizeof(midi_stack));

}

static void midi(void)
{
    uint16_t c;

    SEI();
    c = counter;
    CLI();

    if(c == 60)     VIA.pra = 0;
    if(c == 120)    VIA.pra = 1;

}

void main()
{
    midi_init();
    while (1)
        midi();
}
