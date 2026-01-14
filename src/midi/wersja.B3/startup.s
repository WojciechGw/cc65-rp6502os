;
; startup.s - Handler przerwan MIDI dla RP6502
; VIA W65C22S @ $FFD0
;

.export _irq_handler
.export _midi_init
.export _midi_start_transmission
.export _midi_stop_transmission
.export _midi_is_busy
.export _midi_flush

.exportzp _midi_head
.exportzp _midi_tail
.exportzp _midi_tx_state
.export _midi_buffer

; ============================================
; Stale
; ============================================

VIA_BASE        = $FFD0
VIA_PORTB       = VIA_BASE + $00
VIA_DDRB        = VIA_BASE + $02
VIA_T2CL        = VIA_BASE + $08
VIA_T2CH        = VIA_BASE + $09
VIA_ACR         = VIA_BASE + $0B
VIA_IFR         = VIA_BASE + $0D
VIA_IER         = VIA_BASE + $0E

IRQ_T2          = $20
IRQ_ENABLE      = $80

MIDI_TX_BIT     = $01
MIDI_TX_MASK    = $FE

TX_STATE_IDLE   = 0
TX_STATE_START  = 1
TX_STATE_DATA   = 2
TX_STATE_STOP   = 3

MIDI_BIT_TIMER_L = 254
MIDI_BIT_TIMER_H = 0

MIDI_BUFFER_SIZE = 64
MIDI_BUFFER_MASK = MIDI_BUFFER_SIZE - 1

; ============================================
; Segment ZEROPAGE
; ============================================
.segment "ZEROPAGE"

_midi_head:       .res 1
_midi_tail:       .res 1
_midi_tx_state:   .res 1
midi_current_byte: .res 1
midi_bit_count:   .res 1

; ============================================
; Segment BSS
; ============================================
.segment "BSS"

_midi_buffer:     .res MIDI_BUFFER_SIZE

; ============================================
; Segment CODE
; ============================================
.segment "CODE"

; --------------------------------------------
; midi_init
; --------------------------------------------
_midi_init:
        sei

        stz     _midi_head
        stz     _midi_tail
        stz     _midi_tx_state
        stz     midi_current_byte
        stz     midi_bit_count

        lda     VIA_DDRB
        ora     #MIDI_TX_BIT
        sta     VIA_DDRB

        lda     VIA_PORTB
        ora     #MIDI_TX_BIT
        sta     VIA_PORTB

        lda     VIA_ACR
        and     #$DF
        sta     VIA_ACR

        lda     #IRQ_T2
        sta     VIA_IER

        lda     VIA_T2CL

        cli
        rts

; --------------------------------------------
; midi_start_transmission
; --------------------------------------------
_midi_start_transmission:
        lda     _midi_head
        cmp     _midi_tail
        beq     start_nodata

        ldx     _midi_tail
        lda     _midi_buffer,x
        sta     midi_current_byte

        inx
        txa
        and     #MIDI_BUFFER_MASK
        sta     _midi_tail

        lda     #TX_STATE_START
        sta     _midi_tx_state

        stz     midi_bit_count

        lda     #IRQ_ENABLE | IRQ_T2
        sta     VIA_IER

        lda     #MIDI_BIT_TIMER_L
        sta     VIA_T2CL
        lda     #MIDI_BIT_TIMER_H
        sta     VIA_T2CH

        rts

start_nodata:
        lda     #TX_STATE_IDLE
        sta     _midi_tx_state
        rts

; --------------------------------------------
; midi_stop_transmission
; --------------------------------------------
_midi_stop_transmission:
        lda     #IRQ_T2
        sta     VIA_IER

        lda     VIA_T2CL

        lda     VIA_PORTB
        ora     #MIDI_TX_BIT
        sta     VIA_PORTB

        lda     #TX_STATE_IDLE
        sta     _midi_tx_state

        rts

; --------------------------------------------
; midi_is_busy
; --------------------------------------------
_midi_is_busy:
        lda     _midi_tx_state
        bne     is_busy_yes
        lda     _midi_head
        cmp     _midi_tail
        bne     is_busy_yes
        lda     #0
        rts
is_busy_yes:
        lda     #1
        rts

; --------------------------------------------
; midi_flush
; --------------------------------------------
_midi_flush:
flush_loop:
        lda     _midi_tx_state
        bne     flush_loop
        lda     _midi_head
        cmp     _midi_tail
        bne     flush_loop
        rts

; --------------------------------------------
; irq_handler
; --------------------------------------------
_irq_others:
        jmp irq_other
_irq_handler:
        pha
        phx

        lda     VIA_IFR
        and     #IRQ_T2
        beq     _irq_others

        ldx     VIA_T2CL

        lda     _midi_tx_state

        cmp     #TX_STATE_DATA
        beq     do_data
        cmp     #TX_STATE_START
        beq     do_start
        cmp     #TX_STATE_STOP
        beq     do_stop

        jmp     irq_error

do_data:
        lsr     midi_current_byte
        lda     VIA_PORTB
        bcs     do_data_one
        and     #MIDI_TX_MASK
        bra     do_data_store
do_data_one:
        ora     #MIDI_TX_BIT
do_data_store:
        sta     VIA_PORTB
        inc     midi_bit_count
        lda     midi_bit_count
        cmp     #8
        bne     irq_timer
        lda     #TX_STATE_STOP
        sta     _midi_tx_state
        bra     irq_timer

do_start:
        lda     VIA_PORTB
        and     #MIDI_TX_MASK
        sta     VIA_PORTB
        lda     #TX_STATE_DATA
        sta     _midi_tx_state
        stz     midi_bit_count
        bra     irq_timer

do_stop:
        lda     VIA_PORTB
        ora     #MIDI_TX_BIT
        sta     VIA_PORTB

        lda     _midi_head
        cmp     _midi_tail
        beq     do_stop_end

        ldx     _midi_tail
        lda     _midi_buffer,x
        sta     midi_current_byte
        inx
        txa
        and     #MIDI_BUFFER_MASK
        sta     _midi_tail
        lda     #TX_STATE_START
        sta     _midi_tx_state
        stz     midi_bit_count
        bra     irq_timer

do_stop_end:
        lda     #TX_STATE_IDLE
        sta     _midi_tx_state
        lda     #IRQ_T2
        sta     VIA_IER
        bra     irq_exit

irq_timer:
        lda     #MIDI_BIT_TIMER_L
        sta     VIA_T2CL
        lda     #MIDI_BIT_TIMER_H
        sta     VIA_T2CH

irq_exit:
        plx
        pla
        rti

irq_error:
        lda     #IRQ_T2
        sta     VIA_IER
        lda     VIA_PORTB
        ora     #MIDI_TX_BIT
        sta     VIA_PORTB
        lda     #TX_STATE_IDLE
        sta     _midi_tx_state
        bra     irq_exit

irq_other:
        lda     VIA_IFR
        sta     VIA_IFR
        bra     irq_exit
