;==================================
;MIDI OUT DRIVER 
;for W65C02+W65C22 (PHI2 8MHz)
;----------------------------------
;Transmits MIDI at 31250 baud 
;using Timer 1
;VIA base address: $FFD0
;Port A bit 0 used as TX line
;==================================

.org $E000

VIA     .equ $FFD0
TXPORT  .equ $FFD1 ;ORA
DDRPORT .equ $FFD3 ;DDRA
T1CL    .equ $FFD4
T1CH    .equ $FFD5
ACR     .equ $FFDB
IFR     .equ $FFDD
IER     .equ $FFDE

TXBYTE  .equ $CB ; byte to send
TXSTATE .equ $CC ; transmission bit 
                 ; counter :
                 ;   0 = start
                 ; 1-8 = data
                 ;   9 = stop
                 ;  10 = done

;==================================
;Initialize VIA and Timer 
;for MIDI speed
;==================================
InitVIA:
        LDA #%00000001    
                  ; PA0 as output
                  ; %00000001
        STA DDRPORT

        LDA #%01000000
                  ; T1 continuous
                  ; interrupt mode
                  ; %01000000 $40
        STA ACR

        LDA #<256 ; 256 cycles 
                  ; @8MHz=32us
        STA T1CL
        LDA #>256
        STA T1CH

        LDA #%11000000
                  ; Enable T1
                  ; interrupt
                  ; %11000000 $C0
        STA IER

        LDA #1    ; set idle state 
                  ; (high)
        STA TXPORT
        RTS

;==================================
; Send byte in A
; (starts transmission via IRQ)
;==================================
SendByte:
        STA TXBYTE
        LDA #0
        STA TXSTATE
        RTS

;==================================
; IRQ handler
; sends one bit per interrupt
;==================================
IRQ:
        PHA
        PHX
        PHY

        LDA IFR
        AND #%01000000
                    ; Timer 1
                    ; interrupt?
                    ; %01000000 $40
        BEQ NotT1

        LDY TXSTATE
        CPY #0
        BEQ StartBit
        CPY #9
        BEQ StopBit
        CPY #10
        BEQ DoneBit

       ; Send data bit Y (1..8)
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
       ; Transmission done,
       ; hold idle
        LDA #1
        STA TXPORT
        RTS
NotT1:
       ; handle other IRQs
       ; here if needed
DoneIRQ:
        PLY
        PLX
        PLA
        RTI

;==================================
; Usage example:
; LDA #$90 ; Note On
; JSR SendByte
; LDA #$3C ; Note C4
; JSR SendByte
; LDA #$7F ; Velocity 127
; JSR SendByte
