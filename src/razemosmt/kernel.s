;
; kernel.s — razemOSmt kernel
; Multitasking OS for WDC65C02S / Picocomputer RP6502
;
; Memory layout:
;   $0200–$025F  JUMPTABLE  — stable ABI vectors (32 × JMP abs)
;   $0260–$0A5F  KERNEL     — IRQ handler, scheduler, syscalls, ria wrappers
;   $0A60–$0B5F  KDATA      — kernel variables
;   $0B60–$0C5F  TCBAREA    — 4 × TCB (Task Control Block, 64 bytes each)
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

; ---------------------------------------------------------------------------
; TCB layout — offsets (TCB = 64 bytes, base = $0D60 + task_id*64)
; ---------------------------------------------------------------------------

TCB_BASE    = $0D60
TCB_SIZE    = 64        ; power-of-2 → indexed by 6x ASL

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
TCB_SPINIT  = 13        ; +13 initial SP value
TCB_FCNT_L  = 14        ; +14 frame counter lo
TCB_FCNT_H  = 15        ; +15 frame counter hi
TCB_LADDR_L = 16        ; +16 load address lo
TCB_LADDR_H = 17        ; +17 load address hi
TCB_MSIZE_L = 18        ; +18 RAM size lo
TCB_MSIZE_H = 19        ; +19 RAM size hi
TCB_NAME    = 20        ; +20 name (8 bytes: 7 chars + null)
TCB_ZP_SNAP = 28        ; +28 ZP$0000–$0019 snapshot (26 bytes)
                        ; +54 reserved (10 bytes)
                        ; = 64 bytes total

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

; PAGE1 stack top per task
STACK_TASK0_TOP = $3F   ; TASK0: $0100–$013F, SP init=$013F
STACK_TASK1_TOP = $7F   ; TASK1: $0140–$017F, SP init=$017F
STACK_TASK2_TOP = $BF   ; TASK2: $0180–$01BF, SP init=$01BF
STACK_KERNEL_SP = $FF   ; Kernel/TASK3: $01C0–$01FF, SP init=$01FF

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
jt_mth_mul8:          jmp mth_mul8            ; $0257 [28]
jt_mth_div16:         jmp mth_div16           ; $025A [29]
jt_xpush_byte:        jmp xpush_byte          ; $025D [30]

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

TASK0_ENTRY = $0E60     ; cc65 crt0 __STARTUP__ — must match shell.cfg __STARTADDR__

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

    ; Clear TCBAREA ($0D60–$0E5F): 256 bytes, page-crossing.
    ; $0D60–$0DFF: 160 bytes (Y from $60 to $FF, wraps to 0)
    ; $0E00–$0E5F:  96 bytes (Y from $00 to $5F)
    ; MUST stop before $0E60 = TASK0 entry point.
    lda #<TCB_BASE       ; $60
    sta kzp_tcb_lo
    lda #>TCB_BASE       ; $0D
    sta kzp_tcb_hi
    lda #0
    ldy #<TCB_BASE       ; Y = $60 — start of TCBAREA within its page
@clr_tcb:
    sta (kzp_tcb_lo),y
    iny
    bne @clr_tcb         ; stop when Y wraps to 0 (wrote $60–$FF = 160 bytes)
    ; Y=0 now; second page starts at $0E00: set ptr lo=$00, hi=$0E
    lda #0
    sta kzp_tcb_lo       ; base ptr = $0E00
    inc kzp_tcb_hi       ; kzp_tcb_hi = $0E
@clr_tcb2:
    sta (kzp_tcb_lo),y
    iny
    cpy #$60             ; stop when Y=$60 → $0E00+$60=$0E60 (TASK0 entry)
    bne @clr_tcb2

    ; Initialize VSYNC shadow
    lda RIA_VSYNC
    sta kzp_vsync

    ; Register IRQ handler with RIA ($FFF0–$FFF1)
    lda #<irq_handler
    sta RIA_IRQ
    lda #>irq_handler
    sta RIA_IRQ+1

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

    ldy #TCB_SPINIT
    lda #STACK_TASK0_TOP    ; $3F
    sta (kzp_tcb_lo),y
    ldy #TCB_SP
    sta (kzp_tcb_lo),y      ; SP saved = $3F (stack is empty at entry)

    ldy #TCB_STATUS
    lda #TASK_RUNNING       ; TASK0 starts as RUNNING
    sta (kzp_tcb_lo),y

    lda #1
    sta kzp_ntask           ; one task registered

    ; Enable preemption
    lda #SCHED_PREEMPT_EN
    sta kzp_sched

    ; Set SP to TASK0 partition ($013F)
    ldx #STACK_TASK0_TOP
    txs

    ; Enable interrupts — VSync IRQ can fire from this point on
    cli

    ; Jump to TASK0 entry.
    ; IRQ handler will save/restore TASK0 context correctly from the first VSync.
    jmp TASK0_ENTRY

