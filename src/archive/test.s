.export _msg
.export _end

.include "rp6502.inc"
.segment "CODE"

_msg:
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
;    tsx
;    ;restore A,X,Y,PC
;    lda $F0
;    ldx $F1
;    ldy $F2
;    lda $F4 ;PC MSB
;    pha
;    lda $F3 ;PC LSB
;    pha
    rts

; Halts the 6502 by pulling RESB low go to RP6502 monitor 
_end:
    lda #RIA_OP_EXIT
    sta RIA_OP

.segment "RODATA"

message:
    .byte "Hello, QASM is here!", $0D, $0A, 0
