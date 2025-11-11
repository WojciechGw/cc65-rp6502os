.export _run
.include "rp6502.inc"
.segment "CODE"
_run:
    ;save A,X,Y,PC
    sta $F0
    stx $F1
    sty $F2
    pla
    sta $F3 ;PC LSB
    pla
    sta $F4 ;PC MSB
    ; call program

    jsr $8000
    
@done:
    ;restore A,X,Y,PC
    lda $F0
    ldx $F1
    ldy $F2
    lda $F4 ;PC MSB
    pha
    lda $F3 ;PC LSB
    pha
    rts