; ---------------------------------------------------------------------------
; set_tcb_ptr — set kzp_tcb_lo/hi to TCB[A]
; Entry: A = task_id (0..3)
; Clobbers: A, kzp_tcb_lo, kzp_tcb_hi
; ---------------------------------------------------------------------------

set_tcb_ptr:
    ; TCB_BASE + task_id * 64
    ; task_id * 64: 6x ASL (max A=3 → 3*64=192, fits in 8 bits)
    asl             ; *2
    asl             ; *4
    asl             ; *8
    asl             ; *16
    asl             ; *32
    asl             ; *64
    clc
    adc #<TCB_BASE
    sta kzp_tcb_lo
    lda #>TCB_BASE
    adc #0          ; carry from low byte
    sta kzp_tcb_hi
    rts

; ---------------------------------------------------------------------------
; IRQ Handler — context switch on VSync
; CPU auto-pushes on IRQ: PChi, PClo, P (I=1)
; ---------------------------------------------------------------------------

.export irq_handler

irq_handler:
    ; --- 1. Atomic save A, X, Y ---
    sta kzp_tmp0
    stx kzp_tmp1
    sty kzp_tmp2

    ; --- 2. Check for new VSync frame ---
    lda RIA_VSYNC
    cmp kzp_vsync
    bne @vsync_new
    jmp @spurious       ; same counter → spurious IRQ, ignore
