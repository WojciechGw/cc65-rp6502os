;
; crt0_shell.s — TASK0 startup for razemOSmt
;
; Sends a single '!' byte via UART immediately on entry
; to confirm that kernel_init reached TASK0_ENTRY ($0E60).
; Then sets c_sp and calls _main.
;
; Does NOT reset hardware SP.
;

.export _init, _exit
.export __STARTUP__ : absolute = 1

.import _main
.import __RAM_START__, __RAM_SIZE__

.include "zeropage.inc"

RIA_READY = $FFE0
RIA_TX    = $FFE1

.segment "STARTUP"

_init:
    cld

    ; --- diagnostic: send '!' immediately to confirm TASK0 reached ---
@tx_wait:
    lda RIA_READY
    and #$80
    beq @tx_wait
    lda #'!'
    sta RIA_TX

    ; Set cc65 software stack pointer — do NOT touch hardware SP
    lda #<(__RAM_START__ + __RAM_SIZE__)
    sta c_sp
    lda #>(__RAM_START__ + __RAM_SIZE__)
    sta c_sp+1

    ; Call main() — never returns
    jsr _main

_exit:
    lda #0
    jmp $0203              ; KERN_EXIT
