;
; ria_api.s — Picocomputer RP6502 OS wrapper dla natywnego asemblera WDC65C02S
;
; Konwencja wywołania (jeśli nie zaznaczono inaczej):
;   Wejście : A = low byte, X = high byte (16-bit), lub opisane per-funkcja
;   Wyjście : A = low byte, X = high byte wyniku (int, 16-bit)
;             N=1 lub A/X = $FFFF oznacza błąd (errno w RIA_ERRNO/$FFED)
;   Rejestry: A, X mogą być zmienione; Y zachowany
;   ZP      : api_zp0..api_zp7 — 8 bajtów przydzielanych przez linker (segment ZEROPAGE)
;
; Użycie (tryb include):
;   .include "ria_api.s"
;
; Użycie (tryb biblioteka, kompiluj osobno + zlinkuj):
;   ca65 -t rp6502 ria_api.s -o ria_api.o
;   ld65 ... ria_api.o ...
;   W pliku wywołującym: .import uart_putc, ria_open, ...
;
; Wymagany assembler: ca65 (cc65)
;

.ifndef RIA_API_INCLUDED
RIA_API_INCLUDED = 1

; ---------------------------------------------------------------------------
; Adresy rejestrów sprzętowych RIA  ($FFE0–$FFF9)
; ---------------------------------------------------------------------------

RIA_READY   = $FFE0   ; bit7=TX gotowy, bit6=RX gotowy
RIA_TX      = $FFE1   ; rejestr nadawczy UART (zapis)
RIA_RX      = $FFE2   ; rejestr odbiorczy UART (odczyt)
RIA_VSYNC   = $FFE3   ; licznik VSync (inkrementowany co ramkę)
RIA_RW0     = $FFE4   ; portal XRAM 0 — odczyt/zapis
RIA_STEP0   = $FFE5   ; portal XRAM 0 — krok auto-inkrementacji
RIA_ADDR0   = $FFE6   ; portal XRAM 0 — adres (16-bit, lo=$FFE6 hi=$FFE7)
RIA_RW1     = $FFE8   ; portal XRAM 1 — odczyt/zapis
RIA_STEP1   = $FFE9   ; portal XRAM 1 — krok
RIA_ADDR1   = $FFEA   ; portal XRAM 1 — adres (lo=$FFEA hi=$FFEB)
RIA_XSTACK  = $FFEC   ; stos parametrów OS (push: zapis; pop: odczyt)
RIA_ERRNO   = $FFED   ; kod błędu po wywołaniu OS (16-bit: lo=$FFED hi=$FFEE)
RIA_OP      = $FFEF   ; uruchom operację OS (zapis opcode → start)
RIA_IRQ     = $FFF0   ; wektor IRQ (zapis adresu handlera)
RIA_SPIN    = $FFF1   ; czekaj na zakończenie OS (JSR do $FFF1)
RIA_BUSY    = $FFF2   ; bit7=1 jeśli OS zajęty
RIA_LDA     = $FFF3   ; (opcode LDA abs)
RIA_A       = $FFF4   ; rejestr A fastcall (zapis przed RIA_OP)
RIA_LDX     = $FFF5   ; (opcode LDX abs)
RIA_X       = $FFF6   ; rejestr X fastcall
RIA_RTS     = $FFF7   ; (opcode RTS)
RIA_SREG    = $FFF8   ; rejestr rozszerzony wynik (lo=$FFF8 hi=$FFF9)

; ---------------------------------------------------------------------------
; Adresy rejestrów VIA 6522  ($FFD0–$FFDF)
; ---------------------------------------------------------------------------

VIA_BASE    = $FFD0
VIA_PRB     = VIA_BASE+$0   ; Port B
VIA_PRA     = VIA_BASE+$1   ; Port A
VIA_DDRB    = VIA_BASE+$2   ; Kierunek portu B
VIA_DDRA    = VIA_BASE+$3   ; Kierunek portu A
VIA_T1CL    = VIA_BASE+$4   ; Timer 1 — low byte
VIA_T1CH    = VIA_BASE+$5   ; Timer 1 — high byte
VIA_T1LL    = VIA_BASE+$6   ; Timer 1 latch low
VIA_T1LH    = VIA_BASE+$7   ; Timer 1 latch high
VIA_T2CL    = VIA_BASE+$8   ; Timer 2 — low byte
VIA_T2CH    = VIA_BASE+$9   ; Timer 2 — high byte
VIA_SR      = VIA_BASE+$A   ; Shift register
VIA_ACR     = VIA_BASE+$B   ; Auxiliary control register
VIA_PCR     = VIA_BASE+$C   ; Peripheral control register
VIA_IFR     = VIA_BASE+$D   ; Interrupt flag register
VIA_IER     = VIA_BASE+$E   ; Interrupt enable register
VIA_PANH    = VIA_BASE+$F   ; Port A bez handshake

