; Hello World for Mini ASSembler
.org $C000

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
.ascii "Hello "
.ascii "World."
.ascii " I'm b"
.asciiz "ack!\r\n"

runaddress:
.word  start
