; Hello World for Handy ASSembler
; ------------------------------
.org $A000

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
end:STP

text:
.ascii  "Hello World. "
.ascii  "I'm back !"
.asciiz "\r\n"

; for OS shell .exe
shellrun:
.word  start
; -- end --