; ---------------------------------------------------------------------------
; Opcody operacji OS
; ---------------------------------------------------------------------------

OP_EXIT         = $FF   ; zakończ program (A=kod wyjścia)
OP_ZXSTACK      = $00   ; wyczyść XSTACK
OP_XREG         = $01   ; ustaw rejestr urządzenia XRAM
OP_ARGV         = $08   ; pobierz dane argc/argv z kernela
OP_EXEC         = $09   ; uruchom ROM (dane przez XSTACK)
OP_ATTR_GET     = $0A   ; pobierz atrybut systemu (id w A)
OP_ATTR_SET     = $0B   ; ustaw atrybut systemu (wartość w SREG+A/X, id przez XSTACK)
OP_TZSET        = $0D   ; ustaw strefę czasową (dane przez XSTACK)
OP_TZQUERY      = $0E   ; pobierz informacje o strefie czasowej
OP_CLOCK        = $0F   ; pobierz tiki zegarowe (wynik 32-bit w SREG+A/X)
OP_CLOCK_GETRES = $10   ; rozdzielczość zegara
OP_CLOCK_GETTIME= $11   ; czas bieżący (timespec przez XSTACK)
OP_CLOCK_SETTIME= $12   ; ustaw czas (timespec przez XSTACK)
OP_OPEN         = $14   ; otwórz plik (nazwa przez XSTACK, flagi w A/X)
OP_CLOSE        = $15   ; zamknij deskryptor (fd w A/X)
OP_READ_XSTACK  = $16   ; czytaj plik → XSTACK (count w A/X, fd przez XSTACK)
OP_READ_XRAM    = $17   ; czytaj plik → XRAM   (addr+count+fd przez XSTACK)
OP_WRITE_XSTACK = $18   ; pisz plik ← XSTACK  (dane+count+fd przez XSTACK)
OP_WRITE_XRAM   = $19   ; pisz plik ← XRAM    (addr+count+fd przez XSTACK)
OP_LSEEK        = $1A   ; przesuń pozycję pliku (offset 32-bit+whence+fd przez XSTACK)
OP_UNLINK       = $1B   ; usuń plik (nazwa przez XSTACK)
OP_RENAME       = $1C   ; zmień nazwę (stara+nowa nazwa przez XSTACK)
OP_SYNCFS       = $1E   ; synchronizuj system plików
OP_STAT         = $1F   ; informacje o pliku (nazwa przez XSTACK, wynik przez XSTACK)
OP_OPENDIR      = $20   ; otwórz katalog (nazwa przez XSTACK)
OP_READDIR      = $21   ; czytaj wpis katalogu (dirdes w A/X, wynik przez XSTACK)
OP_CLOSEDIR     = $22   ; zamknij katalog (dirdes w A/X)
OP_TELLDIR      = $23   ; pozycja w katalogu (dirdes w A/X)
OP_SEEKDIR      = $24   ; przeskocz do pozycji (offset 32-bit+dirdes przez XSTACK)
OP_REWINDDIR    = $25   ; przewiń katalog (dirdes w A/X)
OP_CHMOD        = $26   ; zmień atrybuty pliku
OP_MKDIR        = $28   ; utwórz katalog (nazwa przez XSTACK)
OP_CHDIR        = $29   ; zmień aktualny katalog (nazwa przez XSTACK)
OP_CHDRIVE      = $2A   ; zmień aktywny dysk (nazwa przez XSTACK)
OP_GETCWD       = $2B   ; pobierz aktualny katalog (size w A/X, wynik przez XSTACK)
OP_SETLABEL     = $2C   ; ustaw etykietę woluminu (nazwa przez XSTACK)
OP_GETLABEL     = $2D   ; pobierz etykietę woluminu (ścieżka przez XSTACK)
OP_GETFREE      = $2E   ; wolne miejsce (ścieżka przez XSTACK)

