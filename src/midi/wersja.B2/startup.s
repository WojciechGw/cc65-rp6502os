;
; startup.s - Kod startowy i handler przerwan dla MIDI-OUT
; Picocomputer 6502 z W65C02S @ 8MHz
; VIA W65C22S @ $FFD0
; 64KB RAM
;

.import _main
.import __STACK_START__
.import __STACK_SIZE__
.import __DATA_LOAD__
.import __DATA_RUN__
.import __DATA_SIZE__
.import __BSS_RUN__
.import __BSS_SIZE__

; Eksportowane symbole
.export _irq_handler
.export _midi_init
.export _midi_start_transmission
.export _midi_stop_transmission
.export __STARTUP__ : absolute = 1

; Eksportowane zmienne dla kodu C
.export _midi_head
.export _midi_tail
.export _midi_tx_state
.export _midi_buffer

; ============================================
; Stale
; ============================================

; Adresy rejestrow VIA @ $FFD0
VIA_BASE    = $FFD0
VIA_PORTB   = VIA_BASE + $00
VIA_PORTA   = VIA_BASE + $01
VIA_DDRB    = VIA_BASE + $02
VIA_DDRA    = VIA_BASE + $03
VIA_T1CL    = VIA_BASE + $04
VIA_T1CH    = VIA_BASE + $05
VIA_T1LL    = VIA_BASE + $06
VIA_T1LH    = VIA_BASE + $07
VIA_T2CL    = VIA_BASE + $08
VIA_T2CH    = VIA_BASE + $09
VIA_SR      = VIA_BASE + $0A
VIA_ACR     = VIA_BASE + $0B
VIA_PCR     = VIA_BASE + $0C
VIA_IFR     = VIA_BASE + $0D
VIA_IER     = VIA_BASE + $0E
VIA_PORTA_NH = VIA_BASE + $0F

; Bity przerwan i kontrolne
IRQ_T2          = $20
IRQ_ENABLE      = $80

; Bit MIDI TX na porcie B
MIDI_TX_BIT     = $01

; Stany transmisji
TX_STATE_IDLE   = 0
TX_STATE_START  = 1
TX_STATE_DATA   = 2
TX_STATE_STOP   = 3

; Timer dla 31250 baud przy 8 MHz
; 8000000 / 31250 = 256 cykli na bit
; Wartosc timera = 256 - 2 = 254 (kompensacja opoznienia przerwania)
MIDI_BIT_TIMER_L = 254
MIDI_BIT_TIMER_H = 0

; Rozmiar bufora (musi byc potega 2)
MIDI_BUFFER_SIZE = 64
MIDI_BUFFER_MASK = MIDI_BUFFER_SIZE - 1

; ============================================
; Segment ZEROPAGE - szybki dostep
; ============================================
.segment "ZEROPAGE"

sp:                     .res 2      ; Wskaznik stosu C

; Zmienne MIDI w zero page dla szybkosci
_midi_head:             .res 1      ; Indeks zapisu do bufora
_midi_tail:             .res 1      ; Indeks odczytu z bufora
_midi_tx_state:         .res 1      ; Stan maszyny TX
midi_current_byte:      .res 1      ; Aktualnie wysylany bajt
midi_bit_count:         .res 1      ; Licznik wysylanych bitow

; Zmienne tymczasowe
tmp1:                   .res 1
ptr1:                   .res 2

; ============================================
; Segment BSS - bufor MIDI
; ============================================
.segment "BSS"

_midi_buffer:           .res MIDI_BUFFER_SIZE

; ============================================
; Segment STARTUP - kod inicjalizacji
; ============================================
.segment "STARTUP"

reset:
    ; Wylacz przerwania
    sei
    
    ; Wyczysc tryb dziesietny
    cld
    
    ; Inicjalizuj stos sprzetowy
    ldx #$FF
    txs
    
    ; Kopiuj dane zainicjalizowane (jesli potrzebne)
    ; W systemie RAM-only moze nie byc potrzebne
    lda #<__DATA_SIZE__
    ora #>__DATA_SIZE__
    beq @skip_data_copy
    
    lda #<__DATA_LOAD__
    sta ptr1
    lda #>__DATA_LOAD__
    sta ptr1+1
    
    ldx #<__DATA_SIZE__
    ldy #>__DATA_SIZE__
    
@copy_data:
    ; Prosta petla kopiujaca
    ; (uproszczona - zakladamy male rozmiary)
    
