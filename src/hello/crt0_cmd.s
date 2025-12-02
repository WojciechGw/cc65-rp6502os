.export __STARTUP__
.import _main, initlib, donelib

.segment "STARTUP"
__STARTUP__:
    ; Initialize C runtime (zero BSS, run constructors)
    jsr initlib
    ; Call user code
    jsr _main
    ; Run destructors/flush stdio
    jsr donelib
    ; Return to caller (OS shell should JSR here)
    rts
