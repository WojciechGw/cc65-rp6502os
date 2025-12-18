;.export __STARTUP__
.import _main, initlib, donelib

ARGC_LOC = $0200           ; must match shell RUN_ARGS_BASE
ARGV_LOC = ARGC_LOC + 1    ; pointer table starts immediately after argc

.segment "STARTUP"
__STARTUP__:
    ; Initialize C runtime (zero BSS, run constructors)
    jsr initlib

    ; Push argv, then argc (16-bit) for cc65 call convention
    lda #<ARGV_LOC
    pha
    lda #>ARGV_LOC
    pha
    lda ARGC_LOC           ; argc low
    pha
    lda #0                 ; argc high
    pha
    jsr _main
    pla
    pla
    pla
    pla

    ; Run destructors/flush stdio
    jsr donelib
    ; Return to caller (OS shell should JSR here)
    rts
