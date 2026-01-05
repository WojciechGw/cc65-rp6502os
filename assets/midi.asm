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

; W65C22 REGISTER DEFINITIONS
; (offsets from VIA base)
; $0 Out Register B / In Register B
; $1 Out Register A / In Register A
; $2 Data Direction Register B
; $3 Data Direction Register A
; $4 Timer 1 Counter Low
; $5 Timer 1 Counter High
; $6 Timer 1 Latch Low
; $7 Timer 1 Latch High
; $8 Timer 2 Counter Low
; $9 Timer 2 Counter High
; $A Shift Register
; $B Auxiliary Control Register
; $C Peripheral Control Register
; $D Interrupt Flag Register
; $E Interrupt Enable Register
; $F ORA no handshake 
;    (alias for ORA)

VIA_ORB   .equ $FFD0
VIA_ORA   .equ $FFD1
VIA_DDRB  .equ $FFD2
VIA_DDRA  .equ $FFD3
VIA_T1CL  .equ $FFD4
VIA_T1CH  .equ $FFD5
VIA_T1LL  .equ $FFD6
VIA_T1LH  .equ $FFD7
VIA_T2CL  .equ $FFD8
VIA_T2CH  .equ $FFD9
VIA_SR    .equ $FFDA
VIA_ACR   .equ $FFDB
VIA_PCR   .equ $FFDC
VIA_IFR   .equ $FFDD
VIA_IER   .equ $FFDE
VIA_ORANH .equ $FFDF

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
       
       ; 256 cycles @8MHz=32us 
        LDA #<256
        STA T1CL
        LDA #>256
        STA T1CH
        LDA #<256
        STA T1LL
        LDA #>256
        STA T1LH

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
        JMP DoneIRQ
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

;==================================
; IRQ vector
;==================================
;        .org $FFFE
;        .word IRQ
