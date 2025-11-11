.include "rp6502.inc"

.setcpu "65C02"
.segment "CODE"
.org $8000

@outsider:
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
    rts

.segment "RODATA"

message:
    .byte "Outsider is here!", $0D, $0A, 0
