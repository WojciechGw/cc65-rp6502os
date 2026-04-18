;
; crt0_task.s — startup for TASK1-3 in razemOSmt
;
; Called via fake IRQ frame set up by sys_task_create (RTI → entry point).
; Hardware SP is already set to task partition by sys_task_create.
; Does NOT reset hardware SP.
; On return from main: calls KERN_EXIT ($0203).
;

.export _init, _exit
.export __STARTUP__ : absolute = 1

.import _main
.import __RAM_START__, __RAM_SIZE__

.include "zeropage.inc"

KERN_EXIT = $0203

.segment "STARTUP"

_init:
    cld

    ; Zero cc65 ZP variables ($0042–$005B, zpspace=26 bytes)
    lda #0
    ldx #25
@zp_clr:
    sta $42,x
    dex
    bpl @zp_clr

    ; Set cc65 software stack pointer — do NOT touch hardware SP
    lda #<(__RAM_START__ + __RAM_SIZE__)
    sta c_sp
    lda #>(__RAM_START__ + __RAM_SIZE__)
    sta c_sp+1

    ; Call main() — should never return
    jsr _main

_exit:
    lda #0
    jmp KERN_EXIT
