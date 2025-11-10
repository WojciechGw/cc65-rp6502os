.include "rp6502.inc"

    .setcpu "65C02"
    .segment "CODE"

_test:

    ; 6502 doesn't reset these
    ldx #$FF
    txs
    cld

; Print "Hello, world!" message
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

; Halts the 6502 by pulling RESB low
_exit:
    lda #RIA_OP_EXIT
    sta RIA_OP

message:
    .byte "Hello, QASM is here!", $0D, $0A, 0
