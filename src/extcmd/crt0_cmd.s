;.export __STARTUP__
.import _main, initlib, donelib, pushax

ARGC_LOC = $0200           ; must match shell RUN_ARGS_BASE
ARGV_LOC = ARGC_LOC + 1    ; pointer table starts immediately after argc

.segment "STARTUP"
__STARTUP__:
    ; Initialize C runtime (zero BSS, run constructors, init sp)
    jsr initlib

    ; Push argc onto cc65 software stack (left param = deeper)
    lda ARGC_LOC           ; argc value
    ldx #0                 ; argc high byte
    jsr pushax

    ; Push argv onto cc65 software stack (right param = top)
    lda #<ARGV_LOC         ; argv pointer = $0201
    ldx #>ARGV_LOC
    jsr pushax

    ; Y = total argument bytes on software stack (2 args x 2 bytes = 4)
    ldy #4
    ; tail-call _main: when _main does RTS it returns here (to jsr donelib)
    jsr __call_main

    ; Run destructors/flush stdio
    jsr donelib
    ; Return to caller (OS shell should JSR here)
    rts

__call_main:
    jmp _main
