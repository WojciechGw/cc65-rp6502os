;
; kernel.inc — razemOSmt kernel ABI
; Fixed jump table addresses for user programs and tasks
;
; Usage (ca65):
;   .include "kernel.inc"
;   JSR KERN_YIELD
;   JSR KERN_OPEN
;
; Usage (C via cc65) — through thin ASM wrapper or inline:
;   extern void __fastcall__ kern_yield(void);
;
; Calling convention (identical to ria_api.s):
;   Entry : A=lo, X=hi (16-bit); api_zp0/1=pointer; api_zp2..5=32-bit; Y=preserved
;   Exit  : A=lo, X=hi; api_zp2..5=32-bit; Y=always preserved
;   Error : A/X=$FFFF, RIA_ERRNO=$FFED
;

.ifndef KERNEL_INC
KERNEL_INC = 1

; ---------------------------------------------------------------------------
; Jump table base — NEVER changes
; ---------------------------------------------------------------------------

KERNEL_BASE = $0200

; ---------------------------------------------------------------------------
; Kernel entry points and memory area addresses
; ---------------------------------------------------------------------------

KERNEL_INIT    = $0260     ; CPU RESET entry point — kernel_init (start of KCODE)
KERNEL_KDATA   = $0D00     ; start of kernel data (KDATA)
KERNEL_ZP_SLOT_BASE = $0D02 ; zp_slot_base[0..7]: ZP base lo per task slot
KERNEL_TCBBASE = $0E00     ; start of Task Control Blocks (TCBAREA, 8x64B)
KERNEL_END     = $1000     ; first free address after kernel (TASK0 start)

; ---------------------------------------------------------------------------
; Kernel syscalls (own kernel logic)
; ---------------------------------------------------------------------------

KERN_YIELD          = KERNEL_BASE + $00     ; [ 0] $0200 — yield CPU (voluntary)
                                            ;       entry: none, exit: none
KERN_EXIT           = KERNEL_BASE + $03     ; [ 1] $0203 — terminate task
                                            ;       entry: A=exit_code, does not return
KERN_TASK_CREATE    = KERNEL_BASE + $06     ; [ 2] $0206 — create task
                                            ;       entry: A/X=code_addr, Y=slot(0..7)
                                            ;       exit:  A=0 OK, A=$FF error
KERN_TASK_KILL      = KERNEL_BASE + $09     ; [ 3] $0209 — kill task
                                            ;       entry: A=task_id
KERN_TASK_WAIT      = KERNEL_BASE + $0C     ; [ 4] $020C — wait for child to finish
                                            ;       entry: A=child_task_id
KERN_SLEEP_FRAMES   = KERNEL_BASE + $0F     ; [ 5] $020F — sleep N VSync frames (60Hz)
                                            ;       entry: A=lo, X=hi (frame count)
KERN_GET_TASK_ID    = KERNEL_BASE + $12     ; [ 6] $0212 — get current task ID
                                            ;       exit:  A=task_id (0..7)
KERN_TASK_STATUS    = KERNEL_BASE + $15     ; [ 7] $0215 — task status
                                            ;       entry: A=task_id
                                            ;       exit:  A=status, X=wait_type
KERN_IO_LOCK        = KERNEL_BASE + $18     ; [ 8] $0218 — lock preemption (RIA call)
KERN_IO_UNLOCK      = KERNEL_BASE + $1B     ; [ 8+] $021B — unlock preemption

; ---------------------------------------------------------------------------
; RIA wrappers — file operations
; ---------------------------------------------------------------------------

KERN_OPEN           = KERNEL_BASE + $1E     ; [ 9] $021E — ria_open
                                            ;       entry: api_zp0/1=name, A/X=flags
                                            ;       exit:  A/X=fd or $FFFF
KERN_CLOSE          = KERNEL_BASE + $21     ; [10] $0221 — ria_close
                                            ;       entry: A/X=fd
KERN_READ           = KERNEL_BASE + $24     ; [11] $0224 — ria_read_buf
                                            ;       entry: api_zp0/1=buffer, A/X=count, api_zp2=fd
                                            ;       exit:  A/X=bytes read
KERN_WRITE          = KERNEL_BASE + $27     ; [12] $0227 — ria_write_buf
                                            ;       entry: api_zp0/1=buffer, A/X=count, api_zp2=fd
                                            ;       exit:  A/X=bytes written