@vsync_new:
    sta kzp_vsync       ; update shadow

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

    ; Current task SP: CPU pushed 3 bytes (PChi, PClo, P) before IRQ entry,
    ; so current SP = SP_before_IRQ - 3. We save SP_before_IRQ.
    tsx
    txa
    clc
    adc #3
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

    ; --- 7. Save ZP$0000–$0019 to TCB+28 (26 bytes, unrolled) ---
    ; Y is used as TCB offset; addressing via (kzp_tcb_lo),y
    ldy #TCB_ZP_SNAP
    lda $00
    sta (kzp_tcb_lo),y
    iny
    lda $01
    sta (kzp_tcb_lo),y
    iny
    lda $02
    sta (kzp_tcb_lo),y
    iny
    lda $03
    sta (kzp_tcb_lo),y
    iny
    lda $04
    sta (kzp_tcb_lo),y
    iny
    lda $05
    sta (kzp_tcb_lo),y
    iny
    lda $06
    sta (kzp_tcb_lo),y
    iny
    lda $07
    sta (kzp_tcb_lo),y
    iny
    lda $08
    sta (kzp_tcb_lo),y
    iny
    lda $09
    sta (kzp_tcb_lo),y
    iny
    lda $0A
    sta (kzp_tcb_lo),y
    iny
    lda $0B
    sta (kzp_tcb_lo),y
    iny
    lda $0C
    sta (kzp_tcb_lo),y
    iny
    lda $0D
    sta (kzp_tcb_lo),y
    iny
    lda $0E
    sta (kzp_tcb_lo),y
    iny
    lda $0F
    sta (kzp_tcb_lo),y
    iny
    lda $10
    sta (kzp_tcb_lo),y
    iny
    lda $11
    sta (kzp_tcb_lo),y
    iny
    lda $12
    sta (kzp_tcb_lo),y
    iny
    lda $13
    sta (kzp_tcb_lo),y
    iny
    lda $14
    sta (kzp_tcb_lo),y
    iny
    lda $15
    sta (kzp_tcb_lo),y
    iny
    lda $16
    sta (kzp_tcb_lo),y
    iny
    lda $17
    sta (kzp_tcb_lo),y
    iny
    lda $18
    sta (kzp_tcb_lo),y
    iny
    lda $19
    sta (kzp_tcb_lo),y  ; last — no iny

    ; --- 8. Set current task status = READY ---
    ldy #TCB_STATUS
    lda #TASK_READY
    sta (kzp_tcb_lo),y

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

    ; --- 10. Switch SP to kernel stack ($01FF) ---
    ldx #STACK_KERNEL_SP
    txs

    ; --- 11. Run scheduler — result in kzp_next ---
    jsr kernel_scheduler

    ; --- 12. Set kzp_curr = kzp_next ---
    lda kzp_next
    sta kzp_curr

    ; --- 13. Point TCB pointer to new task ---
    jsr set_tcb_ptr

    ; --- 14. Mark new task as RUNNING ---
    ldy #TCB_STATUS
    lda #TASK_RUNNING
    sta (kzp_tcb_lo),y

    ; --- 15. Restore ZP$0000–$0019 from new task TCB ---
    ldy #TCB_ZP_SNAP
    lda (kzp_tcb_lo),y
    sta $00
    iny
    lda (kzp_tcb_lo),y
    sta $01
    iny
    lda (kzp_tcb_lo),y
    sta $02
    iny
    lda (kzp_tcb_lo),y
    sta $03
    iny
    lda (kzp_tcb_lo),y
    sta $04
    iny
    lda (kzp_tcb_lo),y
    sta $05
    iny
    lda (kzp_tcb_lo),y
    sta $06
    iny
    lda (kzp_tcb_lo),y
    sta $07
    iny
    lda (kzp_tcb_lo),y
    sta $08
    iny
    lda (kzp_tcb_lo),y
    sta $09
    iny
    lda (kzp_tcb_lo),y
    sta $0A
    iny
    lda (kzp_tcb_lo),y
    sta $0B
    iny
    lda (kzp_tcb_lo),y
    sta $0C
    iny
    lda (kzp_tcb_lo),y
    sta $0D
    iny
    lda (kzp_tcb_lo),y
    sta $0E
    iny
    lda (kzp_tcb_lo),y
    sta $0F
    iny
    lda (kzp_tcb_lo),y
    sta $10
    iny
    lda (kzp_tcb_lo),y
    sta $11
    iny
    lda (kzp_tcb_lo),y
    sta $12
    iny
    lda (kzp_tcb_lo),y
    sta $13
    iny
    lda (kzp_tcb_lo),y
    sta $14
    iny
    lda (kzp_tcb_lo),y
    sta $15
    iny
    lda (kzp_tcb_lo),y
    sta $16
    iny
    lda (kzp_tcb_lo),y
    sta $17
    iny
    lda (kzp_tcb_lo),y
    sta $18
    iny
    lda (kzp_tcb_lo),y
    sta $19  ; last — no iny

    ; --- 16. Restore new task SP ---
    ldy #TCB_SP
    lda (kzp_tcb_lo),y
    tax
    txs

    ; --- 17. Restore A, X, Y via kzp_tmp ---
    ldy #TCB_A
    lda (kzp_tcb_lo),y
    sta kzp_tmp0
    ldy #TCB_X
    lda (kzp_tcb_lo),y
    sta kzp_tmp1
    ldy #TCB_Y
    lda (kzp_tcb_lo),y
    sta kzp_tmp2

    ; --- 18. Clear in_irq flag ---
    lda kzp_iflags
    and #(.BITNOT IFLAGS_IN_IRQ & $FF)
    sta kzp_iflags

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
    ; --- Phase 1: wake WAITING tasks ---
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

    ; --- Phase 2: round-robin from (curr+1) AND 3 ---
    lda kzp_curr
    clc
    adc #1
    and #3
    sta kzp_next        ; starting candidate

    ldx #0              ; attempt counter (max 4)
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
    and #3
    sta kzp_next
    inx
    cpx #4
    bne @rr_loop

    ; No READY task found → continue current task
    lda kzp_curr
    sta kzp_next

    ; Mark current task RUNNING again (stays)
    jsr set_tcb_ptr
    ldy #TCB_STATUS
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
    ; Validate task_id range
    cpy #4
    bcc @slot_ok
    jmp @err