; Identyfikatory atrybutów systemu (ATTR_GET / ATTR_SET)
ATTR_ERRNO_OPT  = $00
ATTR_PHI2_KHZ   = $01   ; częstotliwość CPU w kHz (tylko odczyt)
ATTR_CODE_PAGE  = $02   ; strona kodowa terminala
ATTR_RLN_LENGTH = $03   ; długość bufora readline
ATTR_LRAND      = $04   ; losowa liczba 32-bit
ATTR_BEL        = $05   ; dzwonek BELL (0=wyłączony)
ATTR_LAUNCHER   = $06   ; rejestracja jako launcher
ATTR_EXIT_CODE  = $07   ; kod wyjścia ostatniego procesu

; Flagi open() (OP_OPEN)
O_RDONLY        = $0000
O_WRONLY        = $0001
O_RDWR          = $0002
O_CREAT         = $0200
O_TRUNC         = $0400
O_APPEND        = $0008
O_EXCL          = $0800

; Flagi lseek() (OP_LSEEK)
SEEK_SET        = 0
SEEK_CUR        = 1
SEEK_END        = 2

; Atrybuty pliku FAT (f_stat, f_chmod)
AM_RDO          = $01   ; tylko do odczytu
AM_HID          = $02   ; ukryty
AM_SYS          = $04   ; systemowy
AM_VOL          = $08   ; etykieta woluminu
AM_DIR          = $10   ; katalog
AM_ARC          = $20   ; archiwum

; Bity gotowości UART
RIA_TX_READY    = $80   ; bit RIA_READY: TX FIFO ma miejsce
RIA_RX_READY    = $40   ; bit RIA_READY: bajt odebrany

; ---------------------------------------------------------------------------
; Segment ZEROPAGE — 8 bajtów przydzielanych przez linker
; ---------------------------------------------------------------------------

.segment "ZEROPAGE"
api_zp0: .res 1   ; tymczasowy ptr lo (adres łańcucha / bufora)
api_zp1: .res 1   ; tymczasowy ptr hi
api_zp2: .res 1   ; tymczasowy licznik / long byte0 (lo)
api_zp3: .res 1   ; tymczasowy long byte1
api_zp4: .res 1   ; tymczasowy long byte2
api_zp5: .res 1   ; tymczasowy long byte3 (hi)
api_zp6: .res 1   ; zachowany Y / ptr2 lo
api_zp7: .res 1   ; zachowany A / ptr2 hi

; ---------------------------------------------------------------------------
; Eksporty publiczne
; ---------------------------------------------------------------------------

.export uart_putc, uart_getc, uart_getc_nb, uart_puts, uart_putnl
.export uart_puthex, uart_putdec
.export xpush_byte, xpush_word, xpush_long, xpop_byte, xpop_word, xpop_long
.export xpush_str, xstack_clear
.export ria_set_ax, ria_set_long, ria_get_long
.export ria_call, ria_call_long
.export ria_exit, ria_attr_get, ria_attr_set
.export ria_open, ria_close, ria_read_raw, ria_read_buf, ria_write_buf
.export ria_lseek, ria_unlink, ria_rename
.export ria_opendir, ria_closedir, ria_rewinddir, ria_readdir
.export ria_mkdir, ria_chdir, ria_chdrive, ria_getcwd, ria_chmod, ria_syncfs
.export ria_setlabel, ria_getlabel
.export ria_clock, ria_clock_gettime
.export xram0_set_addr, xram0_set_step, xram0_write_byte, xram0_read_byte
.export xram0_write_buf, xram0_read_buf
.export xram1_set_addr, xram1_set_step
.export vsync_wait
.export via_timer1_set, via_timer1_wait
.export xreg_send16, xreg_keyboard_enable, xreg_keyboard_disable

; ---------------------------------------------------------------------------
; Makra pomocnicze (inline — nie generują wywołania JSR)
; ---------------------------------------------------------------------------

; UART_PUTC — wyślij bajt z A (inline, nie niszczy A)
.macro UART_PUTC
.local @wait
@wait:  bit RIA_READY
        bpl @wait           ; bit7=0 → TX FIFO pełne, czekaj
        sta RIA_TX
.endmacro

