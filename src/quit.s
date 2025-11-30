.export _quit

.include "rp6502.inc"
.segment "CODE"

; Halts the 6502 by pulling RESB low go to RP6502 monitor 
_quit:
    lda #RIA_OP_EXIT
    sta RIA_OP

.segment "RODATA"
