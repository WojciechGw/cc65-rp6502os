;============================================================
; MIDI-OUT IRQ Handler dla Picocomputer 6502
; Procesor: W65C02S @ 8 MHz
; VIA: W65C22S @ $FFD0
; Pamięć: 64kB RAM (brak ROM)
;
; Kompilacja: ca65 midi_irq.s -o midi_irq.o
;============================================================

.setcpu "65C02"

;------------------------------------------------------------
; Adresy W65C22S VIA @ $FFD0
;------------------------------------------------------------
VIA_BASE    = $FFD0
VIA_ORB     = VIA_BASE + $00    ; $FFD0 - Port B Output
VIA_ORA     = VIA_BASE + $01    ; $FFD1 - Port A Output
VIA_DDRB    = VIA_BASE + $02    ; $FFD2 - Port B Direction
VIA_DDRA    = VIA_BASE + $03    ; $FFD3 - Port A Direction
VIA_T1CL    = VIA_BASE + $04    ; $FFD4 - Timer 1 Counter Low
VIA_T1CH    = VIA_BASE + $05    ; $FFD5 - Timer 1 Counter High
VIA_T1LL    = VIA_BASE + $06    ; $FFD6 - Timer 1 Latch Low
VIA_T1LH    = VIA_BASE + $07    ; $FFD7 - Timer 1 Latch High
VIA_T2CL    = VIA_BASE + $08    ; $FFD8 - Timer 2 Counter Low
VIA_T2CH    = VIA_BASE + $09    ; $FFD9 - Timer 2 Counter High
VIA_SR      = VIA_BASE + $0A    ; $FFDA - Shift Register
VIA_ACR     = VIA_BASE + $0B    ; $FFDB - Auxiliary Control
VIA_PCR     = VIA_BASE + $0C    ; $FFDC - Peripheral Control
VIA_IFR     = VIA_BASE + $0D    ; $FFDD - Interrupt Flag
VIA_IER     = VIA_BASE + $0E    ; $FFDE - Interrupt Enable

;------------------------------------------------------------
; Stałe konfiguracyjne
;------------------------------------------------------------
MIDI_PIN            = $01       ; Port B bit 0
MIDI_BUFFER_SIZE    = 64
MIDI_BUFFER_MASK    = 63        ; MIDI_BUFFER_SIZE - 1

; Timer dla 31250 baud @ 8 MHz:
; 8000000 / 31250 = 256 cykli na bit
; Wartość timera = 256 - 2 = 254 = $FE
TIMER_VALUE_LO      = $FE
TIMER_VALUE_HI      = $00

; Stany maszyny stanów transmisji
TX_STATE_IDLE       = 0
TX_STATE_START      = 1
TX_STATE_DATA       = 2
TX_STATE_STOP       = 3

; Flagi IFR/IER
VIA_T1_FLAG         = $40       ; Timer 1 interrupt flag (bit 6)
VIA_IER_SET         = $C0       ; Set interrupt enable (bit 7=1, bit 6=T1)
VIA_IER_CLEAR       = $40       ; Clear interrupt enable (bit 7=0, bit 6=T1)

;------------------------------------------------------------
; Import/Export symboli
;------------------------------------------------------------

; Eksportuj symbole do C
.export _midi_buffer
.export _midi_head
.export _midi_tail
.export _tx_state
.export _tx_byte
.export _tx_bit_count
.export _tx_active

; Eksportuj handler przerwania
.export _midi_irq_handler
.export _irq_handler

;------------------------------------------------------------
; Zmienne w stronie zerowej (szybki dostęp)
;------------------------------------------------------------
.segment "ZEROPAGE"

_tx_state:      .res 1          ; Stan maszyny stanów
_tx_byte:       .res 1          ; Aktualnie wysyłany bajt
_tx_bit_count:  .res 1          ; Licznik bitów (0-7)
_tx_active:     .res 1          ; Flaga aktywnej transmisji

;------------------------------------------------------------
; Zmienne w RAM
;------------------------------------------------------------
.segment "BSS"

_midi_buffer:   .res MIDI_BUFFER_SIZE   ; Bufor cykliczny MIDI
_midi_head:     .res 1                  ; Indeks zapisu
_midi_tail:     .res 1                  ; Indeks odczytu

;------------------------------------------------------------
; Kod obsługi przerwania
;------------------------------------------------------------
.segment "CODE"

;------------------------------------------------------------
; Główny handler IRQ
; Wejście przez wektor $FFFE
; Zachowuje wszystkie rejestry i wywołuje handler MIDI
;------------------------------------------------------------
_irq_handler:
        ; Zachowaj rejestry na stosie (65C02 ma PHX/PHY)
        pha                     ; 3 cykle - zachowaj A
        phx                     ; 3 cykle - zachowaj X
        phy                     ; 3 cykle - zachowaj Y
        
        ; Wywołaj handler MIDI
        jsr _midi_irq_handler   ; 6 cykli
        
        ; Przywróć rejestry
        ply                     ; 4 cykle - przywróć Y
        plx                     ; 4 cykle - przywróć X
        pla                     ; 4 cykle - przywróć A
        
        rti                     ; 6 cykli

