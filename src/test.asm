; 65C02/WDC tests for MiniASSembler

        .org $C000

; immediate/zero-page/absolute/indexed
        LDA #$10
        LDX #$00
        LDY #$FF

        LDA $00
        LDA $10,X
        LDA $20,Y   ;unattended mode
        LDA $1234
        LDA $1234,X
        LDA $1234,Y

        STA $40
        STA $50,X
        STA $60,Y   ;unattended mode
        STA $2000
        STA $2000,X
        STA $2000,Y

        LDX $80
        LDX $90,Y
        LDX $3000
        LDX $3000,Y

        LDY $A0
        LDY $B0,X
        LDY $4000
        LDY $4000,X

; indirect (ZP,X) / (ZP),Y / (ZP)
        LDA ($20,X)
        LDA ($22),Y
        LDA ($24)       ; ZP indirect
        STA ($26,X)
        STA ($28),Y
        STA ($2A)

; absolute indirect 
; and absolute,X for JMP
        JMP ($3000)
        JMP ($3000,X)
        JSR $3100

; A, accumulator vs IMP
        ASL A
        LSR A
        ROL A
        ROR A
        ASL $00
        LSR $01
        ROL $0200
        ROR $0201

; logic / arythmetics
        AND #$F0
        ORA $33
        EOR $44,X
        ADC #$01
        SBC $5000
        CMP $01
        CPX #$02
        CPY $02

; BIT with IMM/ZP/ABS/ABSX
        BIT #$80
        BIT $10
        BIT $2000
        BIT $2000,X

; INC/DEC memory and registers
        INC $10
        INC $2000,X
        INX
        INY
        DEC $11
        DEC $2001,X
        DEX
        DEY

; shifts on ZP/ZPX/ABS/ABSX
        ASL $12
        ASL $2010,X
        LSR $13
        LSR $2011,X

; flags / stack / returns
        CLC
        SEC
        CLI
        SEI
        CLD
        SED
        CLV

        PHA
        PHP
        PHX
        PHY
        PLA
        PLP
        PLX
        PLY

        BRK
        RTI
        RTS

; indirect jumps BBR/BBS (PCREL)
        BRA label1
        BNE label1
        BEQ label1
        BMI label1
        BPL label1
        BCC label1
        BCS label1
        BVC label1
        BVS label1

        BBR0 $10,label2 ;out of range
        BBS1 $10,label2 ;out of range

; TRB/TSB/STZ
        TRB $30
        TSB $31
        STZ $32
        STZ $4000,X

; SMB/RMB
        SMB0 $20
        RMB3 $21

; WAI/STP (IMPLIED)
        WAI
        STP

label1:
        NOP
label2:
        NOP

; short strings ASCII i ASCIZ
        .ascii "HELLO"
        .asciz "WORLD"
