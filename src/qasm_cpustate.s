.include "rp6502.inc"

.export  _capture_cpu_state
.import  _cpu_state

.segment "ZEROPAGE"
ret_lo:  .res 1   ; (PC-1).low
ret_hi:  .res 1   ; (PC-1).high
saveA:   .res 1
saveX:   .res 1
saveY:   .res 1
saveP:   .res 1
saveS:   .res 1

.segment "CODE"

; void capture_cpu_state(void);
_capture_cpu_state:

        ; --- zachowaj bieżące A,X,Y,S,P do tymczasów ---

        sta saveA       ; A z miejsca wywołania
        stx saveX       ; X z miejsca wywołania
        sty saveY       ; Y z miejsca wywołania

        tsx
        stx saveS       ; S z miejsca wywołania

        php             ; P -> stos
        pla
        sta saveP       ; P z miejsca wywołania

        ; --- pobierz PC (adres po JSR) i wpisz do cpu_state ---

        jsr get_pc      ; wstawi PC do cpu_state.pc (offset 0,1)

        ; --- wpisz P, A, X, Y, S do cpu_state ---

        lda saveP
        sta _cpu_state+2

        lda saveA
        sta _cpu_state+3

        lda saveX
        sta _cpu_state+4

        lda saveY
        sta _cpu_state+5

        lda saveS
        sta _cpu_state+6

        ; przywróć rejestry (żeby dalej program szedł w tym samym stanie)
        lda saveA
        ldx saveX
        ldy saveY

        rts

; get_pc:
; używa ret_lo/ret_hi, zapisuje PC bezpośrednio do _cpu_state+0/+1
get_pc:
        ; na stosie: adres powrotu (PC-1)
        pla
        sta ret_lo
        pla
        sta ret_hi

        ; PC.low = (PC-1).low + 1
        lda ret_lo
        clc
        adc #1
        sta _cpu_state      ; offset 0 (low)

        ; PC.high = (PC-1).high + carry
        lda ret_hi
        adc #0
        sta _cpu_state+1    ; offset 1 (high)

        ; przywróć oryginalne (PC-1) na stos (żeby RTS wrócił normalnie)
        lda ret_hi
        pha
        lda ret_lo
        pha

        rts
