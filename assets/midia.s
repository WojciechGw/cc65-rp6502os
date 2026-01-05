.export _main
.export _start
.export _end

.include "rp6502.inc"
.setcpu "65C02"

;==================================
;MIDI OUT DRIVER 
;for W65C02S+W65C22S (PHI2 8MHz)
;----------------------------------
;Transmits MIDI at 31250 baud 
;using Timer 1
;VIA base address: $FFD0
;Port A bit 0 used as TX line
;==================================
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
;VIA       = $FFD0
;VIA_PRB   = $FFD0
;VIA_PRA   = $FFD1
;VIA_DDRB  = $FFD2
;VIA_DDRA  = $FFD3
;VIA_T1CL  = $FFD4
;VIA_T1CH  = $FFD5
;VIA_T1LL  = $FFD6
;VIA_T1LH  = $FFD7
;VIA_T2CL  = $FFD8
;VIA_T2CH  = $FFD9
;VIA_SR    = $FFDA
VIA_ACR    = VIA_CR
;VIA_PCR   = $FFDC
;VIA_IFR   = $FFDD
;VIA_IER   = $FFDE
;VIA_PRA2 = $FFDF

TXBYTE  = $CB ; byte to send
TXSTATE = $CC ; transmission bit 
                 ; counter :
                 ;   0 = start
                 ; 1-8 = data
                 ;   9 = stop
                 ;  10 = done

VECIRQ_L = $FFFE
VECIRQ_H = $FFFF

;RIA_ADDR0 = $FFE6
;RIA_STEP0 = $FFE5
;RIA_ADDR1 = $FFEA
;RIA_STEP1 = $FFE9

; $0100
; 256 cycles @ 8MHz 32us/cycle
; for MIDI standard 31250 baud
IRQCYCLES = $0100

.segment "CODE"

_main:
_start:
    LDA #<IRQhandler
    STA VECIRQ_L
    LDA #>IRQhandler
    STA VECIRQ_H
    JSR InitVIA

PlayScale:
    LDY #0
NextNote:
    LDA Notes,Y
    STA NOTE
    LDA #$90        ; Note On, #1
    JSR SendByte
    LDA NOTE
    JSR SendByte
    LDA #$7F        ; velocity 127
    JSR SendByte

    JSR Delay500ms

    LDA #$80        ; Note Off, #1
    JSR SendByte
    LDA NOTE
    JSR SendByte
    LDA #$00        ; velocity 0
    JSR SendByte

    JSR Delay500ms

    INY
    CPY #8
    BNE NextNote
    ; JMP PlayScale

; Halts the 6502 by pulling RESB low go to RP6502 monitor 
_end:
    lda #RIA_OP_EXIT
    sta RIA_OP

; =======================
; delay ~500ms @ 8 MHz
; for tests only
; =======================
Delay500ms:
    PHA
    PHX
    PHY

    LDY #6
Outer:
    LDX #0
Loop1:
    LDA #0
Loop2:
    NOP
    NOP
    NOP
    NOP
    NOP
    SEC
    SBC #1
    BNE Loop2
    INX
    CPX #$FA
    BNE Loop1
    DEY
    BNE Outer

    PLY
    PLX
    PLA
    RTS

;==================================
;Initialize VIA and Timer 
;for MIDI speed
;==================================
InitVIA:
        LDA #%00000001    
                  ; PA0 as output
                  ; %00000001
        STA VIA_DDRA

        LDA #%01000000
                  ; T1 continuous
                  ; interrupt mode
                  ; %01000000 $40
        STA VIA_ACR
       
        LDA #<IRQCYCLES
        STA VIA_T1LL
        LDA #>IRQCYCLES
        STA VIA_T1LH
        LDA #<IRQCYCLES
        STA VIA_T1CL
        LDA #>IRQCYCLES
        STA VIA_T1CH

        LDA #%11000000
                  ; Enable T1
                  ; interrupt
                  ; %11000000 $C0
        STA VIA_IER

        LDA #1    ; set idle state 
                  ; (high)
        STA VIA_PA1

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
IRQhandler:
        PHA
        PHX
        PHY

        LDA <RIA_ADDR0
        PHA
        LDA >RIA_ADDR0
        PHA
        LDA RIA_STEP0
        PHA

        LDA VIA_IFR
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
        STA VIA_PA1
        INC TXSTATE
        JMP DoneIRQ
StartBit:
        LDA #0
        STA VIA_PA1
        INC TXSTATE
        JMP DoneIRQ
StopBit:
        LDA #1
        STA VIA_PA1
        INC TXSTATE
        JMP DoneIRQ
DoneBit:
       ; Transmission done,
       ; hold idle
        LDA #1
        STA VIA_PA1
NotT1:
       ; handle other IRQs
       ; here if needed
DoneIRQ:
        LDA VIA_T1CL

        PLA
        STA RIA_STEP0
        PLA
        STA >RIA_ADDR0
        PLA
        STA <RIA_ADDR0

        PLY
        PLX
        PLA
        RTI

.segment "RODATA"

NOTE: .byte 0
Notes:
    .byte $3C, $3E, $40, $41
    .byte $43, $45, $47, $48

dosrun:
    .word _start
