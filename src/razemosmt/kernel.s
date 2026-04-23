;
; kernel.s — razemOSmt kernel
; Multitasking OS for WDC65C02S / Picocomputer RP6502
;
; Memory layout:
;   $0200–$025F  JUMPTABLE  — stable ABI vectors (32 × JMP abs)
;   $0260–$0C5F  KERNEL     — IRQ handler, scheduler, syscalls, ria wrappers
;   $0D00–$0DFF  KDATA      — kernel variables (pool bitmaps, zp_snap table)
;   $0E00–$0FFF  TCBAREA    — 8 × TCB (Task Control Block, 64 bytes each)
;   $1000+       TASK0      — shell (moved up to make room for 8-task TCBAREA)
;
; Kernel Zero Page (fixed addresses, NOT via linker):
;   $001A  kzp_tmp0    — scratch A on IRQ entry
;   $001B  kzp_tmp1    — scratch X
;   $001C  kzp_tmp2    — scratch Y
;   $001D  kzp_tmp3    — scratch P
;   $001E  kzp_tcb_lo  — ZP pointer to current TCB (lo)
;   $001F  kzp_tcb_hi  — ZP pointer to current TCB (hi)
;   $0020  kzp_curr    — current task ID (0..3)
;   $0021  kzp_ntask   — number of registered tasks
;   $0022  kzp_sched   — scheduler flags: bit0=preempt_en, bit1=switch_needed
;   $0023  kzp_vsync   — shadow RIA_VSYNC (new frame detection)
;   $0024  kzp_iflags  — IRQ flags: bit0=in_irq, bit1=io_locked
;   $0025  kzp_next    — task_id selected by scheduler
;   $0026  kzp_spare0
;   $0027  kzp_spare1
;
; Required assembler: ca65 (cc65 toolchain)
;

.include "ria_api.s"     ; RIA hardware constants + all ria_* / uart_* / mth_* subroutines

; ---------------------------------------------------------------------------
; ZP — kernel fixed addresses (NOT via ZEROPAGE segment, kernel is pure ASM)
; ---------------------------------------------------------------------------

kzp_tmp0    = $001A
kzp_tmp1    = $001B
kzp_tmp2    = $001C
kzp_tmp3    = $001D
kzp_tcb_lo  = $001E
kzp_tcb_hi  = $001F
kzp_curr    = $0020
kzp_ntask   = $0021
kzp_sched   = $0022
kzp_vsync   = $0023
kzp_iflags  = $0024
kzp_next    = $0025
kzp_spare0  = $0026
kzp_spare1  = $0027

; ---------------------------------------------------------------------------
; Hardware
; ---------------------------------------------------------------------------

; RIA_VSYNC = $FFE3 (defined in ria_api.s)
; RIA_IRQ   = $FFF0 (defined in ria_api.s)

; VIA IFR/IER bit masks (VIA register addresses defined in ria_api.s)
VIA_IER_SET        = %10000000   ; set bits when written with this OR'd in
VIA_IRQ_T1         = %01000000   ; bit 6 = Timer1 IRQ
VIA_ACR_T1_FREERUN = %01000000   ; ACR bits 7:6 = 01 → Timer1 continuous mode

; ---------------------------------------------------------------------------
; TCB layout — offsets (TCB = 64 bytes, base = $0D60 + task_id*64)
;
; Dynamic resource allocation:
;   - PAGE1 stack: 8 slots × 32B = 256B ($0100–$01FF), allocated from stack_pool_free bitmap
;     slot N top = $011F + N*32  (e.g. slot0=$011F, slot1=$013F, ..., slot7=$01FF)
;   - ZP slice:   8 slots × 26B ($0028–$00F7), allocated from zp_pool_free bitmap
;     slot N base = $0028 + N*26
;   - ZP snapshot stored in TCB+32..+57 (26 bytes), NOT in a separate table
;
; $00–$19 not snapshotted (cc65 never uses them when ZP start=$0028).
; kzp_* ($1A–$27) managed exclusively by kernel, never snapshotted.
;
; Max tasks: 8 ($0E00 + 8*64 = $1000, TASK0_ENTRY at $1000)
; ---------------------------------------------------------------------------

TCB_BASE    = $0E00
TCB_SIZE    = 64        ; power-of-2 → indexed with 16-bit set_tcb_ptr
MAX_TASKS   = 8

TCB_PC_LO   = 0         ; +0  program counter lo
TCB_PC_HI   = 1         ; +1  program counter hi
TCB_A       = 2         ; +2  accumulator
TCB_X       = 3         ; +3  X register
TCB_Y       = 4         ; +4  Y register
TCB_SP      = 5         ; +5  stack pointer (8-bit value, PAGE1)
TCB_P       = 6         ; +6  P register (status)
TCB_STATUS  = 7         ; +7  task state
TCB_WTYPE   = 8         ; +8  wait type
TCB_WPARAM  = 9         ; +9  wait parameter lo
TCB_WPARAM1 = 10        ; +10 wait parameter hi
TCB_ID      = 11        ; +11 task_id (fixed)
TCB_PRIO    = 12        ; +12 priority (reserved)
TCB_SPINIT  = 13        ; +13 initial SP value (top of task PAGE1 slot)
TCB_FCNT_L  = 14        ; +14 frame counter lo
TCB_FCNT_H  = 15        ; +15 frame counter hi
TCB_LADDR_L = 16        ; +16 load address lo
TCB_LADDR_H = 17        ; +17 load address hi
TCB_MSIZE_L = 18        ; +18 RAM size lo
TCB_MSIZE_H = 19        ; +19 RAM size hi
TCB_NAME    = 20        ; +20 name (8 bytes: 7 chars + null)
TCB_STSLOT  = 28        ; +28 PAGE1 stack slot (0–7)
TCB_ZPSLOT  = 29        ; +29 ZP slice slot (0–7)
TCB_ZPBASE  = 30        ; +30 ZP slice base lo ($28+slot*26)
                        ; +31 reserved
TCB_ZP_SNAP = 32        ; +32 cc65 ZP snapshot (26 bytes)
                        ; +58..+63 reserved (6 bytes) = 64 total

; Task status
TASK_DEAD    = 0
TASK_READY   = 1
TASK_RUNNING = 2
TASK_WAITING = 3

; Wait type
WAIT_NONE   = 0
WAIT_FRAMES = 1
WAIT_IO_R   = 2
WAIT_IO_W   = 3
WAIT_CHILD  = 4

