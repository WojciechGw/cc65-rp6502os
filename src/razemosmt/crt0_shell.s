;
; crt0_shell.s — TASK0 startup for razemOSmt
;
; Sends '!' via UART to confirm kernel reached TASK0_ENTRY ($0E60).
; Zeroes cc65 ZP ($0028-$0041), sets c_sp, calls _main.
; Does NOT reset hardware SP (kernel sets it to $013F).
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

    ; Zero cc65 ZP variables ($0028–$0041, zpspace=26 bytes)
    lda #0
    ldx #25
@zp_clr:
    sta $28,x
    dex
    bpl @zp_clr

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