KERN_LSEEK          = KERNEL_BASE + $2A     ; [13] $022A — ria_lseek
                                            ;       entry: api_zp2..5=offset, A=whence, api_zp6=fd
KERN_UNLINK         = KERNEL_BASE + $2D     ; [14] $022D — ria_unlink
                                            ;       entry: api_zp0/1=path
KERN_RENAME         = KERNEL_BASE + $30     ; [15] $0230 — ria_rename
                                            ;       entry: api_zp0/1=old, api_zp6/7=new

; ---------------------------------------------------------------------------
; RIA wrappers — directory operations
; ---------------------------------------------------------------------------

KERN_OPENDIR        = KERNEL_BASE + $33     ; [16] $0233 — ria_opendir
                                            ;       entry: api_zp0/1=path
                                            ;       exit:  A/X=dirdes or $FFFF
KERN_READDIR        = KERNEL_BASE + $36     ; [17] $0236 — ria_readdir
                                            ;       entry: A/X=dirdes
                                            ;       exit:  A/X=0 OK, f_stat data via XSTACK
KERN_CLOSEDIR       = KERNEL_BASE + $39     ; [18] $0239 — ria_closedir
                                            ;       entry: A/X=dirdes
KERN_MKDIR          = KERNEL_BASE + $3C     ; [19] $023C — ria_mkdir
                                            ;       entry: api_zp0/1=path
KERN_CHDIR          = KERNEL_BASE + $3F     ; [20] $023F — ria_chdir
                                            ;       entry: api_zp0/1=path
KERN_GETCWD         = KERNEL_BASE + $42     ; [21] $0242 — ria_getcwd
                                            ;       entry: api_zp0/1=buffer, A/X=size

; ---------------------------------------------------------------------------
; RIA wrappers — UART
; ---------------------------------------------------------------------------

KERN_PUTC           = KERNEL_BASE + $45     ; [22] $0245 — uart_putc (blocking)
                                            ;       entry: A=char
KERN_PUTS           = KERNEL_BASE + $48     ; [23] $0248 — uart_puts (zero-terminated)
                                            ;       entry: A/X=string address
KERN_GETC           = KERNEL_BASE + $4B     ; [24] $024B — uart_getc (blocking)
                                            ;       exit:  A=char
KERN_GETC_NB        = KERNEL_BASE + $4E     ; [25] $024E — uart_getc_nb (non-blocking)
                                            ;       exit:  A=char, C=0 got / C=1 none

; ---------------------------------------------------------------------------
; RIA wrappers — clock
; ---------------------------------------------------------------------------

KERN_CLOCK          = KERNEL_BASE + $51     ; [26] $0251 — ria_clock
                                            ;       exit:  api_zp2..5=ticks 32-bit
KERN_CLOCK_GETTIME  = KERNEL_BASE + $54     ; [27] $0254 — ria_clock_gettime
                                            ;       exit:  api_zp2..5=unix_sec 32-bit

KERN_SYNCFS         = KERNEL_BASE + $57     ; [28] $0257 — ria_syncfs (sync filesystem)
KERN_SET_IRQFREQ    = KERNEL_BASE + $5A     ; [29] $025A — set IRQ/context-switch frequency
                                            ;       entry: A=lo, X=hi (Hz, 1–1000)
KERN_SET_PHI2       = KERNEL_BASE + $5D     ; [30] $025D — set PHI2 clock + reprogram VIA T1
                                            ;       entry: A=lo, X=hi (kHz, 100–8000)

; ---------------------------------------------------------------------------
; Task status constants (for use with KERN_TASK_STATUS)
; ---------------------------------------------------------------------------

TASK_DEAD    = 0
TASK_READY   = 1
TASK_RUNNING = 2
TASK_WAITING = 3

WAIT_NONE    = 0
WAIT_FRAMES  = 1
WAIT_IO_R    = 2
WAIT_IO_W    = 3
WAIT_CHILD   = 4

; ---------------------------------------------------------------------------
; open() flags (redefined for convenience, identical to ria_api.s)
; ---------------------------------------------------------------------------

O_RDONLY = $0000
O_WRONLY = $0001
O_RDWR   = $0002
O_CREAT  = $0200
O_TRUNC  = $0400
O_APPEND = $0008

SEEK_SET = 0
SEEK_CUR = 1
SEEK_END = 2

.endif ; KERNEL_INC