@slot_ok:

    ; Save parameters
    sta kzp_spare0      ; code address lo
    stx kzp_spare1      ; code address hi

    ; Point to TCB[Y]
    tya
    jsr set_tcb_ptr

    ; Check if slot is free
    ldy #TCB_STATUS
    lda (kzp_tcb_lo),y
    cmp #TASK_DEAD
    bne @err

    ; Clear TCB
    lda #0
    ldy #0
@clr:
    sta (kzp_tcb_lo),y
    iny
    cpy #TCB_SIZE
    bne @clr

    ; Set TCB fields
    ; PC = code address
    ldy #TCB_PC_LO
    lda kzp_spare0
    sta (kzp_tcb_lo),y
    ldy #TCB_PC_HI
    lda kzp_spare1
    sta (kzp_tcb_lo),y

    ; task_id: calculate from TCB address: (kzp_tcb_lo - TCB_BASE) / 64
    lda kzp_tcb_lo
    sec
    sbc #<TCB_BASE
    ; result in A, divide by 64 = 6x LSR
    lsr
    lsr
    lsr
    lsr
    lsr
    lsr
    ldy #TCB_ID
    sta (kzp_tcb_lo),y

    ; SP init: based on task_id
    tax                 ; save task_id
    lda kzp_tcb_lo
    sec
    sbc #<TCB_BASE
    lsr
    lsr
    lsr
    lsr
    lsr
    lsr                 ; task_id in A
    ; sp_init table: $3F, $7F, $BF, $FF
    asl
    asl
    asl
    asl
    asl
    asl                 ; task_id * 64
    clc
    adc #$3F            ; base: $013F for task0
    ldy #TCB_SPINIT
    sta (kzp_tcb_lo),y
    ldy #TCB_SP
    sta (kzp_tcb_lo),y

    ; Set status = READY
    ldy #TCB_STATUS
    lda #TASK_READY
    sta (kzp_tcb_lo),y

    ; Update kzp_ntask if new maximum
    txa                 ; task_id
    clc
    adc #1
    cmp kzp_ntask
    bcc @no_ntask_upd
    sta kzp_ntask
@no_ntask_upd:

    ; Build fake IRQ frame on task PAGE1 stack
    ; PAGE1 stack for task Y: addresses $0100 + (sp_init[Y] - 2)..$0100 + sp_init[Y]
    ; Push order (CPU on IRQ): PChi, PClo, P
    ; On stack (from lower address): P, PClo, PChi
    ldy #TCB_SPINIT
    lda (kzp_tcb_lo),y  ; sp_init
    tax
    ldy #TCB_PC_HI
    lda (kzp_tcb_lo),y
    sta $0100,x         ; PChi @ sp_init
    dex
    ldy #TCB_PC_LO
    lda (kzp_tcb_lo),y
    sta $0100,x         ; PClo @ sp_init-1
    dex
    lda #$00            ; P = 0 (I=0 after RTI — task starts with IRQs enabled)
    sta $0100,x         ; P   @ sp_init-2

    ; Update SP in TCB: sp_init - 3 (value of SP after CPU IRQ push)
    ldy #TCB_SPINIT
    lda (kzp_tcb_lo),y
    sec
    sbc #3
    ldy #TCB_SP
    sta (kzp_tcb_lo),y

    lda #0
    rts

@err:
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
    lda #TASK_DEAD
    sta (kzp_tcb_lo),y
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
    ; Wait for IRQ
    cli
@wait:
    wai
    lda kzp_curr
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
    ; Wait for scheduler to wake us
    cli
@wait:
    wai
    lda kzp_curr
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
; SEGMENT KDATA — kernel variables in RAM
; ---------------------------------------------------------------------------

.segment "KDATA"

; Initial SP value table per task_id
k_sp_init_table:
    .byte STACK_TASK0_TOP   ; task0: $3F
    .byte STACK_TASK1_TOP   ; task1: $7F
    .byte STACK_TASK2_TOP   ; task2: $BF
    .byte STACK_KERNEL_SP   ; task3: $FF

; Reserved for future kernel data (e.g. task names, per-task fd table)
.res 252, $00

; ---------------------------------------------------------------------------
; SEGMENT TCBAREA — Task Control Blocks
; 4 × 64 bytes = 256 bytes
; ---------------------------------------------------------------------------

.segment "TCBAREA"

tcb_area:
    .res 256, $00

; ---------------------------------------------------------------------------
; EOF kernel.s
; ---------------------------------------------------------------------------
