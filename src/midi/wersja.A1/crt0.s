; crt0.s - Startup code dla Picocomputer z obsługą IRQ
; Kompilacja: ca65 crt0.s -o crt0.o

.import _main
.import _irq_handler
.import __BSS_RUN__, __BSS_SIZE__
.import __DATA_LOAD__, __DATA_RUN__, __DATA_SIZE__

.export __STARTUP__ : absolute = 1
.export _init, _exit

.segment "STARTUP"

_init:
    ; Wyłącz przerwania
    sei
    
    ; Inicjalizuj stos
    ldx #$FF
    txs
    
    ; Wyczyść stronę zerową
    lda #$00
    ldx #$00
@clrzp:
    sta $00,x
    inx
    bne @clrzp
    
    ; Kopiuj dane z ROM do RAM
    lda #<__DATA_LOAD__
    sta $00
    lda #>__DATA_LOAD__
    sta $01
    lda #<__DATA_RUN__
    sta $02
    lda #>__DATA_RUN__
    sta $03
    ldx #>__DATA_SIZE__
    ldy #<__DATA_SIZE__
    beq @check_hi
@copy_loop:
    lda ($00),y
    sta ($02),y
    iny
    bne @copy_loop
    inc $01
    inc $03
    dex
@check_hi:
    cpy #$00
    bne @copy_loop
    
    ; Wyzeruj BSS
    lda #<__BSS_RUN__
    sta $00
    lda #>__BSS_RUN__
    sta $01
    lda #$00
    ldx #>__BSS_SIZE__
    ldy #<__BSS_SIZE__
    beq @check_bss_hi
@clear_bss:
    sta ($00),y
    iny
    bne @clear_bss
    inc $01
    dex
@check_bss_hi:
    cpy #$00
    bne @clear_bss
    
    ; Uruchom main
    jsr _main
    
_exit:
    ; Zatrzymaj w pętli
    jmp _exit

; Handler przerwań
.segment "LOWCODE"

irq:
    pha
    txa
    pha
    tya
    pha
    
    ; Wywołaj handler C
    jsr _irq_handler
    
    pla
    tay
    pla
    tax
    pla
    rti

nmi:
    rti

; Wektory procesora
.segment "VECTORS"

.word nmi       ; $FFFA - NMI
.word _init     ; $FFFC - RESET  
.word irq       ; $FFFE - IRQ/BRK
