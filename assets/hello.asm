; Hello World for Mini ASSembler
RIA_TX .equ $FFE1

    .org $C000
    
    LDX #$00

loop:
    LDA text,X
    STA RIA_TX
    INX
    CMP #$00
    BNE loop
    RTS

text:
.asciz "Hello, World I'm back!\r\n"

runaddress:
.word  $C000