; PAGE1 stack pool: 8 slots × 32B
; SP is 8-bit (PAGE1 implicit), so SPINIT = lo byte of top address
; slot N: $0100 + N*32 .. $011F + N*32;  SP top lo = $1F + N*32
STACK_SLOT_SIZE = 32
STACK_POOL_BASE = $0100
STACK_SLOT0_TOP = $1F   ; $011F
STACK_SLOT1_TOP = $3F   ; $013F
STACK_SLOT2_TOP = $5F   ; $015F
STACK_SLOT3_TOP = $7F   ; $017F
STACK_SLOT4_TOP = $9F   ; $019F
STACK_SLOT5_TOP = $BF   ; $01BF
STACK_SLOT6_TOP = $DF   ; $01DF
STACK_SLOT7_TOP = $FF   ; $01FF

; ZP pool: 8 slots × 26B, starting $0028
ZP_SLOT_SIZE = 26
ZP_POOL_BASE = $0028

; Scheduler flags (kzp_sched)
SCHED_PREEMPT_EN    = %00000001
SCHED_SWITCH_NEEDED = %00000010

; IRQ flags (kzp_iflags)
IFLAGS_IN_IRQ   = %00000001
IFLAGS_IO_LOCK  = %00000010

; ---------------------------------------------------------------------------
; SEGMENT JUMPTABLE — stable ABI vectors $0200–$025F
; ---------------------------------------------------------------------------

.segment "JUMPTABLE"

; Kernel syscalls (own logic)
jt_sys_yield:         jmp sys_yield           ; $0200 [ 0] yield CPU
jt_sys_exit:          jmp sys_exit            ; $0203 [ 1] terminate task
jt_sys_task_create:   jmp sys_task_create     ; $0206 [ 2] create task
jt_sys_task_kill:     jmp sys_task_kill       ; $0209 [ 3] kill task
jt_sys_task_wait:     jmp sys_task_wait       ; $020C [ 4] wait for child
jt_sys_sleep_frames:  jmp sys_sleep_frames    ; $020F [ 5] sleep N frames
jt_sys_get_task_id:   jmp sys_get_task_id     ; $0212 [ 6] get current task_id
jt_sys_task_status:   jmp sys_task_status     ; $0215 [ 7] task status
jt_sys_io_lock:       jmp sys_io_lock         ; $0218 [ 8] lock preemption (RIA)
jt_sys_io_unlock:     jmp sys_io_unlock       ; $021B [ 8+] unlock preemption

; Thin RIA wrappers (from ria_api.s)
jt_ria_open:          jmp ria_open            ; $021E [ 9]
jt_ria_close:         jmp ria_close           ; $0221 [10]
jt_ria_read_buf:      jmp ria_read_buf        ; $0224 [11]
jt_ria_write_buf:     jmp ria_write_buf       ; $0227 [12]
jt_ria_lseek:         jmp ria_lseek           ; $022A [13]
jt_ria_unlink:        jmp ria_unlink          ; $022D [14]
jt_ria_rename:        jmp ria_rename          ; $0230 [15]
jt_ria_opendir:       jmp ria_opendir         ; $0233 [16]
jt_ria_readdir:       jmp ria_readdir         ; $0236 [17]
jt_ria_closedir:      jmp ria_closedir        ; $0239 [18]
jt_ria_mkdir:         jmp ria_mkdir           ; $023C [19]
jt_ria_chdir:         jmp ria_chdir           ; $023F [20]
jt_ria_getcwd:        jmp ria_getcwd          ; $0242 [21]
jt_uart_putc:         jmp uart_putc           ; $0245 [22]
jt_uart_puts:         jmp uart_puts           ; $0248 [23]
jt_uart_getc:         jmp uart_getc           ; $024B [24]
jt_uart_getc_nb:      jmp uart_getc_nb        ; $024E [25]
jt_ria_clock:         jmp ria_clock           ; $0251 [26]
jt_ria_clock_gettime: jmp ria_clock_gettime   ; $0254 [27]
jt_ria_syncfs:        jmp ria_syncfs          ; $0257 [28] sync filesystem
jt_sys_set_irqfreq:   jmp sys_set_irqfreq     ; $025A [29] set IRQ/context-switch frequency (Hz)
jt_sys_set_phi2:      jmp sys_set_phi2        ; $025D [30] set PHI2 clock + reprogram VIA T1

; ---------------------------------------------------------------------------
; SEGMENT KERNEL — kernel code
; Starts at $0260 — this is the CPU RESET entry point.
; RESET vector ($FFFC) must point to $0260 (kernel_init).
; ---------------------------------------------------------------------------

.segment "KERNEL"

; ---------------------------------------------------------------------------
; kernel_init — kernel initialization
; CPU RESET entry point ($0260, start of KCODE).
; Called once by the bootloader via RESET vector.
; Entry: none
; Exit:  jumps to TASK0 entry point (cc65 crt0 at $0E60) — never returns
; ---------------------------------------------------------------------------

.export kernel_init

TASK0_ENTRY = $1000     ; cc65 crt0 __STARTUP__ — must match shell.cfg __STARTADDR__

kernel_init:
    ; Disable interrupts during initialization
    sei

    ; Clear kzp area ($001A–$0027)
    lda #0
    sta kzp_tmp0
    sta kzp_tmp1
    sta kzp_tmp2
    sta kzp_tmp3
    sta kzp_tcb_lo
    sta kzp_tcb_hi
    sta kzp_curr
    sta kzp_ntask
    sta kzp_sched
    sta kzp_vsync
    sta kzp_iflags
    sta kzp_next
    sta kzp_spare0
    sta kzp_spare1

    ; Initialize pool bitmaps: all slots free except slot 0 (TASK0)
    lda #%11111110          ; bits 1–7 free, bit 0 = TASK0 taken
    sta stack_pool_free
    sta zp_pool_free

    ; Clear TCBAREA ($0D60–$0F5F = 512 bytes = 8×64B) in two Y-wrap passes.
    lda #<TCB_BASE       ; $60
    sta kzp_tcb_lo
    lda #>TCB_BASE       ; $0D
    sta kzp_tcb_hi
    lda #0
    ldy #0
@clr_tcb_lo:
    sta (kzp_tcb_lo),y
    iny
    bne @clr_tcb_lo      ; first 256B ($0D60–$0E5F)
    inc kzp_tcb_hi       ; advance to $0E60
