.export _test
.export _end

.include "rp6502.inc"

.segment "CODE"

_test:

    ;save A,X,Y,PC
    sta $F0
    stx $F1
    sty $F2
    pla
    sta $F3 ;PC LSB
    pla
    sta $F4 ;PC MSB

    ; 6502 doesn't reset these
    ldx #$FF
    txs
    cld

; Print message
    ldx #0
@loop:
    lda message,x
    beq @done           ; If zero, we're done
@wait:
    bit RIA_READY       ; Waiting on UART tx ready
    bpl @wait
    sta RIA_TX          ; Transmit the byte
    inx
    bne @loop           ; Continue loop
@done:
    tsx
    ;restore A,X,Y,PC
    lda $F0
    ldx $F1
    ldy $F2
    lda $F4 ;PC MSB
    pha
    lda $F3 ;PC LSB
    pha
    rts

; Halts the 6502 by pulling RESB low
_end:
    lda #RIA_OP_EXIT
    sta RIA_OP

.segment "RODATA"

message:
    .byte "Hello, QASM is here!", $0D, $0A, 0