; UART_RXCHECK — sprawdź czy bajt odebrany; Z=1 brak, Z=0 jest
.macro UART_RXCHECK
        lda RIA_READY
        and #RIA_RX_READY
.endmacro

; RIA_CALL op — uruchom opcode OS inline (literal)
.macro RIA_CALL  op
        lda #op
        sta RIA_OP
        jsr RIA_SPIN
.endmacro

; XPUSH_BYTE val — push literału do XSTACK
.macro XPUSH_BYTE  val
        lda #val
        sta RIA_XSTACK
.endmacro

; ---------------------------------------------------------------------------
; Segment CODE — cały kod wykonywalny
; ---------------------------------------------------------------------------

.segment "CODE"

; ---------------------------------------------------------------------------
; UART — wejście/wyjście terminala
; ---------------------------------------------------------------------------

; uart_putc — wyślij bajt z A (blokujący)
; Zmienia: nic
uart_putc:
        pha
@wait:  bit RIA_READY
        bpl @wait
        sta RIA_TX
        pla
        rts

; uart_getc — odbierz bajt (blokujący)
; Wyjście: A = odebrany bajt
uart_getc:
@wait:  bit RIA_READY
        bvc @wait           ; bit6=0 → RX pusty
        lda RIA_RX
        rts

; uart_getc_nb — odbierz bajt bez blokowania
; Wyjście: A = bajt, C=0 jeśli gotowy; C=1 jeśli brak danych
uart_getc_nb:
        lda RIA_READY
        and #RIA_RX_READY
        beq @empty
        lda RIA_RX
        clc
        rts
@empty: sec
        rts

; uart_puts — wyślij łańcuch zakończony zerem
; Wejście: A/X = adres łańcucha (A=lo, X=hi)
; Zmienia: A, Y
uart_puts:
        sty api_zp6
        sta api_zp0
        stx api_zp1
        ldy #0
@loop:  lda (api_zp0),y
        beq @done
@wait:  bit RIA_READY
        bpl @wait
        sta RIA_TX
        iny
        bne @loop
        inc api_zp1
        bra @loop
@done:  ldy api_zp6
        rts

; uart_putnl — wyślij CR LF
uart_putnl:
        lda #$0D
        jsr uart_putc
        lda #$0A
        jmp uart_putc

; uart_puthex — wyślij bajt w A jako 2 cyfry hex
; Zmienia: A
uart_puthex:
        pha
        lsr
        lsr
        lsr
        lsr
        jsr @hex1
        pla
        and #$0F
@hex1:  cmp #$0A
        bcc @digit
        adc #$06
@digit: adc #$30
        jmp uart_putc

; uart_putdec — wyślij liczbę 16-bit A/X w dziesiętnym
; Wejście: A=lo, X=hi
; Zmienia: A, X, Y
uart_putdec:
        sta api_zp2
        stx api_zp3
        sty api_zp6
        ldy #5
        lda #' '
        sta api_zp4         ; flaga "nie wydrukowano jeszcze cyfry"
@div:   lda #0
        ldx #16
@bitloop:
        asl api_zp2
        rol api_zp3
        rol
        cmp #10
        bcc @noadj
        sbc #10
        inc api_zp2
@noadj: dex
        bne @bitloop
        pha
        dey
        bne @div
        ldy #5
@print: pla
        bne @nonzero
        cpy #1
        bne @skip
@nonzero:
        stz api_zp4
        ora #$30
        jsr uart_putc
@skip:  dey
        bne @print
        lda api_zp4
        cmp #' '
        bne @ok
        lda #'0'
        jsr uart_putc
@ok:    ldy api_zp6
        rts

; ---------------------------------------------------------------------------
; Prymitywy XSTACK
; ---------------------------------------------------------------------------

; xpush_byte — push bajtu z A do XSTACK
xpush_byte:
        sta RIA_XSTACK
        rts

; xpush_word — push 16-bit A/X (lo, hi)
xpush_word:
        sta RIA_XSTACK
        stx RIA_XSTACK
        rts

; xpush_long — push 32-bit z api_zp2(lo)..api_zp5(hi)
xpush_long:
        lda api_zp2
        sta RIA_XSTACK
        lda api_zp3
        sta RIA_XSTACK
        lda api_zp4
        sta RIA_XSTACK
        lda api_zp5
        sta RIA_XSTACK
        rts