@clr_tcb_hi:
    sta (kzp_tcb_lo),y
    iny
    bne @clr_tcb_hi      ; second 256B ($0E60–$0F5F)
    dec kzp_tcb_hi       ; restore to $0D

    ; Read current PHI2 from RIA (ATTR_GET id=1)
    ; Result is in RIA_A ($FFF4) / RIA_X ($FFF6) after RIA_SPIN returns.
    lda #ATTR_PHI2_KHZ
    sta RIA_A
    lda #OP_ATTR_GET
    sta RIA_OP
    jsr RIA_SPIN
    lda RIA_A               ; lo byte of kHz
    sta via_phi2_lo
    lda RIA_X               ; hi byte of kHz
    sta via_phi2_hi
    ; Initialize IRQ frequency to 60 Hz (KDATA is zeroed by loader — must set explicitly)
    lda #60
    sta via_irq_hz_lo
    stz via_irq_hz_hi
    lda #1
    sta via_irq_divider
    sta via_irq_tick
    ; Initialize zp_slot_base table
    lda #$28
    sta zp_slot_base+0
    lda #$42
    sta zp_slot_base+1
    lda #$5C
    sta zp_slot_base+2
    lda #$76
    sta zp_slot_base+3
    lda #$90
    sta zp_slot_base+4
    lda #$AA
    sta zp_slot_base+5
    lda #$C4
    sta zp_slot_base+6
    lda #$DE
    sta zp_slot_base+7
    ; Compute Timer1 latch and divider for 60Hz, store in KDATA
    jsr via_compute_latch

    ; Configure VIA Timer1 — sequence from paint.c (rumbledethumps):
    ;   ACR first, then latch, then counter (T1CH write starts timer)
    lda VIA_ACR
    and #%00111111
    ora #VIA_ACR_T1_FREERUN ; bits 7:6 = 01 → continuous IRQ mode
    sta VIA_ACR
    lda via_t1_latch_lo
    sta VIA_T1LL            ; latch lo
    sta VIA_T1CL            ; counter lo
    lda via_t1_latch_hi
    sta VIA_T1LH            ; latch hi
    sta VIA_T1CH            ; counter hi — starts timer, clears T1 IFR flag
    ; Enable T1 IRQ: $C0 = set-bit ($80) | T1 ($40)
    lda #(VIA_IER_SET | VIA_IRQ_T1)
    sta VIA_IER

    ; Disable RIA VSync IRQ
    lda #0
    sta RIA_IRQ

    ; Register TASK0 in TCB[0].
    ; sys_task_create cannot be called yet (IRQ disabled, stack not set up),
    ; so we fill TCB[0] directly.
    lda #0
    jsr set_tcb_ptr         ; kzp_tcb_lo/hi → TCB[0] at $0D60

    ldy #TCB_PC_LO
    lda #<TASK0_ENTRY
    sta (kzp_tcb_lo),y
    ldy #TCB_PC_HI
    lda #>TASK0_ENTRY
    sta (kzp_tcb_lo),y

    ldy #TCB_ID
    lda #0
    sta (kzp_tcb_lo),y

    ; TASK0 gets stack slot 0 (top=$011F) and ZP slot 0 (base=$0028)
    ldy #TCB_STSLOT
    lda #0
    sta (kzp_tcb_lo),y
    ldy #TCB_ZPSLOT
    sta (kzp_tcb_lo),y
    ldy #TCB_ZPBASE
    lda #ZP_POOL_BASE
    sta (kzp_tcb_lo),y

    ldy #TCB_SPINIT
    lda #STACK_SLOT0_TOP    ; $011F
    sta (kzp_tcb_lo),y
    ldy #TCB_SP
    sta (kzp_tcb_lo),y      ; SP saved = $011F (stack is empty at entry)

    ldy #TCB_STATUS
    lda #TASK_RUNNING       ; TASK0 starts as RUNNING
    sta (kzp_tcb_lo),y

    lda #1
    sta kzp_ntask           ; one task registered

    ; Enable preemption
    lda #SCHED_PREEMPT_EN
    sta kzp_sched

    ; Set SP to TASK0 slot 0 top ($011F)
    ldx #STACK_SLOT0_TOP
    txs

    ; Enable interrupts — VSync IRQ can fire from this point on
    cli

    ; Jump to TASK0 entry.
    ; IRQ handler will save/restore TASK0 context correctly from the first VSync.
    jmp TASK0_ENTRY

; ---------------------------------------------------------------------------
; set_tcb_ptr — set kzp_tcb_lo/hi to TCB[A]
; Entry: A = task_id (0..7)
; Clobbers: A, kzp_tcb_lo, kzp_tcb_hi. Does NOT clobber kzp_tmp*.
; ---------------------------------------------------------------------------

set_tcb_ptr:
    ; TCB_BASE + task_id * 64 (16-bit: task_id=7 → 7*64=$1C0)
    ; hi = task_id >> 2;  lo = (task_id << 6) & $FF
    ; Clobbers: A only. Does NOT clobber kzp_tmp3.
    pha                 ; save task_id
    lsr
    lsr                 ; A = task_id / 4
    clc
    adc #>TCB_BASE      ; hi byte of TCB address
    sta kzp_tcb_hi
    pla                 ; restore task_id
    asl
    asl
    asl
    asl
    asl
    asl                 ; A = (task_id * 64) lo byte
    clc
    adc #<TCB_BASE
    sta kzp_tcb_lo
    bcc @done
    inc kzp_tcb_hi      ; propagate carry
@done:
    rts

; ---------------------------------------------------------------------------
; IRQ Handler — context switch on VSync
; CPU auto-pushes on IRQ: PChi, PClo, P (I=1)
; ---------------------------------------------------------------------------

.export irq_handler

irq_handler:
    ; --- 0. IRQ ACK ---
    ; Save A first (needed before any read that clobbers flags).
    sta kzp_tmp0

    ; Ack VIA Timer1: reading T1CL clears the T1 IRQ flag in IFR.
    ; Do this unconditionally — safe even if T1 was not the source.
    lda VIA_T1CL

    ; Check VIA IFR: was this a Timer1 IRQ?
    ; After ack, IFR bit6 is clear — but before ack it was set if T1 fired.
    ; We already acked, so check via_irq_tick counter instead: any IRQ here
    ; that passed the VIA_T1CL read is a valid T1 tick (only source we enable).

    ; --- 1. Atomic save X, Y ---
    stx kzp_tmp1
    sty kzp_tmp2

    ; --- 2. Count T1 ticks; switch context only every via_irq_divider ticks ---
    dec via_irq_tick
    beq @tick_do_switch
    jmp @spurious       ; not yet time for context switch
@tick_do_switch:

    lda via_irq_divider
    sta via_irq_tick    ; reload tick counter

    ; --- 3. Check preemption enabled and no IO lock ---
    lda kzp_sched
    and #SCHED_PREEMPT_EN
    bne @preempt_ok
    jmp @spurious       ; preemption disabled → no switch
@preempt_ok:
    lda kzp_iflags
    and #IFLAGS_IO_LOCK
    beq @iolock_ok
    jmp @spurious       ; IO lock active → no switch
