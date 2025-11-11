.include "rp6502.inc"

    .setcpu "65C02"
    .segment "CODE"

@outsider:
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
    rts

message:
    .byte "Outsider is here!", $0D, $0A, 0
