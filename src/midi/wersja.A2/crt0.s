;============================================================
; crt0.s - Startup code dla Picocomputer 6502
; System: 64kB RAM, brak ROM
; VIA @ $FFD0
;
; Kompilacja: ca65 crt0.s -o crt0.o
;============================================================

.setcpu "65C02"

; Import symboli
.import _main
.import _irq_handler
.import __BSS_RUN__, __BSS_SIZE__

; Export symboli
.export __STARTUP__ : absolute = 1
.export _init, _exit

;------------------------------------------------------------
; Strona zerowa - wskaźniki pomocnicze
;------------------------------------------------------------
.segment "ZEROPAGE"

ptr1:   .res 2

;------------------------------------------------------------
; Segment startowy
;------------------------------------------------------------
.segment "STARTUP"

_init:
        ; Wyłącz przerwania
        sei
        
        ; Wyłącz tryb dziesiętny
        cld
        
        ; Inicjalizuj stos ($01FF w dół)
        ldx #$FF
        txs

        ;----------------------------------------------------
        ; Wyzeruj segment BSS
        ; (w systemie RAM-only nie trzeba kopiować DATA)
        ;----------------------------------------------------
        lda #<__BSS_SIZE__
        ora #>__BSS_SIZE__
        beq @skip_bss           ; Rozmiar = 0, pomiń
        
        lda #<__BSS_RUN__
        sta ptr1
        lda #>__BSS_RUN__
        sta ptr1+1
        
        lda #0
        tay
        ldx #>__BSS_SIZE__
        beq @bss_frac
        
@bss_page:
        sta (ptr1),y
        iny
        bne @bss_page
        inc ptr1+1
        dex
        bne @bss_page
        
@bss_frac:
        ldx #<__BSS_SIZE__
        beq @skip_bss
        
@bss_byte:
        sta (ptr1),y
        iny
        dex
        bne @bss_byte
        
@skip_bss:

        ;----------------------------------------------------
        ; Ustaw wektory przerwań w RAM
        ; (muszą być zapisane przed włączeniem przerwań)
        ;----------------------------------------------------
        lda #<nmi_handler
        sta $FFFA
        lda #>nmi_handler
        sta $FFFB
        
        lda #<_init
        sta $FFFC
        lda #>_init
        sta $FFFD
        
        lda #<_irq_handler
        sta $FFFE
        lda #>_irq_handler
        sta $FFFF

        ;----------------------------------------------------
        ; Wywołaj main()
        ;----------------------------------------------------
        jsr _main
        
_exit:
        ; Wyłącz przerwania i zatrzymaj procesor
        sei
@halt:
        wai                     ; 65C02: Wait for Interrupt
        bra @halt

;------------------------------------------------------------
; Handler NMI (pusty - tylko powrót)
;------------------------------------------------------------
nmi_handler:
        rti

;------------------------------------------------------------
; Koniec pliku
;------------------------------------------------------------