@skip_data_copy:
    ; Zeruj segment BSS
    lda #<__BSS_SIZE__
    ora #>__BSS_SIZE__
    beq @skip_bss_clear
    
    lda #0
    ldx #<__BSS_SIZE__
@clear_bss:
    dex
    sta __BSS_RUN__, x
    bne @clear_bss
    
@skip_bss_clear:
    ; Inicjalizuj stos C
    lda #<(__STACK_START__ + __STACK_SIZE__)
    sta sp
    lda #>(__STACK_START__ + __STACK_SIZE__)
    sta sp+1
    
    ; Wlacz przerwania
    cli
    
    ; Skocz do main
    jsr _main
    
    ; Jesli main powroci, zatrzymaj procesor
@halt:
    wai                     ; Czekaj na przerwanie (65C02)
    jmp @halt

; ============================================
; Segment CODE - kod programu
; ============================================
.segment "CODE"

; ============================================
; _midi_init - Inicjalizacja systemu MIDI
; Wywolanie z C: void midi_init(void);
; ============================================
.proc _midi_init
    ; Wylacz przerwania na czas konfiguracji
    sei
    
    ; Wyzeruj zmienne
    stz _midi_head          ; STZ - instrukcja 65C02
    stz _midi_tail
    stz _midi_tx_state
    stz midi_current_byte
    stz midi_bit_count
    
    ; Konfiguracja portu B bit 0 jako wyjscie
    lda VIA_DDRB
    ora #MIDI_TX_BIT
    sta VIA_DDRB
    
    ; Ustaw linie TX w stan wysoki (IDLE)
    lda VIA_PORTB
    ora #MIDI_TX_BIT
    sta VIA_PORTB
    
    ; Timer 2 w trybie one-shot (ACR bit 5 = 0)
    lda VIA_ACR
    and #<~$20
    sta VIA_ACR
    
    ; Wylacz przerwanie Timer 2
    lda #IRQ_T2
    sta VIA_IER
    
    ; Wyczysc flage przerwania Timer 2
    lda VIA_T2CL
    
    ; Wlacz przerwania
    cli
    rts
.endproc

; ============================================
; _midi_start_transmission - Rozpocznij transmisje
; Wywolanie z C: void midi_start_transmission(void);
; ============================================
.proc _midi_start_transmission
    ; Sprawdz czy sa dane w buforze
    lda _midi_head
    cmp _midi_tail
    beq @no_data
    
    ; Pobierz bajt z bufora
    ldx _midi_tail
    lda _midi_buffer, x
    sta midi_current_byte
    
    ; Zwieksz tail z maska
    inx
    txa
    and #MIDI_BUFFER_MASK
    sta _midi_tail
    
    ; Ustaw stan START
    lda #TX_STATE_START
    sta _midi_tx_state
    
    ; Wyzeruj licznik bitow
    stz midi_bit_count
    
    ; Wlacz przerwanie Timer 2
    lda #(IRQ_ENABLE | IRQ_T2)
    sta VIA_IER
    
    ; Uruchom timer - zapis do T2CH startuje odliczanie
    lda #MIDI_BIT_TIMER_L
    sta VIA_T2CL
    lda #MIDI_BIT_TIMER_H
    sta VIA_T2CH
    
    rts
    
@no_data:
    ; Brak danych - ustaw stan IDLE
    lda #TX_STATE_IDLE
    sta _midi_tx_state
    rts
.endproc

; ============================================
; _midi_stop_transmission - Zatrzymaj transmisje
; Wywolanie z C: void midi_stop_transmission(void);
; ============================================
.proc _midi_stop_transmission
    ; Wylacz przerwanie Timer 2
    lda #IRQ_T2
    sta VIA_IER
    
    ; Wyczysc flage przerwania
    lda VIA_T2CL
    
    ; Ustaw linie TX w stan wysoki
    lda VIA_PORTB
    ora #MIDI_TX_BIT
    sta VIA_PORTB
    
    ; Ustaw stan IDLE
    lda #TX_STATE_IDLE
    sta _midi_tx_state
    
    rts
.endproc