; xpop_byte — pop bajtu z XSTACK do A
xpop_byte:
        lda RIA_XSTACK
        rts

; xpop_word — pop 16-bit do A/X
xpop_word:
        lda RIA_XSTACK
        ldx RIA_XSTACK
        rts

; xpop_long — pop 32-bit do api_zp2..api_zp5
xpop_long:
        lda RIA_XSTACK
        sta api_zp2
        lda RIA_XSTACK
        sta api_zp3
        lda RIA_XSTACK
        sta api_zp4
        lda RIA_XSTACK
        sta api_zp5
        rts

; xstack_clear — wyczyść XSTACK (OP_ZXSTACK)
xstack_clear:
        lda #OP_ZXSTACK
        sta RIA_OP
        jmp RIA_SPIN

; xpush_str — push łańcucha zero-terminated do XSTACK w kolejności odwrotnej
; Wejście: A/X = adres łańcucha
; Wyjście: Y = długość
; Zmienia: A, Y
; Uwaga: RP6502 OS oczekuje ostatniego bajtu nazwy najwyżej na stosie
xpush_str:
        sta api_zp0
        stx api_zp1
        ldy #0
@find:  lda (api_zp0),y
        beq @found
        iny
        bne @find
        inc api_zp1
        bra @find
@found: tya
        beq @empty
@push:  dey
        lda (api_zp0),y
        sta RIA_XSTACK
        tya
        bne @push
@empty: rts

; ---------------------------------------------------------------------------
; Prymitywy fastcall RIA
; ---------------------------------------------------------------------------

; ria_set_ax — załaduj A/X do rejestrów fastcall RIA_A/X
ria_set_ax:
        sta RIA_A
        stx RIA_X
        rts

; ria_set_long — załaduj api_zp2..zp5 do RIA_A/X + RIA_SREG
ria_set_long:
        lda api_zp2
        sta RIA_A
        lda api_zp3
        sta RIA_X
        lda api_zp4
        sta RIA_SREG
        lda api_zp5
        sta RIA_SREG+1
        rts

; ria_get_long — pobierz wynik 32-bit z RIA_A/X + RIA_SREG do api_zp2..zp5
ria_get_long:
        lda RIA_A
        sta api_zp2
        lda RIA_X
        sta api_zp3
        lda RIA_SREG
        sta api_zp4
        lda RIA_SREG+1
        sta api_zp5
        rts

; ria_call — uruchom operację OS, wynik int w A/X
; Wejście: A = opcode (OP_*)
; Wyjście: A=lo, X=hi ($FFFF = błąd)
ria_call:
        sta RIA_OP
        jsr RIA_SPIN
        rts

; ria_call_long — uruchom operację OS, wynik 32-bit
; Wejście: A = opcode
; Wyjście: api_zp2..zp5 = wynik 32-bit; A/X = niskie 16 bitów
ria_call_long:
        sta RIA_OP
        jsr RIA_SPIN
        lda RIA_SREG
        sta api_zp4
        lda RIA_SREG+1
        sta api_zp5
        rts

; ---------------------------------------------------------------------------
; System
; ---------------------------------------------------------------------------

; ria_exit — zakończ program
; Wejście: A = kod wyjścia (0=OK)
ria_exit:
        sta RIA_A
        lda #OP_EXIT
        sta RIA_OP
        stp                 ; WDC65C02: zatrzymaj procesor

; ria_attr_get — pobierz atrybut systemu
; Wejście: A = id atrybutu (ATTR_*)
; Wyjście: api_zp2..zp5 = wartość 32-bit
ria_attr_get:
        sta RIA_A
        lda #OP_ATTR_GET
        jsr ria_call_long
        rts

; ria_attr_set — ustaw atrybut systemu
; Wejście: A = id atrybutu, api_zp2..zp5 = nowa wartość 32-bit
ria_attr_set:
        pha
        jsr ria_set_long
        pla
        sta RIA_XSTACK
        lda #OP_ATTR_SET
        jmp ria_call

; ---------------------------------------------------------------------------
; Pliki
; ---------------------------------------------------------------------------

; ria_open — otwórz plik
; Wejście: api_zp0/1 = adres nazwy pliku, A/X = flagi (O_*)
; Wyjście: A/X = fd, lub $FFFF = błąd
ria_open:
        sty api_zp6
        jsr ria_set_ax
        lda api_zp0
        ldx api_zp1
        jsr xpush_str
        lda #OP_OPEN
        jsr ria_call
        ldy api_zp6
        rts