@iolock_ok:

    ; --- 4. Set kzp_iflags.in_irq ---
    lda kzp_iflags
    ora #IFLAGS_IN_IRQ
    sta kzp_iflags

    ; --- 5. Point TCB pointer to current task ---
    lda kzp_curr
    jsr set_tcb_ptr

    ; --- 6. Save CPU registers to TCB ---
    ; A, X, Y
    ldy #TCB_A
    lda kzp_tmp0
    sta (kzp_tcb_lo),y
    ldy #TCB_X
    lda kzp_tmp1
    sta (kzp_tcb_lo),y
    ldy #TCB_Y
    lda kzp_tmp2
    sta (kzp_tcb_lo),y

    ; Save current SP as-is (after IRQ pushed PChi/PClo/P, SP = SP_before_IRQ - 3).
    ; RTI will pop P/PClo/PChi from this SP, so we restore this exact value.
    tsx
    txa
    ldy #TCB_SP
    sta (kzp_tcb_lo),y

    ; PC and P: located on PAGE1 stack at $0101+SP..$0103+SP
    ; (CPU pushes in order: PChi, PClo, P — P is on top of stack)
    tsx
    lda $0101,x         ; P — status (top of stack after IRQ)
    ldy #TCB_P
    sta (kzp_tcb_lo),y
    lda $0102,x         ; PClo
    ldy #TCB_PC_LO
    sta (kzp_tcb_lo),y
    lda $0103,x         ; PChi
    ldy #TCB_PC_HI
    sta (kzp_tcb_lo),y

    ; --- 7. Save cc65 ZP slice to TCB snapshot (26 bytes) ---
    ; ZP source = TCB_ZPBASE (lo byte of task's ZP slice base)
    ; TCB dest  = TCB_ZP_SNAP (offset 32) .. +25
    ; Use kzp_spare0 ($26) as ZP src ptr, kzp_spare1 ($27) as TCB offset counter.
    ldy #TCB_ZPBASE
    lda (kzp_tcb_lo),y  ; ZP slice base for current task
    sta kzp_spare0      ; ZP src pointer lo (hi=$00 always)
    lda #TCB_ZP_SNAP
    sta kzp_spare1
@zp_save:
    ldy kzp_spare0
    lda $00,y
    ldy kzp_spare1
    sta (kzp_tcb_lo),y
    inc kzp_spare0
    inc kzp_spare1
    lda kzp_spare1
    cmp #(TCB_ZP_SNAP + 26)
    bne @zp_save

    ; --- 8. Set current task status = READY (only if it was RUNNING) ---
    ; A WAITING task must not be overwritten — it is sleeping intentionally.
    ldy #TCB_STATUS
    lda (kzp_tcb_lo),y
    cmp #TASK_RUNNING
    bne @keep_status
    lda #TASK_READY
    sta (kzp_tcb_lo),y
@keep_status:

    ; --- 9. Increment frame counter ---
    ldy #TCB_FCNT_L
    lda (kzp_tcb_lo),y
    clc
    adc #1
    sta (kzp_tcb_lo),y
    ldy #TCB_FCNT_H
    lda (kzp_tcb_lo),y
    adc #0
    sta (kzp_tcb_lo),y

    ; --- 10. Switch SP to last PAGE1 slot top ($01FF) for scheduler ---
    ldx #STACK_SLOT7_TOP
    txs

    ; --- 11. Run scheduler — result in kzp_next ---
    jsr kernel_scheduler

    ; --- 12. Set kzp_curr = kzp_next ---
    lda kzp_next
    sta kzp_curr

    ; --- 13. Point TCB pointer to new task ---
    jsr set_tcb_ptr

    ; --- 14. Mark new task as RUNNING (skip if WAITING — sleeping task stays
    ; WAITING so Phase 1 keeps ticking its countdown each IRQ)
    ldy #TCB_STATUS
    lda (kzp_tcb_lo),y
    cmp #TASK_WAITING
    beq @skip_set_running
    lda #TASK_RUNNING
    sta (kzp_tcb_lo),y
@skip_set_running:

    ; --- 15+16+17+18. Pre-load registers, restore ZP slice, set SP ---

    ; Pre-load SP, A, X, Y from TCB (kzp_tcb_lo/hi valid here)
    ldy #TCB_SP
    lda (kzp_tcb_lo),y
    tax             ; X = new task SP
    ldy #TCB_A
    lda (kzp_tcb_lo),y
    sta kzp_tmp0
    ldy #TCB_X
    lda (kzp_tcb_lo),y
    sta kzp_tmp1
    ldy #TCB_Y
    lda (kzp_tcb_lo),y
    sta kzp_tmp2

    ; Clear in_irq flag
    lda kzp_iflags
    and #(.BITNOT IFLAGS_IN_IRQ & $FF)
    sta kzp_iflags

    ; Restore cc65 ZP slice for new task.
    ; kzp_spare0 = ZP dest (from TCB_ZPBASE of new task)
    ; kzp_spare1 = TCB src offset (TCB_ZP_SNAP..+25)
    ; kzp_tmp0/1/2/tcb_lo/hi must not be clobbered — spare0/1 are safe ($26/$27).
    ldy #TCB_ZPBASE
    lda (kzp_tcb_lo),y  ; ZP base of new task
    sta kzp_spare0
    lda #TCB_ZP_SNAP
    sta kzp_spare1
@zp_rest:
    ldy kzp_spare1
    lda (kzp_tcb_lo),y
    ldy kzp_spare0
    sta $00,y
    inc kzp_spare0
    inc kzp_spare1
    lda kzp_spare1
    cmp #(TCB_ZP_SNAP + 26)
    bne @zp_rest

    ; Set new task SP
    txs

    lda kzp_tmp2
    tay
    lda kzp_tmp1
    tax
    lda kzp_tmp0

    ; --- 19. RTI — jump to new task ---
    ; PC/P are already on the new task PAGE1 stack (pushed by:
    ;   a) previous IRQ context switch (step 6 for that task), or
    ;   b) fake frame from sys_task_create)
    rti

@spurious:
    lda kzp_tmp2
    tay
    lda kzp_tmp1
    tax
    lda kzp_tmp0
    rti

; ---------------------------------------------------------------------------
; kernel_scheduler — select next task to run
; Entry: kzp_curr = current (just suspended) task_id
; Exit:  kzp_next = task_id of next task to run
; Clobbers: A, X, Y, kzp_tcb_lo/hi
; Note: called from kernel stack, SEI is active
; ---------------------------------------------------------------------------

kernel_scheduler:
    ; --- Phase 1: wake WAITING tasks — scan all MAX_TASKS slots ---
    ldx #0
