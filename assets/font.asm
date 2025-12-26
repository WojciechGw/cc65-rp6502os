; TX port terminala
.org $9000

RIA_READY .equ $FFE0
RIA_TX .equ $FFE1
RANGE_START .equ $FE
RANGE_END .equ $FF

        ; first range
        lda #$40
        sta RANGE_START
        lda #$80
        sta RANGE_END
        jsr printascii
        ; second range
        lda #$81
        sta RANGE_START
        lda #$FF
        sta RANGE_END
        jsr printascii
        rts

printascii:
        pha
        phx
        phy
        ldy RANGE_START
loop:
        jsr wait_tx_ready
        sty RIA_TX
        iny
        cpy RANGE_END
        bne loop
        lda #$0A
        jsr wait_tx_ready
        sta RIA_TX
        lda #$0D
        jsr wait_tx_ready
        sta RIA_TX
        ply
        plx
        pla
        rts

; wait for bit 7 RIA_READY
wait_tx_ready:
        bit RIA_READY
        bpl wait_tx_ready
        rts