; ria_close — zamknij deskryptor
; Wejście: A/X = fd
ria_close:
        jsr ria_set_ax
        lda #OP_CLOSE
        jmp ria_call

; ria_read_raw — czytaj z pliku do XSTACK (max 512 bajtów)
; Wejście: A/X = count, api_zp2 = fd
; Wyjście: A/X = przeczytano (dane czekają na XSTACK do pobrania)
ria_read_raw:
        jsr xpush_word      ; count
        lda api_zp2
        sta RIA_XSTACK      ; fd lo
        lda #0
        sta RIA_XSTACK      ; fd hi
        lda #OP_READ_XSTACK
        jmp ria_call

; ria_read_buf — czytaj z pliku do bufora RAM (max 512 bajtów)
; Wejście: api_zp0/1 = bufor, A/X = count, api_zp2 = fd
; Wyjście: A/X = przeczytano
ria_read_buf:
        sty api_zp6
        sta api_zp3
        stx api_zp4
        jsr ria_read_raw
        cmp #$FF
        beq @err
        sta api_zp7
        tax
        beq @done
        ldy #0
@pop:   lda RIA_XSTACK
        sta (api_zp0),y
        iny
        dex
        bne @pop
@done:  lda api_zp7
        ldx #0
@err:   ldy api_zp6
        rts

; ria_write_buf — pisz bufor RAM do pliku (max 512 bajtów)
; Wejście: api_zp0/1 = bufor, A/X = count, api_zp2 = fd
; Wyjście: A/X = zapisano
ria_write_buf:
        sty api_zp6
        sta api_zp3
        stx api_zp4
        ldy api_zp3
        beq @skipdata
@push:  dey
        lda (api_zp0),y
        sta RIA_XSTACK
        tya
        bne @push
        lda (api_zp0)
        sta RIA_XSTACK
@skipdata:
        lda api_zp3
        ldx api_zp4
        jsr xpush_word
        lda api_zp2
        sta RIA_XSTACK
        lda #0
        sta RIA_XSTACK
        lda #OP_WRITE_XSTACK
        jsr ria_call
        ldy api_zp6
        rts

; ria_lseek — przesuń pozycję w pliku
; Wejście: api_zp2..zp5 = offset 32-bit, A = whence (SEEK_*), api_zp6 = fd
; Wyjście: api_zp2..zp5 = nowa pozycja 32-bit
ria_lseek:
        pha
        jsr xpush_long
        pla
        sta RIA_XSTACK
        lda api_zp6
        sta RIA_XSTACK
        lda #0
        sta RIA_XSTACK
        lda #OP_LSEEK
        jsr ria_call_long
        jsr ria_get_long
        rts

; ria_unlink — usuń plik
; Wejście: api_zp0/1 = adres nazwy
ria_unlink:
        lda api_zp0
        ldx api_zp1
        jsr xpush_str
        lda #OP_UNLINK
        jmp ria_call

; ria_rename — zmień nazwę / przenieś
; Wejście: api_zp0/1 = stara nazwa, api_zp6/7 = nowa nazwa
ria_rename:
        lda api_zp6
        ldx api_zp7
        jsr xpush_str
        lda api_zp0
        ldx api_zp1
        jsr xpush_str
        lda #OP_RENAME
        jmp ria_call

; ---------------------------------------------------------------------------
; Katalogi
; ---------------------------------------------------------------------------

; ria_opendir — otwórz katalog
; Wejście: api_zp0/1 = ścieżka
; Wyjście: A/X = dirdes, $FFFF = błąd
ria_opendir:
        lda api_zp0
        ldx api_zp1
        jsr xpush_str
        lda #OP_OPENDIR
        jmp ria_call

; ria_closedir — zamknij katalog
; Wejście: A/X = dirdes
ria_closedir:
        jsr ria_set_ax
        lda #OP_CLOSEDIR
        jmp ria_call

; ria_rewinddir — przewiń do początku
; Wejście: A/X = dirdes
ria_rewinddir:
        jsr ria_set_ax
        lda #OP_REWINDDIR
        jmp ria_call