@wake_loop:
    txa
    jsr set_tcb_ptr
    ldy #TCB_STATUS
    lda (kzp_tcb_lo),y
    cmp #TASK_WAITING
    bne @wake_next

    ldy #TCB_WTYPE
    lda (kzp_tcb_lo),y
    cmp #WAIT_FRAMES
    bne @wake_check_child

    ; WAIT_FRAMES: decrement 16-bit wait_param (lo in TCB_WPARAM, hi in TCB_WPARAM1)
    ldy #TCB_WPARAM
    lda (kzp_tcb_lo),y
    bne @dec_lo
    ; lo == 0: check hi
    ldy #TCB_WPARAM1
    lda (kzp_tcb_lo),y
    beq @wake_task          ; hi == 0 too → wake (was 0 frames)
    ; decrement hi (DEC (zp),y not supported — manual)
    sec
    sbc #1
    sta (kzp_tcb_lo),y
    ldy #TCB_WPARAM
@dec_lo:
    ; decrement lo
    lda (kzp_tcb_lo),y
    sec
    sbc #1
    sta (kzp_tcb_lo),y
    ; check if lo==0 and hi==0
    sta kzp_spare0
    ldy #TCB_WPARAM1
    lda (kzp_tcb_lo),y
    ora kzp_spare0
    bne @wake_next          ; not zero yet
    bra @wake_task

@wake_check_child:
    cmp #WAIT_CHILD
    bne @wake_next
    ; WAIT_CHILD: check child status (id in wait_param lo)
    ldy #TCB_WPARAM
    lda (kzp_tcb_lo),y  ; child task_id
    sta kzp_spare0      ; save temporarily
    jsr set_tcb_ptr     ; point to child TCB
    ldy #TCB_STATUS
    lda (kzp_tcb_lo),y
    cmp #TASK_DEAD
    bne @wake_restore
@wake_task:
    ; Restore pointer to task X and wake it
    txa
    jsr set_tcb_ptr
    ldy #TCB_STATUS
    lda #TASK_READY
    sta (kzp_tcb_lo),y
    ldy #TCB_WTYPE
    lda #WAIT_NONE
    sta (kzp_tcb_lo),y
    bra @wake_next
@wake_restore:
    txa
    jsr set_tcb_ptr
@wake_next:
    inx
    cpx kzp_ntask
    bne @wake_loop

    ; --- Phase 2: round-robin from (curr+1) mod MAX_TASKS ---
    lda kzp_curr
    clc
    adc #1
    cmp #MAX_TASKS
    bcc @rr_no_wrap
    lda #0
@rr_no_wrap:
    sta kzp_next        ; starting candidate

    ldx #0              ; attempt counter (max MAX_TASKS)
@rr_loop:
    lda kzp_next
    jsr set_tcb_ptr
    ldy #TCB_STATUS
    lda (kzp_tcb_lo),y
    cmp #TASK_READY
    beq @rr_found

    lda kzp_next
    clc
    adc #1
    cmp #MAX_TASKS
    bcc @rr_inc_ok
    lda #0
@rr_inc_ok:
    sta kzp_next
    inx
    cpx #MAX_TASKS
    bne @rr_loop

    ; No READY task found → continue current task
    lda kzp_curr
    sta kzp_next

    ; Only mark RUNNING if not WAITING — a sleeping task must stay WAITING so
    ; step 8 on the next IRQ won't convert it to READY and skip Phase 1 wakeup.
    jsr set_tcb_ptr
    ldy #TCB_STATUS
    lda (kzp_tcb_lo),y
    cmp #TASK_WAITING
    beq @rr_found
    lda #TASK_RUNNING
    sta (kzp_tcb_lo),y

@rr_found:
    rts

; ---------------------------------------------------------------------------
; Syscall: sys_yield — voluntary CPU yield
; Called via: JSR $0200 (jump table entry 0)
; Clobbers: nothing (context fully restored on return)
; ---------------------------------------------------------------------------

sys_yield:
    ; Force switch by setting flag and waiting for IRQ
    lda kzp_sched
    ora #SCHED_SWITCH_NEEDED
    sta kzp_sched
    ; Wait for next VSync IRQ which will perform the context switch
@wait_irq:
    lda kzp_sched
    and #SCHED_SWITCH_NEEDED
    bne @wait_irq
    rts

; ---------------------------------------------------------------------------
; Syscall: sys_exit — terminate current task
; Entry: A = exit code
; Does not return.
; ---------------------------------------------------------------------------

sys_exit:
    ; Save exit code in TCB[curr].wait_param (readable by WAIT_CHILD)
    sta kzp_spare0
    lda kzp_curr
    jsr set_tcb_ptr
    ldy #TCB_WPARAM
    lda kzp_spare0
    sta (kzp_tcb_lo),y

    ; Mark task as DEAD
    ldy #TCB_STATUS
    lda #TASK_DEAD
    sta (kzp_tcb_lo),y

    ; Clear WTYPE
    ldy #TCB_WTYPE
    lda #WAIT_NONE
    sta (kzp_tcb_lo),y

    ; Free PAGE1 stack slot
    ldy #TCB_STSLOT
    lda (kzp_tcb_lo),y
    jsr free_stack_slot
    ; Free ZP slot
    ldy #TCB_ZPSLOT
    lda (kzp_tcb_lo),y
    jsr free_zp_slot

    ; Wait for IRQ to perform context switch to another task
    cli
@halt:
    wai                 ; WDC65C02: wait for interrupt (low cycle overhead)
    bra @halt

; ---------------------------------------------------------------------------
; Syscall: sys_task_create — create and register a new task
; Entry: A/X = code address (A=lo, X=hi), Y = task_id slot (0..3)
; Exit:  A=0 success, A=$FF error (slot occupied or invalid)
; ---------------------------------------------------------------------------

sys_task_create:
    ; Entry: A/X = code addr (A=lo, X=hi), Y = task_id slot (0..MAX_TASKS-1)
    sei

    ; Validate task_id range
    cpy #MAX_TASKS
    bcc @slot_ok
    jmp @err
@slot_ok:

    ; Save parameters: code addr lo→kzp_spare0, hi→kzp_spare1, slot→kzp_tmp2
    sta kzp_spare0
    stx kzp_spare1
    sty kzp_tmp2

    ; Point to TCB[Y] and check slot is free
    tya
    jsr set_tcb_ptr
    ldy #TCB_STATUS
    lda (kzp_tcb_lo),y
    cmp #TASK_DEAD
    beq @slot_free
    jmp @err
@slot_free:

    ; Allocate PAGE1 stack slot from pool
    jsr alloc_stack_slot    ; → A = slot (0–7) or $FF
    cmp #$FF
    bne @stk_ok
    jmp @err
@stk_ok:
    sta kzp_tmp3            ; save stack_slot

    ; Allocate ZP slot from pool
    jsr alloc_zp_slot       ; → A = slot (0–7) or $FF
    cmp #$FF
    bne @zp_ok
    jmp @err_free_stk       ; roll back stack slot
