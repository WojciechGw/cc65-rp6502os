;=======================================
;MIDI OUT DRIVER 
;for W65C02+W65C22 (PHI2 8MHz)
;---------------------------------------
;Transmits MIDI at 31250 baud 
;using Timer 1
;VIA base address: $FFD0
;Port A bit 0 used as TX line
;=======================================

        .org $E000

VIA     .equ $FFD0
TXPORT  .equ VIA + $01    ;ORA
DDRPORT .equ VIA + $03    ;DDRA
T1CL    .equ VIA + $04
T1CH    .equ VIA + $05
ACR     .equ VIA + $0B
IFR     .equ VIA + $0D
IER     .equ VIA + $0E

TXBYTE  .equ $CB   ;byte to send
TXSTATE .equ $CC   ;transmission bit 
                   ;counter (0=start, 
                   ;1-8=data, 9=stop,
                   ;10=done)

;=======================================
;Initialize VIA and Timer 
;for MIDI speed
;=======================================
InitVIA:
        LDA #%00000001 ;PA0 as output
        STA DDRPORT

        LDA #%01000000 ;T1 continuous 
                       ;interrupt mode
        STA ACR

        LDA #<256      ;256 cycles 
                       ;@ 8 MHz = 32 us
        STA T1CL
        LDA #>256
        STA T1CH

        LDA #%11000000 ;Enable T1
                       ;interrupt
        STA IER

        LDA #1         ;set idle state 
                       ;(high)
        STA TXPORT
        RTS

;=======================================
;Send byte in A
;(starts transmission via IRQ)
;=======================================
SendByte:
        STA TXBYTE
        LDA #0
        STA TXSTATE
        RTS

;=======================================
;IRQ handler
;sends one bit per interrupt
;=======================================
IRQ:
        PHA
        PHX
        PHY

        LDA IFR
        AND #%01000000 ;Timer 1
                       ;interrupt?
        BEQ NotT1

        LDY TXSTATE
        CPY #0
        BEQ StartBit
        CPY #9
        BEQ StopBit
        CPY #10
        BEQ DoneBit

       ;Send data bit Y (1..8)
        LDA TXBYTE
BitLoop:
        LSR A
        DEY
        BNE BitLoop
        AND #$01
        STA TXPORT
        INC TXSTATE
        JMP DoneIRQ

StartBit:
        LDA #0
        STA TXPORT
        INC TXSTATE
        JMP DoneIRQ

StopBit:
        LDA #1
        STA TXPORT
        INC TXSTATE
        JMP DoneIRQ

DoneBit:
       ;Transmission done, hold idle
        LDA #1
        STA TXPORT
        RTS

NotT1:
       ;handle other IRQs here if needed

DoneIRQ:
        PLY
        PLX
        PLA
        RTI

;=======================================
;Usage Example:
;LDA #$90 ; Note On
;JSR SendByte
;LDA #$3C ; Note C4
;JSR SendByte
;LDA #$7F ; Velocity 127
;JSR SendByte
;=======================================