; ria_readdir — czytaj następny wpis katalogu
; Wejście: A/X = dirdes
; Wyjście: A/X = 0 OK (dane struktury f_stat przez XSTACK), $FFFF = błąd
;          gdy fname[0]=0 → koniec katalogu
ria_readdir:
        jsr ria_set_ax
        lda #OP_READDIR
        jmp ria_call

; ria_mkdir — utwórz katalog
; Wejście: api_zp0/1 = ścieżka
ria_mkdir:
        lda api_zp0
        ldx api_zp1
        jsr xpush_str
        lda #OP_MKDIR
        jmp ria_call

; ria_chdir — zmień bieżący katalog
; Wejście: api_zp0/1 = ścieżka
ria_chdir:
        lda api_zp0
        ldx api_zp1
        jsr xpush_str
        lda #OP_CHDIR
        jmp ria_call

; ria_chdrive — zmień aktywny dysk
; Wejście: api_zp0/1 = nazwa dysku (np. "0:", "MSC0:")
ria_chdrive:
        lda api_zp0
        ldx api_zp1
        jsr xpush_str
        lda #OP_CHDRIVE
        jmp ria_call

; ria_getcwd — pobierz bieżący katalog do bufora RAM
; Wejście: api_zp0/1 = bufor docelowy, A/X = rozmiar bufora
; Wyjście: A/X = 0 OK, bufor wypełniony
ria_getcwd:
        sty api_zp6
        jsr xpush_word
        lda #OP_GETCWD
        jsr ria_call
        ldy #0
@pop:   lda RIA_XSTACK
        sta (api_zp0),y
        beq @done
        iny
        bne @pop
        inc api_zp1
        bra @pop
@done:  lda #0
        ldx #0
        ldy api_zp6
        rts

; ria_chmod — ustaw atrybuty pliku
; Wejście: api_zp0/1 = ścieżka, A = attr (bity ustawiane), X = maska
ria_chmod:
        jsr xpush_word
        lda api_zp0
        ldx api_zp1
        jsr xpush_str
        lda #OP_CHMOD
        jmp ria_call

; ria_syncfs — synchronizuj system plików
ria_syncfs:
        lda #OP_SYNCFS
        jmp ria_call

; ---------------------------------------------------------------------------
; Etykieta woluminu
; ---------------------------------------------------------------------------

; ria_setlabel — ustaw etykietę dysku
; Wejście: api_zp0/1 = etykieta
ria_setlabel:
        lda api_zp0
        ldx api_zp1
        jsr xpush_str
        lda #OP_SETLABEL
        jmp ria_call

; ria_getlabel — pobierz etykietę dysku
; Wejście: api_zp0/1 = ścieżka dysku (np. "0:"), api_zp6/7 = bufor na wynik
; Wyjście: A/X = 0 OK, bufor wypełniony
ria_getlabel:
        lda api_zp0
        ldx api_zp1
        jsr xpush_str
        lda #OP_GETLABEL
        jsr ria_call
        ldy #0
@pop:   lda RIA_XSTACK
        sta (api_zp6),y
        beq @done
        iny
        bne @pop
        inc api_zp7
        bra @pop
@done:  rts

; ---------------------------------------------------------------------------
; Zegar / czas
; ---------------------------------------------------------------------------

; ria_clock — tiki zegarowe od startu (CLOCKS_PER_SEC = 1 000 000)
; Wyjście: api_zp2..zp5 = wartość 32-bit
ria_clock:
        lda #OP_CLOCK
        jsr ria_call_long
        rts

; ria_clock_gettime — czas Unix
; Wyjście: api_zp2..zp5 = tv_sec 32-bit (kolejne bajty przez XSTACK = tv_nsec)
ria_clock_gettime:
        lda #0
        sta RIA_A           ; CLOCK_REALTIME = 0
        lda #OP_CLOCK_GETTIME
        jsr ria_call
        jsr xpop_long       ; tv_sec → api_zp2..zp5
        rts

; ---------------------------------------------------------------------------
; XRAM — portale 0 i 1
; ---------------------------------------------------------------------------

; xram0_set_addr — ustaw adres portalu 0
; Wejście: A/X = adres XRAM (A=lo, X=hi)
xram0_set_addr:
        sta RIA_ADDR0
        stx RIA_ADDR0+1
        rts

xram1_set_addr:
        sta RIA_ADDR1
        stx RIA_ADDR1+1
        rts