;------------------------------------------------------------
; Handler przerwania MIDI - Software UART TX
; Protokół: 31250 baud, 8N1 (1 start, 8 data, 1 stop)
; Czas na bit: 256 cykli @ 8 MHz = 32 µs
;
; Wejście: Wywoływane z IRQ co 256 cykli
; Modyfikuje: A, X (Y zachowane)
; Czas wykonania: ~35-50 cykli (duży margines)
;------------------------------------------------------------
_midi_irq_handler:
        ; Sprawdź czy to przerwanie od Timer 1
        lda VIA_IFR             ; 4 cykle
        and #VIA_T1_FLAG        ; 2 cykle
        beq @not_our_irq        ; 2/3 cykle - nie nasze przerwanie
        
        ; Wyczyść flagę przerwania przez odczyt T1CL
        lda VIA_T1CL            ; 4 cykle - KONIECZNE!
        
        ; Skok według aktualnego stanu (jump table byłby szybszy
        ; ale dla 4 stanów branch jest OK)
        ldx _tx_state           ; 3 cykle
        
        cpx #TX_STATE_START     ; 2 cykle
        beq @state_start        ; 2/3 cykle
        
        cpx #TX_STATE_DATA      ; 2 cykle
        beq @state_data         ; 2/3 cykle
        
        cpx #TX_STATE_STOP      ; 2 cykle
        beq @state_stop         ; 2/3 cykle
        
        ; TX_STATE_IDLE lub nieznany - wyłącz timer
        bra @go_idle            ; 3 cykle

;------------------------------------------------------------
; Stan START - wyślij bit startowy (0 = space = low)
;------------------------------------------------------------
@state_start:
        ; Start bit = 0 (logiczne 0 = niski poziom)
        lda VIA_ORB             ; 4 cykle
        and #<~MIDI_PIN         ; 2 cykle - wyczyść bit 0
        sta VIA_ORB             ; 4 cykle
        
        ; Przejdź do stanu DATA
        lda #TX_STATE_DATA      ; 2 cykle
        sta _tx_state           ; 3 cykle
        
        ; Wyzeruj licznik bitów
        stz _tx_bit_count       ; 3 cykle (65C02)
        
        rts                     ; 6 cykli

;------------------------------------------------------------
; Stan DATA - wyślij kolejny bit danych (LSB first)
;------------------------------------------------------------
@state_data:
        ; Pobierz bajt i przesuń - bit 0 trafia do Carry
        lda _tx_byte            ; 3 cykle
        lsr a                   ; 2 cykle - bit 0 -> Carry
        sta _tx_byte            ; 3 cykle
        
        ; Rozgałęzienie według Carry
        bcc @send_zero          ; 2/3 cykle
        
        ; Wyślij 1 (mark = high)
@send_one:
        lda VIA_ORB             ; 4 cykle
        ora #MIDI_PIN           ; 2 cykle
        sta VIA_ORB             ; 4 cykle
        bra @inc_bit_count      ; 3 cykle
        
        ; Wyślij 0 (space = low)
@send_zero:
        lda VIA_ORB             ; 4 cykle
        and #<~MIDI_PIN         ; 2 cykle
        sta VIA_ORB             ; 4 cykle
        
@inc_bit_count:
        ; Zwiększ licznik bitów
        inc _tx_bit_count       ; 5 cykli
        lda _tx_bit_count       ; 3 cykle
        cmp #8                  ; 2 cykle - czy wysłano 8 bitów?
        bcc @irq_done           ; 2/3 cykle - nie, kontynuuj
        
        ; Wysłano 8 bitów - przejdź do stanu STOP
        lda #TX_STATE_STOP      ; 2 cykle
        sta _tx_state           ; 3 cykle
        
        rts                     ; 6 cykli

;------------------------------------------------------------
; Stan STOP - wyślij bit stopu (1 = mark = high)
;            i sprawdź czy są kolejne dane
;------------------------------------------------------------
@state_stop:
        ; Stop bit = 1 (mark = wysoki poziom)
        lda VIA_ORB             ; 4 cykle
        ora #MIDI_PIN           ; 2 cykle
        sta VIA_ORB             ; 4 cykle
        
        ; Sprawdź czy są kolejne dane w buforze
        lda _midi_head          ; 3 cykle
        cmp _midi_tail          ; 3 cykle
        beq @go_idle            ; 2/3 cykle - bufor pusty
        
        ; Jest następny bajt - pobierz go
        ldx _midi_tail          ; 3 cykle
        lda _midi_buffer,x      ; 4 cykle
        sta _tx_byte            ; 3 cykle
        
        ; Zwiększ tail z zawinięciem (modulo buffer size)
        inx                     ; 2 cykle
        txa                     ; 2 cykle
        and #MIDI_BUFFER_MASK   ; 2 cykle
        sta _midi_tail          ; 3 cykle
        
        ; Przygotuj następny bajt - stan START
        lda #TX_STATE_START     ; 2 cykle
        sta _tx_state           ; 3 cykle
        stz _tx_bit_count       ; 3 cykle
        
        rts                     ; 6 cykli

;------------------------------------------------------------
; Przejście do stanu IDLE - zatrzymaj transmisję
;------------------------------------------------------------
@go_idle:
        ; Ustaw stan IDLE
        lda #TX_STATE_IDLE      ; 2 cykle
        sta _tx_state           ; 3 cykle
        
        ; Wyczyść flagę aktywności
        stz _tx_active          ; 3 cykle
        
        ; Wyłącz przerwanie Timer 1 (bit 7=0 = clear)
        lda #VIA_IER_CLEAR      ; 2 cykle
        sta VIA_IER             ; 4 cykle
        
        rts                     ; 6 cykli

@irq_done:
@not_our_irq:
        rts                     ; 6 cykli

;------------------------------------------------------------
; Koniec pliku
;------------------------------------------------------------