@zp_ok:
    pha                     ; save zp_slot on kernel stack

    ; Re-point to TCB (alloc functions use set_tcb_ptr internally)
    lda kzp_tmp2
    jsr set_tcb_ptr

    ; Clear TCB
    lda #0
    ldy #0
@clr:
    sta (kzp_tcb_lo),y
    iny
    cpy #TCB_SIZE
    bne @clr

    ; PC = code address
    ldy #TCB_PC_LO
    lda kzp_spare0
    sta (kzp_tcb_lo),y
    ldy #TCB_PC_HI
    lda kzp_spare1
    sta (kzp_tcb_lo),y

    ; task_id = Y parameter (kzp_tmp2)
    ldy #TCB_ID
    lda kzp_tmp2
    sta (kzp_tcb_lo),y

    ; Stack slot + ZP slot
    ldy #TCB_STSLOT
    lda kzp_tmp3
    sta (kzp_tcb_lo),y

    pla                     ; zp_slot from stack
    pha                     ; keep copy for later
    ldy #TCB_ZPSLOT
    sta (kzp_tcb_lo),y

    ; ZP base = lookup table zp_slot_base[zp_slot] (1B per slot)
    ; A still contains zp_slot
    tay
    lda zp_slot_base,y      ; lo byte of ZP base for this slot
    ldy #TCB_ZPBASE
    sta (kzp_tcb_lo),y

    ; SPINIT (lo byte of SP) = $1F + stack_slot * 32
    ; slot0=$1F, slot1=$3F, ..., slot7=$FF
    lda kzp_tmp3            ; stack_slot
    asl                     ; *2
    asl                     ; *4
    asl                     ; *8
    asl                     ; *16
    asl                     ; *32
    clc
    adc #$1F                ; + $1F (slot0 top lo byte)
    ldy #TCB_SPINIT
    sta (kzp_tcb_lo),y
    ldy #TCB_SP
    sta (kzp_tcb_lo),y

    ; Status = READY
    ldy #TCB_STATUS
    lda #TASK_READY
    sta (kzp_tcb_lo),y

    ; Update kzp_ntask if new maximum
    lda kzp_tmp2            ; task_id
    clc
    adc #1
    cmp kzp_ntask
    bcc @no_ntask_upd
    sta kzp_ntask
@no_ntask_upd:

    ; Build fake IRQ frame on task's PAGE1 stack slot
    ldy #TCB_SPINIT
    lda (kzp_tcb_lo),y  ; sp_init (top of stack slot)
    tax
    ldy #TCB_PC_HI
    lda (kzp_tcb_lo),y
    sta $0100,x         ; PChi @ sp_init
    dex
    ldy #TCB_PC_LO
    lda (kzp_tcb_lo),y
    sta $0100,x         ; PClo @ sp_init-1
    dex
    lda #$00            ; P=0 (I=0 after RTI → task starts with IRQs enabled)
    sta $0100,x         ; P   @ sp_init-2

    ; Update SP: sp_init - 3
    ldy #TCB_SPINIT
    lda (kzp_tcb_lo),y
    sec
    sbc #3
    ldy #TCB_SP
    sta (kzp_tcb_lo),y

    pla                 ; discard saved zp_slot copy
    cli
    lda #0
    rts

@err_free_stk:
    ; Free already-allocated stack slot before returning error
    lda kzp_tmp3
    jsr free_stack_slot
@err:
    cli
    lda #$FF
    rts

; ---------------------------------------------------------------------------
; Syscall: sys_task_kill — destroy a task
; Entry: A = task_id
; ---------------------------------------------------------------------------

sys_task_kill:
    cmp kzp_curr
    beq @self           ; cannot kill yourself this way — use sys_exit
    jsr set_tcb_ptr
    ldy #TCB_STATUS
    lda (kzp_tcb_lo),y
    cmp #TASK_DEAD
    beq @already_dead
    lda #TASK_DEAD
    sta (kzp_tcb_lo),y
    ; Free PAGE1 stack slot
    ldy #TCB_STSLOT
    lda (kzp_tcb_lo),y
    jsr free_stack_slot
    ; Free ZP slot
    ldy #TCB_ZPSLOT
    lda (kzp_tcb_lo),y
    jsr free_zp_slot
@already_dead:
    lda #0
    rts
@self:
    jmp sys_exit

; ---------------------------------------------------------------------------
; Syscall: sys_task_wait — wait for child task to finish
; Entry: A = child task_id
; ---------------------------------------------------------------------------

sys_task_wait:
    sta kzp_spare0
    lda kzp_curr
    sta kzp_next            ; save our own task_id
    jsr set_tcb_ptr
    ldy #TCB_STATUS
    lda #TASK_WAITING
    sta (kzp_tcb_lo),y
    ldy #TCB_WTYPE
    lda #WAIT_CHILD
    sta (kzp_tcb_lo),y
    ldy #TCB_WPARAM
    lda kzp_spare0
    sta (kzp_tcb_lo),y
    ; Wait for scheduler to wake us (check OUR OWN TCB, not kzp_curr)
    cli
@wait:
    wai
    lda kzp_next            ; our saved task_id
    jsr set_tcb_ptr
    ldy #TCB_STATUS
    lda (kzp_tcb_lo),y
    cmp #TASK_RUNNING
    bne @wait
    rts

; ---------------------------------------------------------------------------
; Syscall: sys_sleep_frames — sleep for N VSync frames
; Entry: A=lo, X=hi (frame count)
; ---------------------------------------------------------------------------

sys_sleep_frames:
    sta kzp_spare0
    stx kzp_spare1
    lda kzp_curr
    sta kzp_next            ; save our own task_id (kzp_next free here)
    jsr set_tcb_ptr
    ldy #TCB_STATUS
    lda #TASK_WAITING
    sta (kzp_tcb_lo),y
    ldy #TCB_WTYPE
    lda #WAIT_FRAMES
    sta (kzp_tcb_lo),y
    ldy #TCB_WPARAM
    lda kzp_spare0
    sta (kzp_tcb_lo),y
    ldy #TCB_WPARAM1
    lda kzp_spare1
    sta (kzp_tcb_lo),y
    ; Wait for scheduler to wake us: TASK_RUNNING means countdown elapsed and
    ; the IRQ restored us. While WAITING or READY, keep spinning.
    cli
@wait:
    wai
    lda kzp_next            ; our saved task_id
    jsr set_tcb_ptr
    ldy #TCB_STATUS
    lda (kzp_tcb_lo),y
    cmp #TASK_RUNNING
    bne @wait
    rts

; ---------------------------------------------------------------------------
; Syscall: sys_get_task_id — get current task ID
; Exit: A = task_id
; ---------------------------------------------------------------------------

sys_get_task_id:
    lda kzp_curr
    rts

