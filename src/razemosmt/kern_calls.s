;
; kern_calls.s — C-callable wrappers for razemOSmt kernel syscalls
;

.export _kern_sleep_frames
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

.segment "BSS"
_kern_task_create_slot: .res 1

.segment "CODE"

; void __fastcall__ kern_sleep_frames(unsigned int n)
; __fastcall__: A=lo byte, X=hi byte on entry
_kern_sleep_frames:
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
