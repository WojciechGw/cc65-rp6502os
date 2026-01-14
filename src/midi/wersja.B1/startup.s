;
; startup.s - Kod startowy i handler przerwan dla MIDI-OUT
; Picocomputer 6502 z W65C02S
;

.import _main
.import _midi_timer_isr
.import __STACK_START__
.import __STACK_SIZE__

.export _irq_handler
.export __STARTUP__ : absolute = 1

; Adresy rejestrow VIA
VIA_BASE    = $D000
VIA_IFR     = VIA_BASE + $0D
VIA_T2CL    = VIA_BASE + $08

; Bity przerwan
IRQ_T2      = $20

.segment "STARTUP"

; ============================================
; Wektor RESET
; ============================================
reset:
    ; Wylacz przerwania
    sei
    
    ; Wyczysc tryb dziesietny
    cld
    
    ; Inicjalizuj stos
    ldx #$FF
    txs
    
    ; Inicjalizuj stos C
    lda #<(__STACK_START__ + __STACK_SIZE__)
    sta sp
    lda #>(__STACK_START__ + __STACK_SIZE__)
    sta sp+1
    
    ; Wlacz przerwania
    cli
    
    ; Skocz do main
    jmp _main

; ============================================
; Handler przerwania IRQ
; ============================================
.segment "CODE"

_irq_handler:
    ; Zachowaj rejestry
    pha
    phx
    phy
    
    ; Sprawdz czy przerwanie od Timer 2
    lda VIA_IFR
    and #IRQ_T2
    beq @not_timer2
    
    ; Obsluz przerwanie Timer 2 - wywolaj funkcje C
    jsr _midi_timer_isr
    jmp @irq_done
    
@not_timer2:
    ; Inne przerwanie - wyczysc wszystkie flagi
    lda VIA_IFR
    sta VIA_IFR

@irq_done:
    ; Przywroc rejestry
    ply
    plx
    pla
    rti

; ============================================
; Handler NMI (nieuzywany)
; ============================================
nmi_handler:
    rti

; ============================================
; Zmienna stosu C
; ============================================
.segment "ZEROPAGE"
sp:     .res 2

; ============================================
; Wektory przerwan
; ============================================
.segment "VECTORS"
    .word nmi_handler      ; $FFFA - NMI
    .word reset            ; $FFFC - RESET  
    .word _irq_handler     ; $FFFE - IRQ/BRK