; ---------------------------------------------------------------------------
; Syscall: sys_task_status — get task status
; Entry: A = task_id
; Exit:  A = status, X = wait_type
; ---------------------------------------------------------------------------

sys_task_status:
    jsr set_tcb_ptr
    ldy #TCB_STATUS
    lda (kzp_tcb_lo),y
    pha
    ldy #TCB_WTYPE
    lda (kzp_tcb_lo),y
    tax
    pla
    rts

; ---------------------------------------------------------------------------
; Syscall: sys_io_lock — lock preemption during RIA call
; Exit: none
; ---------------------------------------------------------------------------

sys_io_lock:
    lda kzp_iflags
    ora #IFLAGS_IO_LOCK
    sta kzp_iflags
    rts

; ---------------------------------------------------------------------------
; Syscall: sys_io_unlock — unlock preemption after RIA call
; ---------------------------------------------------------------------------

sys_io_unlock:
    lda kzp_iflags
    and #(.BITNOT IFLAGS_IO_LOCK & $FF)
    sta kzp_iflags
    rts

; ---------------------------------------------------------------------------
; Bitmask table for bit N (used by alloc/free pool functions)
bit_mask:
    .byte %00000001, %00000010, %00000100, %00001000
    .byte %00010000, %00100000, %01000000, %10000000

; alloc_stack_slot — allocate one PAGE1 stack slot from pool
; Exit: A = slot (0–7) or $FF (pool full). Clobbers: A, X, Y.
; ---------------------------------------------------------------------------

alloc_stack_slot:
    lda stack_pool_free
    bne @scan
    lda #$FF            ; pool empty
    rts
@scan:
    ldx #0
@bit: lsr
    bcs @found
    inx
    bra @bit
@found:
    ; clear bit X in stack_pool_free
    lda bit_mask,x
    eor #$FF
    and stack_pool_free
    sta stack_pool_free
    txa
    rts

; ---------------------------------------------------------------------------
; free_stack_slot — return PAGE1 stack slot to pool
; Entry: A = slot (0–7). Clobbers: A, X.
; ---------------------------------------------------------------------------

free_stack_slot:
    tax
    lda bit_mask,x
    ora stack_pool_free
    sta stack_pool_free
    rts

; ---------------------------------------------------------------------------
; alloc_zp_slot — allocate one ZP slice slot from pool
; Exit: A = slot (0–7) or $FF. Clobbers: A, X.
; ---------------------------------------------------------------------------

alloc_zp_slot:
    lda zp_pool_free
    bne @scan
    lda #$FF
    rts
@scan:
    ldx #0
@bit: lsr
    bcs @found
    inx
    bra @bit
@found:
    lda bit_mask,x
    eor #$FF
    and zp_pool_free
    sta zp_pool_free
    txa
    rts

; ---------------------------------------------------------------------------
; free_zp_slot — return ZP slot to pool
; Entry: A = slot (0–7). Clobbers: A, X.
; ---------------------------------------------------------------------------

free_zp_slot:
    tax
    lda bit_mask,x
    ora zp_pool_free
    sta zp_pool_free
    rts

; ---------------------------------------------------------------------------
; via_compute_latch — compute VIA T1 latch and IRQ divider for target Hz
; Input:  via_phi2_lo/hi (kHz), via_irq_hz_lo/hi (Hz, 1–1000)
; Output: via_t1_latch_lo/hi, via_irq_divider, via_irq_tick set in KDATA
; Pure 6502, no RIA calls.
; Clobbers: A, X, Y, api_zp2..5, kzp_spare0/1, kzp_tmp2/3
; ---------------------------------------------------------------------------

via_compute_latch:
    ; Step 1: kHz*1000 into api_zp2(lo)/api_zp3(mid)/api_zp4(hi), 24-bit
    ; kHz*1000 = kHz*(8+32+64+128+256+512)
    ; spare = kzp_spare0/1 (16-bit shift register), kzp_tmp3 = spare overflow byte
    lda via_phi2_lo
    sta kzp_spare0
    lda via_phi2_hi
    sta kzp_spare1
    stz kzp_tmp3            ; spare overflow
    stz api_zp2
    stz api_zp3
    stz api_zp4
    ; *8 (3 shifts — spare stays 16-bit, no overflow yet)
    asl kzp_spare0
    rol kzp_spare1
    asl kzp_spare0
    rol kzp_spare1
    asl kzp_spare0
    rol kzp_spare1
    clc
    lda api_zp2
    adc kzp_spare0
    sta api_zp2
    lda api_zp3
    adc kzp_spare1
    sta api_zp3
    lda api_zp4
    adc kzp_tmp3
    sta api_zp4
    ; *32 (2 more shifts, total 5 — spare may overflow into kzp_tmp3)
    asl kzp_spare0
    rol kzp_spare1
    rol kzp_tmp3
    asl kzp_spare0
    rol kzp_spare1
    rol kzp_tmp3
    clc
    lda api_zp2
    adc kzp_spare0
    sta api_zp2
    lda api_zp3
    adc kzp_spare1
    sta api_zp3
    lda api_zp4
    adc kzp_tmp3
    sta api_zp4
    ; *64 (1 more shift, total 6)
    asl kzp_spare0
    rol kzp_spare1
    rol kzp_tmp3
    clc
    lda api_zp2
    adc kzp_spare0
    sta api_zp2
    lda api_zp3
    adc kzp_spare1
    sta api_zp3
    lda api_zp4
    adc kzp_tmp3
    sta api_zp4
    ; *128 (1 more shift, total 7)
    asl kzp_spare0
    rol kzp_spare1
    rol kzp_tmp3
    clc
    lda api_zp2
    adc kzp_spare0
    sta api_zp2
    lda api_zp3
    adc kzp_spare1
    sta api_zp3
    lda api_zp4
    adc kzp_tmp3
    sta api_zp4
    ; *256 (1 more shift, total 8)
    asl kzp_spare0
    rol kzp_spare1
    rol kzp_tmp3
    clc
    lda api_zp2
    adc kzp_spare0
    sta api_zp2
    lda api_zp3
    adc kzp_spare1
    sta api_zp3
    lda api_zp4
    adc kzp_tmp3
    sta api_zp4
    ; *512 (1 more shift, total 9)
    asl kzp_spare0
    rol kzp_spare1
    rol kzp_tmp3
    clc
    lda api_zp2
    adc kzp_spare0
    sta api_zp2
    lda api_zp3
    adc kzp_spare1
    sta api_zp3
    lda api_zp4
    adc kzp_tmp3
    sta api_zp4
    ; api_zp2/3/4 = kHz*1000 (24-bit); kzp_tmp3 now free (spare done)

    ; Step 2: 24÷16 restoring division
    ; Dividend: api_zp2(lo)/api_zp3(mid)/api_zp4(hi)  — 24-bit
    ; Divisor:  via_irq_hz_lo(lo)/via_irq_hz_hi(hi)   — 16-bit
    ; Quotient: kzp_spare0(lo)/kzp_spare1(hi)/kzp_tmp3(overflow) — 24-bit
    ; Remainder: api_zp5(lo)/kzp_tmp2(hi) — discarded
    stz kzp_spare0          ; quotient = 0
    stz kzp_spare1
    stz kzp_tmp3
    stz api_zp5             ; remainder = 0
    stz kzp_tmp2
    ldy #24
