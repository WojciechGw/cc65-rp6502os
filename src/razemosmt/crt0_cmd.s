;
; crt0_cmd.s — startup for .com programs in razemOSmt
;
; Change from original src/extcmd/crt0_cmd.s:
;   ARGC_LOC changed from $0200 to $0C60
;   Reason: $0200 is now the KERNEL JUMP TABLE in razemOSmt.
;   New address $0C60 is the start of KDATA (kernel data area),
;   where the kernel stores argc/argv for the active task.
;
; shell.h must define:
;   #define RUN_ARGS_BASE 0x0C60   ; was 0x0200
;

.import _main, initlib, donelib, pushax

; argc/argv block address passed by shell to task before launch.
; Must match RUN_ARGS_BASE in shell.h (razemosmt version).
ARGC_LOC = $0C60           ; KDATA start — argc/argv block for active task
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
    lda #<ARGV_LOC         ; argv pointer
    ldx #>ARGV_LOC
    jsr pushax

    ; Y = total argument bytes on software stack (2 args x 2 bytes = 4)
    ldy #4
    ; tail-call _main
    jsr __call_main

    ; Run destructors/flush stdio
    jsr donelib

    ; Terminate task via kernel syscall (KERN_EXIT = $0203)
    lda #0                 ; exit code = 0
    jmp $0203              ; KERN_EXIT — does not return

__call_main:
    jmp _main
