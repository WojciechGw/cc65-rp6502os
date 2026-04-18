;
; crt0_task.s — startup for TASK1-7 in razemOSmt
;
; Called via fake IRQ frame set up by sys_task_create (RTI → entry point).
; Hardware SP is already set to task's PAGE1 slot by sys_task_create.
; Does NOT reset hardware SP.
; ZP slice base is retrieved dynamically from kernel via KERN_GET_TASK_ID +
; TCB_ZPBASE lookup — supports any slot (0-7) without recompilation.
; On return from main: calls KERN_EXIT ($0203).
;

.export _init, _exit
.export __STARTUP__ : absolute = 1

.import _main
.import __RAM_START__, __RAM_SIZE__

.include "zeropage.inc"

KERN_EXIT        = $0203
KERN_GET_TASK_ID = $0212    ; A = current task_id

; TCB layout (mirror kernel.s)
TCB_BASE   = $0D60
TCB_SIZE   = 64
TCB_ZPBASE = 30     ; offset of ZP slice base lo in TCB

.segment "STARTUP"

_init:
    cld

    ; Get task_id and look up ZP base from kernel's zp_slot_base table.
    ; kernel.s KDATA: stack_pool_free=$0D00, zp_pool_free=$0D01,
    ;                 zp_slot_base[0..7]=$0D02..$0D09
    ; task_id == slot number (sys_task_create assigns same slot for both)
    ; So: ZP base = zp_slot_base[task_id] = mem[$0D02 + task_id]
    ; Read TCB_ZPBASE from our own TCB.
    ; Use c_sp/c_sp+1 as temporary ZP pointer (safe: we write before read).
    ; c_sp is in this task's ZP slice (e.g. $0042 for TASK1).
    ; IRQ may fire but kernel IRQ handler only touches kzp_* ($1A-$27) and
    ; TCB fields — c_sp in task ZP is saved/restored, so no conflict.
    jsr KERN_GET_TASK_ID    ; A = task_id (0..7)
    pha                     ; save on PAGE1 stack (slot already set by kernel)
    lsr
    lsr                     ; A = task_id / 4
    clc
    adc #>TCB_BASE          ; hi byte of TCB[id]
    sta c_sp+1
    pla                     ; restore task_id
    asl
    asl
    asl
    asl
    asl
    asl                     ; A = (task_id * 64) lo byte
    clc
    adc #<TCB_BASE
    sta c_sp
    bcc @ptr_ok
    inc c_sp+1
@ptr_ok:
    ldy #TCB_ZPBASE
    lda (c_sp),y            ; A = ZP base lo for this task


    ; Zero 26 bytes at ZP base
    tax                     ; X = ZP base start address lo
    lda #0
    ldy #26
@zp_clr:
    sta $00,x
    inx
    dey
    bne @zp_clr

    ; Set cc65 software stack pointer
    lda #<(__RAM_START__ + __RAM_SIZE__)
    sta c_sp
    lda #>(__RAM_START__ + __RAM_SIZE__)
    sta c_sp+1

    jsr _main

_exit:
    lda #0
    jmp KERN_EXIT