@div_bit:
    asl api_zp2
    rol api_zp3
    rol api_zp4
    rol api_zp5
    rol kzp_tmp2
    lda api_zp5
    sec
    sbc via_irq_hz_lo
    tax
    lda kzp_tmp2
    sbc via_irq_hz_hi
    bcc @no_sub
    stx api_zp5
    sta kzp_tmp2
    sec
    bra @shift_q
@no_sub:
    clc
@shift_q:
    rol kzp_spare0
    rol kzp_spare1
    rol kzp_tmp3
    dey
    bne @div_bit
    ; quotient in kzp_spare0/1/tmp3 (24-bit)

    ; Step 3: scale quotient down to 16 bits, tracking D (divider)
    ; If quotient > $FFFF, shift right and double D until it fits.
    lda #1
    sta kzp_tmp2            ; D = 1
@scale:
    lda kzp_tmp3
    beq @done               ; overflow byte = 0: quotient fits in 16 bits
    lsr kzp_tmp3
    ror kzp_spare1
    ror kzp_spare0
    asl kzp_tmp2            ; D <<= 1
    bra @scale
@done:
    lda kzp_spare0
    sta via_t1_latch_lo
    lda kzp_spare1
    sta via_t1_latch_hi
    lda kzp_tmp2
    sta via_irq_divider
    sta via_irq_tick
    rts

sys_set_phi2:
    sei
    ; Save kHz
    sta via_phi2_lo
    stx via_phi2_hi

    ; Call RIA ATTR_SET for PHI2_KHZ
    ; Value = kHz (16-bit) in api_zp2/3, api_zp4/5=0
    lda via_phi2_lo
    sta api_zp2
    sta RIA_A
    lda via_phi2_hi
    sta api_zp3
    sta RIA_X
    stz api_zp4
    stz api_zp5
    lda #0
    sta RIA_SREG
    sta RIA_SREG+1
    lda #ATTR_PHI2_KHZ
    sta RIA_XSTACK
    lda #OP_ATTR_SET
    sta RIA_OP
    jsr RIA_SPIN

    ; Reload kHz from KDATA (via_phi2_lo/hi already set above)
    ; Recompute latch
    jsr via_compute_latch

    ; Reprogram VIA Timer1 with new latch (write latch then reload counter)
    lda via_t1_latch_lo
    sta VIA_T1LL
    lda via_t1_latch_hi
    sta VIA_T1LH            ; update latch
    sta VIA_T1CH            ; reload counter immediately
    lda via_irq_divider
    sta via_irq_tick        ; reset tick countdown

    cli
    lda #0
    rts

; ---------------------------------------------------------------------------
; sys_set_irqfreq — set context-switch frequency
; Input: A=Hz lo, X=Hz hi (1–1000)
; Reprograms VIA T1 latch; new frequency takes effect immediately
; ---------------------------------------------------------------------------

sys_set_irqfreq:
    sei
    sta via_irq_hz_lo
    stx via_irq_hz_hi
    jsr via_compute_latch
    lda via_t1_latch_lo
    sta VIA_T1LL
    lda via_t1_latch_hi
    sta VIA_T1LH            ; update latch
    sta VIA_T1CH            ; reload counter immediately (starts new period)
    lda via_irq_divider
    sta via_irq_tick        ; reset tick countdown
    cli
    lda #0
    rts

; ---------------------------------------------------------------------------
; SEGMENT KDATA — kernel variables in RAM
; ---------------------------------------------------------------------------

.segment "KDATA"

; PAGE1 stack slot pool bitmap: bit N=1 → slot N is free
; TASK0 is pre-allocated (slot 0 taken), so bit0=0 at init.
; Initialized at runtime in kernel_init.
stack_pool_free: .byte 0

; ZP slot pool bitmap: bit N=1 → slot N is free
; TASK0 pre-allocated (slot 0 taken), bit0=0 at init.
zp_pool_free: .byte 0

; ZP slice base address (lo byte) per slot index
; slot N base = $0028 + N*26
zp_slot_base:
    .byte $28   ; slot 0: $0028
    .byte $42   ; slot 1: $0042
    .byte $5C   ; slot 2: $005C
    .byte $76   ; slot 3: $0076
    .byte $90   ; slot 4: $0090
    .byte $AA   ; slot 5: $00AA
    .byte $C4   ; slot 6: $00C4
    .byte $DE   ; slot 7: $00DE

; VIA Timer1 state (updated by via_compute_latch / sys_set_phi2 / sys_set_irqfreq)
via_phi2_lo:     .byte 0     ; current CPU clock lo byte (kHz)
via_phi2_hi:     .byte 0     ; current CPU clock hi byte (kHz)
via_t1_latch_lo: .byte 0     ; Timer1 latch lo byte
via_t1_latch_hi: .byte 0     ; Timer1 latch hi byte
via_irq_divider: .byte 1     ; number of T1 ticks per context switch
via_irq_tick:    .byte 1     ; countdown; switch when reaches 0
via_irq_hz_lo:   .byte 60    ; target IRQ/context-switch frequency lo byte (Hz)
via_irq_hz_hi:   .byte 0     ; target IRQ/context-switch frequency hi byte (Hz)

; Reserved for future kernel data
.res 238, $00

; ---------------------------------------------------------------------------
; SEGMENT TCBAREA — Task Control Blocks
; 4 × 64 bytes = 256 bytes
; ---------------------------------------------------------------------------

.segment "TCBAREA"

tcb_area:
    .res 512, $00

; ---------------------------------------------------------------------------
; SEGMENT VECTORS — 6502 hardware vectors $FFFA–$FFFF
; NMI=$FFFA, RESET=$FFFC, IRQ=$FFFE
; rp6502_executable sets RESET=$0260; we add IRQ=irq_handler here so the
; standard 6502 IRQ mechanism (CPU_IRQB assert by RIA on VSync) dispatches
; directly to our handler.
; Note: rp6502_executable will write RESET from cmake; VECTORS segment
; provides IRQ and NMI as .word entries. The cmake RESET overrides our
; .word below for RESET, but IRQ/NMI are ours.
; ---------------------------------------------------------------------------

; ---------------------------------------------------------------------------
; EOF kernel.s
; ---------------------------------------------------------------------------
