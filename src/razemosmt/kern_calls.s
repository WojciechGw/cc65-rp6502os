;
; kern_calls.s — C-callable wrappers for razemOSmt kernel syscalls
;

.export _kern_sleep_frames
.export _kern_sleep_ms
.export _kern_task_create
.export _kern_task_create_slot   ; slot parameter (set before calling)
.export _kern_task_kill
.export _kern_set_phi2
.export _kern_set_irqfreq

.importzp c_sp, ptr1

KERN_TASK_CREATE  = $0206
KERN_TASK_KILL    = $0209
KERN_SLEEP_FRAMES = $020F
KERN_CLOCK        = $0251
KERN_SET_IRQFREQ  = $025A
KERN_SET_PHI2     = $025D

KDATA_IRQ_HZ_LO   = $0D10   ; via_irq_hz_lo in KDATA ($0D00 + 16)
KDATA_IRQ_HZ_HI   = $0D11   ; via_irq_hz_hi in KDATA ($0D00 + 17)

.segment "BSS"
_kern_task_create_slot: .res 1
; temporaries for kern_sleep_ms (9 bytes)
kcall_ms_lo:  .res 1   ; ms lo (input) / quotient lo (output)
kcall_ms_hi:  .res 1   ; ms hi (input) / quotient hi (output)
kcall_r0:     .res 1   ; product lo0 / dividend lo0
kcall_r1:     .res 1   ; product lo1 / dividend lo1
kcall_p2:     .res 1   ; product hi0 / dividend hi0 / remainder lo
kcall_p3:     .res 1   ; product hi1 / dividend hi1 / remainder hi
kcall_a0:     .res 1   ; addend lo  (hz shifted left)
kcall_a1:     .res 1   ; addend hi
kcall_a2:     .res 1   ; addend overflow (hz can reach 1000<<15 = 25 bits)

.segment "CODE"

; void __fastcall__ kern_sleep_frames(unsigned int n)
; __fastcall__: A=lo byte, X=hi byte on entry
_kern_sleep_frames:
    jmp KERN_SLEEP_FRAMES

; void __fastcall__ kern_sleep_ms(unsigned int ms)
; Sleeps for ms milliseconds regardless of irqfreq.
; frames = ms * irq_hz / 1000  (16x16->32 multiply, 32/16 divide)
; __fastcall__: A=ms_lo, X=ms_hi on entry
_kern_sleep_ms:
    sta kcall_ms_lo
    stx kcall_ms_hi
    ; --- 16x16->32 multiply: product(r0/r1/p2/p3) = ms * irq_hz ---
    ; Shift ms right (LSB first); add addend(a0/a1/a2) to product on each 1-bit.
    ; Shift addend left each iteration.
    lda KDATA_IRQ_HZ_LO
    sta kcall_a0
    lda KDATA_IRQ_HZ_HI
    sta kcall_a1
    stz kcall_a2
    stz kcall_r0
    stz kcall_r1
    stz kcall_p2
    stz kcall_p3
    ldy #16
@mul_bit:
    lsr kcall_ms_hi
    ror kcall_ms_lo
    bcc @mul_no_add
    clc
    lda kcall_r0
    adc kcall_a0
    sta kcall_r0
    lda kcall_r1
    adc kcall_a1
    sta kcall_r1
    lda kcall_p2
    adc kcall_a2
    sta kcall_p2
    lda kcall_p3
    adc #0
    sta kcall_p3
@mul_no_add:
    asl kcall_a0
    rol kcall_a1
    rol kcall_a2
    dey
    bne @mul_bit
    ; product in r0/r1/p2/p3 (32-bit, r0=lo)
    ; --- 32/16 restoring division: frames = product / 1000 ---
    ; Shift product left into remainder, subtract 1000, build quotient.
    stz kcall_ms_lo
    stz kcall_ms_hi
    stz kcall_a0        ; remainder lo (reuse a0/a1)
    stz kcall_a1        ; remainder hi
    ldy #32
@div_bit:
    asl kcall_r0
    rol kcall_r1
    rol kcall_p2
    rol kcall_p3
    rol kcall_a0
    rol kcall_a1
    lda kcall_a0
    sec
    sbc #<1000
    tax
    lda kcall_a1
    sbc #>1000
    bcc @div_no_sub
    stx kcall_a0
    sta kcall_a1
    sec
    bra @div_shift_q
@div_no_sub:
    clc
@div_shift_q:
    rol kcall_ms_lo
    rol kcall_ms_hi
    dey
    bne @div_bit
    lda kcall_ms_lo
    ora kcall_ms_hi
    bne @call_sleep
    inc kcall_ms_lo     ; minimum 1 frame
@call_sleep:
    lda kcall_ms_lo
    ldx kcall_ms_hi
    jmp KERN_SLEEP_FRAMES

; void __fastcall__ kern_task_create(unsigned int addr)
; slot must be set in kern_task_create_slot before calling.
; __fastcall__: A=addr_lo, X=addr_hi on entry
_kern_task_create:
    ldy _kern_task_create_slot   ; Y = slot
    jmp KERN_TASK_CREATE

; unsigned char __fastcall__ kern_task_kill(unsigned char task_id)
; __fastcall__: A=task_id on entry; returns A=0 ok, A=$FF error
_kern_task_kill:
    jmp KERN_TASK_KILL

; void __fastcall__ kern_set_irqfreq(unsigned int hz)
; __fastcall__: A=lo, X=hi (Hz, 1–1000)
_kern_set_irqfreq:
    jmp KERN_SET_IRQFREQ

; unsigned char __fastcall__ kern_set_phi2(unsigned int khz)
; __fastcall__: A=lo, X=hi; returns A=0 ok, A=$FF error
_kern_set_phi2:
    jmp KERN_SET_PHI2