; ============================================
; Handler przerwania IRQ
; Zoptymalizowany dla minimalnego czasu wykonania
; @ 8MHz mamy wiecej czasu, ale precyzja nadal wazna
; ============================================
.proc _irq_handler
    ; Zachowaj rejestry (PHX, PHY - instrukcje 65C02)
    pha
    phx
    
    ; Sprawdz zrodlo przerwania - Timer 2
    lda VIA_IFR
    and #IRQ_T2
    beq @not_timer2
    
    ; === Obsluga przerwania Timer 2 ===
    ; Wyczysc flage przerwania przez odczyt T2CL
    ldx VIA_T2CL
    
    ; Sprawdz stan maszyny transmisji
    lda _midi_tx_state
    
    ; Rozgalezienie wedlug stanu
    cmp #TX_STATE_START
    beq @state_start
    cmp #TX_STATE_DATA
    beq @state_data
    cmp #TX_STATE_STOP
    beq @state_stop
    
    ; Stan nieznany lub IDLE - zatrzymaj
    bra @stop_and_exit      ; BRA - instrukcja 65C02

; --- Stan START: Wyslij bit startu (LOW) ---
@state_start:
    ; Bit startu = LOW
    lda VIA_PORTB
    and #<~MIDI_TX_BIT
    sta VIA_PORTB
    
    ; Przejdz do stanu DATA
    lda #TX_STATE_DATA
    sta _midi_tx_state
    
    ; Wyzeruj licznik bitow
    stz midi_bit_count
    
    bra @restart_timer

; --- Stan DATA: Wyslij bit danych (LSB first) ---
@state_data:
    ; Przesun bajt w prawo, bit 0 -> Carry
    lsr midi_current_byte
    
    ; Ustaw lub wyczysc bit TX zalenie od Carry
    lda VIA_PORTB
    bcs @bit_high
    
    ; Bit = 0, wyczysc
    and #<~MIDI_TX_BIT
    bra @store_bit
    
@bit_high:
    ; Bit = 1, ustaw
    ora #MIDI_TX_BIT
    
@store_bit:
    sta VIA_PORTB
    
    ; Zwieksz licznik bitow
    inc midi_bit_count
    lda midi_bit_count
    cmp #8
    bne @restart_timer
    
    ; 8 bitow wyslanych - przejdz do STOP
    lda #TX_STATE_STOP
    sta _midi_tx_state
    bra @restart_timer

; --- Stan STOP: Wyslij bit stopu (HIGH) ---
@state_stop:
    ; Bit stopu = HIGH
    lda VIA_PORTB
    ora #MIDI_TX_BIT
    sta VIA_PORTB
    
    ; Sprawdz czy sa nastepne dane
    lda _midi_head
    cmp _midi_tail
    beq @transmission_done
    
    ; === Pobierz nastepny bajt ===
    ldx _midi_tail
    lda _midi_buffer, x
    sta midi_current_byte
    
    ; Zwieksz tail z maska
    inx
    txa
    and #MIDI_BUFFER_MASK
    sta _midi_tail
    
    ; Wracamy do stanu START
    lda #TX_STATE_START
    sta _midi_tx_state
    
    ; Wyzeruj licznik
    stz midi_bit_count
    
    bra @restart_timer

@transmission_done:
    ; Bufor pusty - koniec transmisji
    lda #TX_STATE_IDLE
    sta _midi_tx_state
    
    ; Wylacz przerwanie Timer 2
    lda #IRQ_T2
    sta VIA_IER
    
    bra @irq_done

@stop_and_exit:
    ; Zatrzymaj transmisje przy bledzie
    lda #IRQ_T2
    sta VIA_IER
    
    lda VIA_PORTB
    ora #MIDI_TX_BIT
    sta VIA_PORTB
    
    lda #TX_STATE_IDLE
    sta _midi_tx_state
    
    bra @irq_done

@restart_timer:
    ; Uruchom timer dla nastepnego bitu
    lda #MIDI_BIT_TIMER_L
    sta VIA_T2CL
    lda #MIDI_BIT_TIMER_H
    sta VIA_T2CH
    ; Przejdz do irq_done

@irq_done:
    ; Przywroc rejestry
    plx
    pla
    rti

@not_timer2:
    ; Inne zrodlo przerwania - wyczysc wszystkie flagi
    lda VIA_IFR
    sta VIA_IFR
    bra @irq_done
    
.endproc

; ============================================
; Handler NMI (nieuzywany, ale wymagany)
; ============================================
.proc nmi_handler
    rti
.endproc

; ============================================
; Wektory przerwan - umieszczone w RAM
; Musza byc skopiowane pod $FFFA-$FFFF
; lub ustawione przez bootloader
; ============================================
.segment "VECTORS"
    .word nmi_handler      ; $FFFA - NMI
    .word reset            ; $FFFC - RESET  
    .word _irq_handler     ; $FFFE - IRQ/BRK
