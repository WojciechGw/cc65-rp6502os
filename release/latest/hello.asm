; Hello World
; for Handy ASSembler 65C02S
; --------------------------
.org $8000

RIA_TX .equ $FFE1

start:
    LDX #$00
loop:
    LDA text,X
    STA RIA_TX
    INX
    CMP #$00
    BNE loop
    RTS

text:
.ascii  "Hello World. "
.ascii  "I'm back!"
.asciiz "\r\n"

;for razemOS .exe
shellrun:
.word  start
; -- end of file --