; xram0_set_step — ustaw krok auto-inkrementacji portalu 0
; Wejście: A = krok (0=brak, 1=+1/bajt, itp.)
xram0_set_step:
        sta RIA_STEP0
        rts

xram1_set_step:
        sta RIA_STEP1
        rts

; xram0_write_byte — zapisz bajt Y pod adres XRAM A/X
xram0_write_byte:
        jsr xram0_set_addr
        sty RIA_RW0
        rts

; xram0_read_byte — odczytaj bajt z adresu XRAM A/X
; Wyjście: A = wartość
xram0_read_byte:
        jsr xram0_set_addr
        lda RIA_RW0
        rts

; xram0_write_buf — kopiuj blok RAM → XRAM przez portal 0
; Wejście: api_zp0/1 = źródło RAM, A/X = adres XRAM, api_zp2 = liczba bajtów
; Zmienia: A, Y
xram0_write_buf:
        jsr xram0_set_addr
        lda #1
        sta RIA_STEP0
        ldy #0
@loop:  lda (api_zp0),y
        sta RIA_RW0
        iny
        cpy api_zp2
        bne @loop
        rts

; xram0_read_buf — kopiuj blok XRAM → RAM przez portal 0
; Wejście: api_zp0/1 = cel RAM, A/X = adres XRAM, api_zp2 = liczba bajtów
xram0_read_buf:
        jsr xram0_set_addr
        lda #1
        sta RIA_STEP0
        ldy #0
@loop:  lda RIA_RW0
        sta (api_zp0),y
        iny
        cpy api_zp2
        bne @loop
        rts

; ---------------------------------------------------------------------------
; VSync
; ---------------------------------------------------------------------------

; vsync_wait — czekaj na nową ramkę VGA
; Zmienia: A
vsync_wait:
        lda RIA_VSYNC
@wait:  cmp RIA_VSYNC
        beq @wait
        rts

; ---------------------------------------------------------------------------
; VIA 6522
; ---------------------------------------------------------------------------

; via_timer1_set — ustaw Timer 1 (one-shot)
; Wejście: A = interwał lo, X = interwał hi
via_timer1_set:
        sta VIA_T1CL
        stx VIA_T1CH
        rts

; via_timer1_wait — czekaj na przepełnienie Timer 1 (bit6 IFR)
via_timer1_wait:
@wait:  lda VIA_IFR
        and #$40
        beq @wait
        rts

; ---------------------------------------------------------------------------
; XREG — konfiguracja urządzeń
; ---------------------------------------------------------------------------

; xreg_send16 — wyślij wartość 16-bit do rejestru urządzenia
; Wejście: api_zp2 = device, api_zp3 = channel, api_zp4 = address, A/X = wartość
; Wyjście: A/X = 0 OK
xreg_send16:
        stx RIA_XSTACK      ; value hi
        sta RIA_XSTACK      ; value lo
        lda api_zp4
        sta RIA_XSTACK      ; address
        lda api_zp3
        sta RIA_XSTACK      ; channel
        lda api_zp2
        sta RIA_XSTACK      ; device
        lda #OP_XREG
        jmp ria_call

; xreg_keyboard_enable — włącz raportowanie klawiatury w XRAM
; Wejście: A/X = adres XRAM bufora (zwykle $FFE0)
; device=0, channel=0, address=0
xreg_keyboard_enable:
        sty api_zp6
        stx RIA_XSTACK      ; value hi
        sta RIA_XSTACK      ; value lo
        lda #0
        sta RIA_XSTACK      ; address
        sta RIA_XSTACK      ; channel
        sta RIA_XSTACK      ; device
        lda #OP_XREG
        jsr ria_call
        ldy api_zp6
        rts

; xreg_keyboard_disable — wyłącz klawiaturę XRAM ($FFFF = disable)
xreg_keyboard_disable:
        lda #$FF
        sta RIA_XSTACK      ; value hi
        sta RIA_XSTACK      ; value lo
        lda #0
        sta RIA_XSTACK      ; address
        sta RIA_XSTACK      ; channel
        sta RIA_XSTACK      ; device
        lda #OP_XREG
        jmp ria_call

; ---------------------------------------------------------------------------

.endif ; RIA_API_INCLUDED

; --- EOF ria_api.s ---
