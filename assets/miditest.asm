; =================================
; MIDI Gama C-dur
; Tempo: 120 BPM
; =================================

.org $A000
InitVIA  .equ $E000
SendByte .equ $E01F
IRQ      .equ $E026

Init:
    LDA #<IRQ
    STA $FFFE
    LDA #>IRQ
    STA $FFFF
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
    RTS

NOTE: .byte 0
Notes:
    .byte $3C, $3E, $40, $41
    .byte $43, $45, $47, $48

; =======================
; delay ~500ms @ 8 MHz
; for tests only
; =======================
Delay500ms:
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
    RTS
