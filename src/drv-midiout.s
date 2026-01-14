; midi.s - MIDI OUT 31250 8N1 @ PHI2=8MHz, VIA=$FFD0, TX=PB0, T1 IRQ
; API dla C: midi_init, midi_tx_put, midi_tx_used, midi_tx_free, midi_tx_flush

        .export _midi_init, _midi_tx_put, _midi_tx_used, _midi_tx_free, _midi_tx_flush
        .export _irq_handler          ; jeśli chcesz podpiąć bezpośrednio do wektora IRQ
        .export midi_isr              ; jeśli chcesz wywołać z własnego IRQ handlera

; --- VIA map ---
VIA_BASE = $FFD0
ORB     = VIA_BASE+$00
DDRB    = VIA_BASE+$02
T1CL    = VIA_BASE+$04
T1CH    = VIA_BASE+$05
T1LL    = VIA_BASE+$06
T1LH    = VIA_BASE+$07
ACR     = VIA_BASE+$0B
IFR     = VIA_BASE+$0D
IER     = VIA_BASE+$0E

TXMASK  = $01          ; PB0
T1_BIT  = $40          ; Timer1 flag/enable bit

; --- ZP vars ---
        .segment "ZEROPAGE"
pb_shadow:  .res 1
tx_busy:    .res 1
tx_bits:    .res 1
tx_sh_lo:   .res 1
tx_sh_hi:   .res 1
tx_head:    .res 1
tx_tail:    .res 1

; --- Buffer 256 bytes ---
        .segment "BSS"
midi_tx_buf:
        .res 256

; --- Code ---
        .segment "CODE"
reset:
; void midi_init(void);
_midi_init:
        ; pb_shadow = ORB
        lda ORB
        sta pb_shadow

        ; PB0 output
        lda DDRB
        ora #TXMASK
        sta DDRB

        ; TX idle = 1
        lda pb_shadow
        ora #TXMASK
        sta pb_shadow
        sta ORB

        ; T1 free-run (ACR bit6=1)
        lda ACR
        ora #$40
        sta ACR

        ; disable T1 IRQ (clear bit6)
        lda #T1_BIT
        sta IER

        stz tx_busy
        stz tx_head
        stz tx_tail
        rts


; internal: start TX if idle and buffer not empty
tx_start_if_needed:
        lda tx_head
        cmp tx_tail
        beq @rts

        ldy tx_head
        lda midi_tx_buf,y
        inc tx_head

        ; build frame: (1<<9) | (A<<1)
        sta tx_sh_lo
        stz tx_sh_hi
        asl tx_sh_lo
        rol tx_sh_hi
        lda tx_sh_hi
        ora #$02            ; stop bit (global bit9)
        sta tx_sh_hi

        lda #10
        sta tx_bits
        lda #1
        sta tx_busy

        ; T1 latch = $00FF -> 256 cycles -> 32us
        lda #$FF
        sta T1LL
        lda #$00
        sta T1LH
        sta T1CH            ; start

        ; enable T1 IRQ (bit7=1 set, bit6=1)
        lda #$C0
        sta IER
@rts:
        rts


; uint8_t __fastcall__ midi_tx_put(uint8_t b);
; A=b, returns A: 0 ok, 1 full
_midi_tx_put:
        sei

        ; next = tail + 1 (wrap 0..255)
        ldx tx_tail
        inx

        ; full if next == head
        cpx tx_head
        beq @full

        ; buf[tail] = b
        ldy tx_tail
        sta midi_tx_buf,y

        ; tail = next
        stx tx_tail

        ; if idle -> start
        lda tx_busy
        bne @ok
        jsr tx_start_if_needed

@ok:
        cli
        lda #$00
        rts

@full:
        cli
        lda #$01
        rts


; uint8_t midi_tx_used(void);  A = (tail - head) mod 256
_midi_tx_used:
        sei
        lda tx_tail
        sec
        sbc tx_head
        cli
        rts


; uint8_t midi_tx_free(void);  A = (head - tail - 1) mod 256
; (pojemność efektywna 255)
_midi_tx_free:
        sei
        lda tx_head
        sec
        sbc tx_tail
        sec
        sbc #$01
        cli
        rts


; void midi_tx_flush(void);  (blokujące; IRQ włączone)
_midi_tx_flush:
@loop:
        sei
        lda tx_head
        cmp tx_tail
        bne @not_empty
        lda tx_busy
        bne @not_empty
        cli
        rts
@not_empty:
        cli
        bra @loop


; --- ISR core (bez zapisu rejestrów, do użycia z własnego IRQ) ---
; Zakłada: A/X/Y można nadpisać, a przerwanie już jest "w środku".
midi_isr:
        lda IFR
        and #T1_BIT
        beq @rts

        ; clear T1 flag
        lda T1CL

        lda tx_busy
        beq @rts

        ; output LSB on PB0
        lda tx_sh_lo
        and #$01
        beq @bit0

@bit1:
        lda pb_shadow
        ora #TXMASK
        sta pb_shadow
        sta ORB
        bra @shift

@bit0:
        lda pb_shadow
        and #($FF-TXMASK)
        sta pb_shadow
        sta ORB

@shift:
        lsr tx_sh_hi
        ror tx_sh_lo

        dec tx_bits
        bne @rts

        ; load next byte if any
        lda tx_head
        cmp tx_tail
        beq @stop

        ldy tx_head
        lda midi_tx_buf,y
        inc tx_head

        sta tx_sh_lo
        stz tx_sh_hi
        asl tx_sh_lo
        rol tx_sh_hi
        lda tx_sh_hi
        ora #$02
        sta tx_sh_hi

        lda #10
        sta tx_bits
        rts

@stop:
        ; idle high, disable IRQ
        lda pb_shadow
        ora #TXMASK
        sta pb_shadow
        sta ORB

        stz tx_busy

        lda #T1_BIT
        sta IER               ; clear enable bit6
@rts:
        rts


; --- pełny IRQ handler z RTI (jeśli chcesz go dać w wektor IRQ) ---
        .segment "HANDLERSIRQ"
_irq_handler:
        pha
        phx
        phy

        jsr midi_isr

        ply
        plx
        pla
        rti

_nmi_handler:
        rti

; --- wektory ---
;        .segment "VECTORS"
;
;        .word   _nmi_handler
;        .word   reset
;        .word   _irq_handler
