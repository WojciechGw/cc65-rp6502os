/*
 * shell_mt.c — razemOSmt TASK0 shell (minimal, no stdlib)
 */

#include <rp6502.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>

#define DEBUG

#define APPVER "0.01"
#define APPNAME "razemOS(mt)"
#define UNAME APPNAME " v." APPVER

#define RIA_READY_TX  0x80
#define RIA_READY_RX  0x40
#define CRLF          "\r\n"
#define PROMPT_STR    "> "
#define CMD_BUF_MAX   80
#define TOKEN_MAX     8

static void uart_putc(char c)
{
    while (!(RIA.ready & RIA_READY_TX))
        ;
    RIA.tx = c;
}

static void uart_puts(const char *s)
{
    while (*s)
        uart_putc(*s++);
}

static char uart_getc(void)
{
    while (!(RIA.ready & RIA_READY_RX))
        ;
    return (char)RIA.rx;
}

/* --------------------------------------------------------------------------
 * Command history
 * -------------------------------------------------------------------------- */
#define HIST_SIZE  8
#define HIST_LEN   CMD_BUF_MAX

static char  s_hist[HIST_SIZE][HIST_LEN];
static unsigned char s_hist_count = 0;   /* total entries stored, max HIST_SIZE */
static unsigned char s_hist_head  = 0;   /* ring index of newest entry          */

static void hist_push(const char *line)
{
    if (line[0] == '\0') return;
    /* skip duplicate of last entry */
    if (s_hist_count > 0) {
        unsigned char prev = (s_hist_head + HIST_SIZE - 1) & (HIST_SIZE - 1);
        if (!strcmp(s_hist[prev], line)) return;
    }
    strncpy(s_hist[s_hist_head], line, HIST_LEN - 1);
    s_hist[s_hist_head][HIST_LEN - 1] = '\0';
    s_hist_head = (s_hist_head + 1) & (HIST_SIZE - 1);
    if (s_hist_count < HIST_SIZE) s_hist_count++;
}

/* Returns pointer to history entry at age=0 (newest) .. age=count-1 (oldest).
   Returns NULL if age >= count. */
static const char *hist_get(unsigned char age)
{
    if (age >= s_hist_count) return NULL;
    return s_hist[((unsigned char)(s_hist_head + HIST_SIZE - 1 - age)) & (HIST_SIZE - 1)];
}

/* --------------------------------------------------------------------------
 * ESC sequence parser — returns a key code
 * -------------------------------------------------------------------------- */
#define KEY_LEFT    0x01
#define KEY_RIGHT   0x02
#define KEY_UP      0x03
#define KEY_DOWN    0x04
#define KEY_HOME    0x05
#define KEY_END     0x06
#define KEY_DEL     0x07

static unsigned char parse_esc(void)
{
    char c = uart_getc();
    if (c != '[') return 0;
    c = uart_getc();
    switch (c) {
        case 'A': return KEY_UP;
        case 'B': return KEY_DOWN;
        case 'C': return KEY_RIGHT;
        case 'D': return KEY_LEFT;
        case 'H': return KEY_HOME;
        case 'F': return KEY_END;
        case '1': uart_getc(); return KEY_HOME;   /* \x1b[1~ */
        case '3': uart_getc(); return KEY_DEL;    /* \x1b[3~ */
        case '4': uart_getc(); return KEY_END;    /* \x1b[4~ */
        default:  return 0;
    }
}

/* --------------------------------------------------------------------------
 * Redraw from cursor to end of line, then reposition cursor.
 * Call after any insertion/deletion that shifts chars right of cursor.
 * pos  = cursor position (0-based index into buf)
 * len  = total line length
 * -------------------------------------------------------------------------- */
static void redraw_tail(const char *buf, int pos, int len)
{
    int i;
    int tail = len - pos;
    /* print chars from pos to end */
    for (i = pos; i < len; i++) uart_putc(buf[i]);
    /* erase any leftover char at old end position */
    uart_putc(' ');
    /* move cursor back to pos */
    for (i = 0; i <= tail; i++) uart_putc('\b');
}

/* --------------------------------------------------------------------------
 * Full line editor
 * -------------------------------------------------------------------------- */
static int read_line(char *buf, int maxlen)
{
    int  len  = 0;   /* current line length  */
    int  pos  = 0;   /* cursor position      */
    int  hage = -1;  /* -1 = not browsing history */
    char c;
    unsigned char key;
    int i;

    maxlen--;        /* reserve space for '\0' */

    for (;;) {
        c = uart_getc();

        /* --- Enter --- */
        if (c == '\r' || c == '\n') {
            /* move cursor to end before newline */
            for (i = pos; i < len; i++) uart_putc(buf[i]);
            uart_puts(CRLF);
            buf[len] = '\0';
            hist_push(buf);
            return len;
        }

        /* --- ESC sequence --- */
        if (c == '\x1b') {
            key = parse_esc();
            switch (key) {

                case KEY_LEFT:
                    if (pos > 0) { pos--; uart_putc('\b'); }
                    break;

                case KEY_RIGHT:
                    if (pos < len) { uart_putc(buf[pos]); pos++; }
                    break;

                case KEY_HOME:
                    while (pos > 0) { uart_putc('\b'); pos--; }
                    break;

                case KEY_END:
                    while (pos < len) { uart_putc(buf[pos]); pos++; }
                    break;

                case KEY_DEL:
                    if (pos < len) {
                        for (i = pos; i < len - 1; i++) buf[i] = buf[i + 1];
                        len--;
                        redraw_tail(buf, pos, len);
                    }
                    break;

                case KEY_UP:
                    /* older history entry */
                    {
                        const char *h;
                        unsigned char next_age = (unsigned char)(hage + 1);
                        h = hist_get(next_age);
                        if (h) {
                            hage = (int)next_age;
                            /* clear current line on screen */
                            while (pos > 0) { uart_putc('\b'); pos--; }
                            for (i = 0; i < len; i++) uart_putc(' ');
                            for (i = 0; i < len; i++) uart_putc('\b');
                            /* copy history entry */
                            strncpy(buf, h, maxlen);
                            buf[maxlen] = '\0';
                            len = pos = 0;
                            while (buf[len]) len++;
                            uart_puts(buf);
                            pos = len;
                        }
                    }
                    break;

                case KEY_DOWN:
                    /* newer history entry or blank line */
                    {
                        const char *h = NULL;
                        if (hage > 0) { hage--; h = hist_get((unsigned char)hage); }
                        else           { hage = -1; }
                        /* clear current line */
                        while (pos > 0) { uart_putc('\b'); pos--; }
                        for (i = 0; i < len; i++) uart_putc(' ');
                        for (i = 0; i < len; i++) uart_putc('\b');
                        if (h) {
                            strncpy(buf, h, maxlen);
                            buf[maxlen] = '\0';
                            len = pos = 0;
                            while (buf[len]) len++;
                            uart_puts(buf);
                            pos = len;
                        } else {
                            len = pos = 0;
                        }
                    }
                    break;
            }
            continue;
        }

        /* --- Backspace --- */
        if ((c == '\b' || c == 0x7f) && pos > 0) {
            pos--;
            len--;
            for (i = pos; i < len; i++) buf[i] = buf[i + 1];
            uart_putc('\b');
            redraw_tail(buf, pos, len);
            continue;
        }

        /* --- Ctrl+A : Home --- */
        if (c == 0x01) { while (pos > 0) { uart_putc('\b'); pos--; } continue; }
        /* --- Ctrl+E : End  --- */
        if (c == 0x05) { while (pos < len) { uart_putc(buf[pos]); pos++; } continue; }
        /* --- Ctrl+K : kill to end --- */
        if (c == 0x0B) {
            for (i = pos; i < len; i++) uart_putc(' ');
            for (i = pos; i < len; i++) uart_putc('\b');
            len = pos;
            continue;
        }

        /* --- Printable character --- */
        if (c >= 0x20 && len < maxlen) {
            /* shift right to make room */
            for (i = len; i > pos; i--) buf[i] = buf[i - 1];
            buf[pos] = c;
            len++;
            uart_putc(c);
            pos++;
            if (pos < len) redraw_tail(buf, pos, len);
        }
    }
}

/* f_stat_t is too large for the stack (256-byte fname) — keep it static */
static f_stat_t s_dirent;

/* --------------------------------------------------------------------------
 * TCB layout (mirrors kernel.s definitions)
 * -------------------------------------------------------------------------- */
#define TCB_BASE     ((volatile unsigned char *)0x0E00)
#define TCB_SIZE     64
#define MAX_TASKS    8

#define TCB_PC_LO    0
#define TCB_PC_HI    1
#define TCB_SP       5
#define TCB_STATUS   7
#define TCB_WTYPE    8
#define TCB_ID       11
#define TCB_FCNT_L   14
#define TCB_FCNT_H   15
#define TCB_NAME     20

#define TASK_DEAD    0
#define TASK_READY   1
#define TASK_RUNNING 2
#define TASK_WAITING 3

#define KZP_NTASK    (*(volatile unsigned char *)0x0021)

/* kernel syscall wrappers (kern_calls.s) */
void __fastcall__ kern_task_create(unsigned int addr);
void __fastcall__ kern_sleep_frames(unsigned int n);
unsigned char __fastcall__ kern_task_kill(unsigned char task_id);
void __fastcall__ kern_set_irqfreq(unsigned int hz);
unsigned char __fastcall__ kern_set_phi2(unsigned int khz);
extern unsigned char kern_task_create_slot;

static void uart_puthex8(unsigned char v)
{
    static const char hex[] = "0123456789ABCDEF";
    uart_putc(hex[v >> 4]);
    uart_putc(hex[v & 0x0F]);
}

/* linker-provided segment boundary symbols (define=yes in shell.cfg) */
extern char _STARTUP_RUN__[];
extern char _STARTUP_SIZE__[];
extern char _CODE_RUN__[];
extern char _CODE_SIZE__[];
extern char _RODATA_RUN__[];
extern char _RODATA_SIZE__[];
extern char _DATA_RUN__[];
extern char _DATA_SIZE__[];
extern char _BSS_RUN__[];
extern char _BSS_SIZE__[];

static void uart_put_dec(unsigned int v)
{
    static char dbuf[6];
    unsigned char j = 5;
    dbuf[j] = '\0';
    do { dbuf[--j] = '0' + (unsigned char)(v % 10); v /= 10; } while (v && j > 0);
    uart_puts(dbuf + j);
}

static void uart_put_hex16(unsigned int v)
{
    uart_puthex8((unsigned char)(v >> 8));
    uart_puthex8((unsigned char)(v & 0xFF));
}

static void mem_row(const char *label, unsigned int start, unsigned int end)
{
    unsigned int size = end - start + 1;
    uart_puts("  ");
    uart_puts(label);
    uart_puts("  $");
    uart_put_hex16(start);
    uart_puts("-$");
    uart_put_hex16(end);
    uart_puts("  ");
    uart_put_dec(size);
    uart_puts("B" CRLF);
}

static void cmd_mem(void)
{
    unsigned int bss_end  = (unsigned int)_BSS_RUN__  + (unsigned int)_BSS_SIZE__;
    unsigned int heap_free = 0x9E00u - bss_end;

    uart_puts(CRLF);
    uart_puts("  --- system ---" CRLF);
    mem_row("ZP kernel  ", 0x001A, 0x0027);
    mem_row("ZP cc65    ", 0x0028, 0x0041);
    mem_row("JUMPTABLE  ", 0x0200, 0x025F);
    mem_row("KERNEL     ", 0x0260, 0x0AF1);
    mem_row("KDATA      ", 0x0D00, 0x0DFF);
    mem_row("TCBAREA    ", 0x0E00, 0x0FFF);
    uart_puts(CRLF);
    uart_puts("  --- task0 ---" CRLF);
    mem_row("STARTUP    ", (unsigned int)_STARTUP_RUN__,   /* TASK0 from $1000 */
            (unsigned int)_STARTUP_RUN__ + (unsigned int)_STARTUP_SIZE__ - 1);
    mem_row("CODE       ", (unsigned int)_CODE_RUN__,
            (unsigned int)_CODE_RUN__    + (unsigned int)_CODE_SIZE__    - 1);
    mem_row("RODATA     ", (unsigned int)_RODATA_RUN__,
            (unsigned int)_RODATA_RUN__  + (unsigned int)_RODATA_SIZE__  - 1);
    if ((unsigned int)_DATA_SIZE__ > 0)
        mem_row("DATA       ", (unsigned int)_DATA_RUN__,
                (unsigned int)_DATA_RUN__ + (unsigned int)_DATA_SIZE__ - 1);
    mem_row("BSS        ", (unsigned int)_BSS_RUN__, bss_end - 1);
    uart_puts(CRLF);
    uart_puts("  --- task0 heap ---" CRLF);
    uart_puts("  free heap   $");
    uart_put_hex16(bss_end);
    uart_puts("-$9DFF  ");
    uart_put_dec(heap_free);
    uart_puts("B" CRLF);
    mem_row("cc65 stack ", 0x9E00, 0x9FFF);
    uart_puts(CRLF);
    uart_puts("  --- tasks 1-7 ---" CRLF);
    mem_row("TASK1 RAM  ", 0xA000, 0xAFFF);
    mem_row("TASK2 RAM  ", 0xB000, 0xBFFF);
    mem_row("TASK3 RAM  ", 0xC000, 0xCFFF);
    mem_row("TASK4 RAM  ", 0xD000, 0xD7FF);  /* example layout */
    mem_row("TASK5 RAM  ", 0xD800, 0xDFFF);
    mem_row("TASK6 RAM  ", 0xE000, 0xEFFF);
    mem_row("TASK7 RAM  ", 0xF000, 0xF7FF);
    uart_puts(CRLF);
}

static void cmd_ps(void)
{
    unsigned char i;
    volatile unsigned char *tcb;
    unsigned char status;
    unsigned char id;
    unsigned int pc;
    unsigned int frames;

    uart_puts("ID  STATUS   PC    FRAMES  NAME" CRLF);
    uart_puts("--  ------   ----  ------  ----" CRLF);

    for (i = 0; i < MAX_TASKS; i++) {
        tcb = TCB_BASE + (unsigned int)i * TCB_SIZE;
        status = tcb[TCB_STATUS];
        if (status == TASK_DEAD) continue;

        id     = tcb[TCB_ID];
        pc     = (unsigned int)tcb[TCB_PC_LO] | ((unsigned int)tcb[TCB_PC_HI] << 8);
        frames = (unsigned int)tcb[TCB_FCNT_L] | ((unsigned int)tcb[TCB_FCNT_H] << 8);

        uart_puthex8(id);
        uart_puts("  ");
        switch (status) {
            case TASK_RUNNING: uart_puts("RUNNING"); break;
            case TASK_READY:   uart_puts("READY  "); break;
            case TASK_WAITING: uart_puts("WAITING"); break;
            default:           uart_puts("?      "); break;
        }
        uart_puts("  $");
        uart_puthex8((unsigned char)(pc >> 8));
        uart_puthex8((unsigned char)(pc & 0xFF));
        uart_puts("  ");
        /* print frames as decimal (max 65535) */
        {
            static char fbuf[6];
            unsigned int v = frames;
            unsigned char j = 5;
            fbuf[j] = '\0';
            do { fbuf[--j] = '0' + (v % 10); v /= 10; } while (v && j > 0);
            /* right-pad to 6 chars */
            uart_puts(fbuf + j);
            while (j-- > 0) uart_putc(' ');
        }
        uart_puts("  ");
        /* name field (7 chars + null, may be empty) */
        if (tcb[TCB_NAME])
            uart_puts((const char *)(tcb + TCB_NAME));
        else {
            uart_puts("task");
            uart_puthex8(id);
        }
        uart_puts(CRLF);
    }
}

static void cmd_ls(void)
{
    int dd = f_opendir(".");
    if (dd < 0) {
        uart_puts("ls: error" CRLF);
        return;
    }
    while (f_readdir(&s_dirent, dd) == 0) {
        if (s_dirent.fname[0] == '\0') break;
        if (s_dirent.fattrib & 0x10)
            uart_puts("[DIR]\t");
        else
            uart_puts("\t");
        uart_puts(s_dirent.fname);
        uart_puts(CRLF);
    }
    f_closedir(dd);
}

#ifdef DEBUG
static void cmd_tst(void)
{
    volatile unsigned char *tcb1 = TCB_BASE + TCB_SIZE;
    unsigned int i;

    uart_puts("TCB1 raw dump ($0DE0):" CRLF);
    for (i = 0; i < 32; i++) {
        if ((i & 7) == 0) {
            uart_puts("  +");
            uart_puthex8((unsigned char)i);
            uart_puts(": ");
        }
        uart_puthex8(tcb1[i]);
        uart_putc(' ');
        if ((i & 7) == 7) uart_puts(CRLF);
    }
    uart_puts("kzp_ntask=$21: ");
    uart_puthex8(*(volatile unsigned char *)0x0021);
    uart_puts(CRLF);
    uart_puts("kzp_curr =$20: ");
    uart_puthex8(*(volatile unsigned char *)0x0020);
    uart_puts(CRLF);
    uart_puts("kzp_sched=$22: ");
    uart_puthex8(*(volatile unsigned char *)0x0022);
    uart_puts(CRLF);
    uart_puts("kzp_iflg=$24: ");
    uart_puthex8(*(volatile unsigned char *)0x0024);
    uart_puts(CRLF);

    /* watch frame counters — use wai so IRQ can fire */
    uart_puts("watching frames (wai)..." CRLF);
    for (i = 0; i < 5; i++) {
        volatile unsigned char *tcb0 = TCB_BASE;
        unsigned int f0;
        unsigned char ifl;
        f0 = (unsigned int)tcb0[TCB_FCNT_L] | ((unsigned int)tcb0[TCB_FCNT_H] << 8);
        ifl = *(volatile unsigned char *)0x0024;
        uart_puts("  iflags="); uart_puthex8(ifl);
        uart_puts(" T0f="); uart_put_dec(f0);
        uart_puts(" T1f="); uart_put_dec((unsigned int)tcb1[TCB_FCNT_L] | ((unsigned int)tcb1[TCB_FCNT_H] << 8));
        uart_puts(" T1st="); uart_puthex8(tcb1[TCB_STATUS]);
        uart_puts(CRLF);
    }
}
#endif

/* Parse decimal integer from string; returns 0 if no digits found. */
static unsigned int parse_uint(const char *s)
{
    unsigned int v = 0;
    while (*s >= '0' && *s <= '9')
        v = v * 10 + (unsigned char)(*s++ - '0');
    return v;
}

/* KDATA layout (offsets from 0x0D00) — mirrors kernel.s KDATA segment:
 * +0  stack_pool_free, +1 zp_pool_free, +2..+9 zp_slot_base[8]
 * +10 via_phi2_lo, +11 via_phi2_hi
 * +12 via_t1_latch_lo, +13 via_t1_latch_hi
 * +14 via_irq_divider, +15 via_irq_tick
 * +16 via_irq_hz_lo, +17 via_irq_hz_hi */
#define KDATA_BASE         ((volatile unsigned char *)0x0D00)
#define KDATA_PHI2_LO      10
#define KDATA_PHI2_HI      11
#define KDATA_T1_LATCH_LO  12
#define KDATA_T1_LATCH_HI  13
#define KDATA_IRQ_DIVIDER  14
#define KDATA_IRQ_HZ_LO    16
#define KDATA_IRQ_HZ_HI    17

static void cmd_irqstat(void)
{
    volatile unsigned char *kd = KDATA_BASE;
    unsigned int phi2_khz, latch, divider, irq_hz_set;
    unsigned long ticks_per_switch;
    unsigned int hz_calc;

    phi2_khz     = (unsigned int)kd[KDATA_PHI2_LO] | ((unsigned int)kd[KDATA_PHI2_HI] << 8);
    latch        = (unsigned int)kd[KDATA_T1_LATCH_LO] | ((unsigned int)kd[KDATA_T1_LATCH_HI] << 8);
    divider      = kd[KDATA_IRQ_DIVIDER];
    irq_hz_set   = (unsigned int)kd[KDATA_IRQ_HZ_LO] | ((unsigned int)kd[KDATA_IRQ_HZ_HI] << 8);

    /* actual Hz = phi2_khz * 1000 / (latch * divider) */
    ticks_per_switch = (unsigned long)latch * divider;
    hz_calc = (ticks_per_switch > 0)
              ? (unsigned int)((unsigned long)phi2_khz * 1000UL / ticks_per_switch)
              : 0;

    uart_puts("phi2:    "); uart_put_dec(phi2_khz);   uart_puts(" kHz" CRLF);
    uart_puts("latch:   "); uart_put_dec(latch);       uart_puts(CRLF);
    uart_puts("divider: "); uart_put_dec(divider);     uart_puts(CRLF);
    uart_puts("set:     "); uart_put_dec(irq_hz_set);  uart_puts(" Hz" CRLF);
    uart_puts("actual:  "); uart_put_dec(hz_calc);     uart_puts(" Hz" CRLF);
}

static void cmd_irqfreq(const char *arg)
{
    unsigned int hz;
    if (*arg == '\0') { uart_puts("usage: irqfreq <Hz>" CRLF); return; }
    hz = parse_uint(arg);
    if (hz < 1 || hz > 1000) { uart_puts("! range: 1-1000" CRLF); return; }
    kern_set_irqfreq(hz);
}

static void cmd_phi2(const char *arg)
{
    unsigned int khz;
    unsigned char r;
    if (*arg == '\0') { uart_puts("usage: phi2 <kHz>" CRLF); return; }
    khz = parse_uint(arg);
    if (khz < 100 || khz > 8000) { uart_puts("! range: 100-8000" CRLF); return; }
    r = kern_set_phi2(khz);
    if (r != 0) uart_puts("! phi2: error" CRLF);
}

static void cmd_sleep(const char *arg)
{
    unsigned int n = parse_uint(arg);
    if (n == 0) { uart_puts("usage: sleep <frames>" CRLF); return; }
    kern_sleep_frames(n);
}

static unsigned int parse_hex(const char *s)
{
    unsigned int v = 0;
    while (1) {
        if (*s >= '0' && *s <= '9')      v = (v << 4) | (*s - '0');
        else if (*s >= 'A' && *s <= 'F') v = (v << 4) | (*s - 'A' + 10);
        else if (*s >= 'a' && *s <= 'f') v = (v << 4) | (*s - 'a' + 10);
        else break;
        s++;
    }
    return v;
}

static void cmd_run(const char *arg)
{
    unsigned char slot;
    unsigned int addr;
    const char *p;
    if (*arg == '\0') { uart_puts("usage: run <slot> <addr>" CRLF); return; }
    slot = (unsigned char)parse_uint(arg);
    if (slot == 0 || slot >= MAX_TASKS) { uart_puts("invalid slot" CRLF); return; }
    for (p = arg; *p && *p != ' '; p++) ;
    if (*p != ' ') { uart_puts("usage: run <slot> <addr>" CRLF); return; }
    p++;
    addr = (*p == '$') ? parse_hex(p + 1) : parse_uint(p);
    kern_task_create_slot = slot;
    kern_task_create(addr);
    uart_puts("task run" CRLF);
}

static void cmd_help(void)
{
    uart_puts(
        "help\t\t\tthis list" CRLF
        "cls\t\t\tclear screen" CRLF
        "ls\t\t\tlist files" CRLF
        "cd <path>\t\tchange directory" CRLF
        "mem\t\t\tmemory map" CRLF
        "ps\t\t\ttask list" CRLF
        "kill <id>\t\tkill task by id" CRLF
        "sleep <N>\t\tsleep N vsync frames" CRLF
        "load <file> <addr>\tload binary to memory ($hex)" CRLF
        "run <slot> <addr>\tstart task (addr: decimal or $hex)" CRLF
        "irqfreq <Hz>\t\tset context-switch frequency (1-1000 Hz)" CRLF
        "irqstat\t\t\tmeasure actual IRQ frequency" CRLF
        "phi2 <kHz>\t\tset CPU clock (100-8000 kHz)" CRLF
        "uname\t\t\tversion" CRLF
        "exit\t\t\texit to the monitor" CRLF
    );
}

static void cmd_kill(const char *arg)
{
    unsigned char id = (unsigned char)parse_uint(arg);
    unsigned char r;
    if (*arg == '\0') { uart_puts("usage: kill <id>" CRLF); return; }
    if (id == 0) { uart_puts("! cannot kill TASK0" CRLF); return; }
    r = kern_task_kill(id);
    uart_puts("task ");
    uart_puts(arg);
    if (r == 0){
        uart_puts(" killed" CRLF);
    } else {
        uart_puts("! kill error" CRLF);
    }
}

static void cmd_cd(const char *arg)
{
    if (*arg == '\0') { uart_puts("usage: cd <path>" CRLF); return; }
    if (chdir(arg) < 0)
        uart_puts("! cd: failed" CRLF);
}

static void cmd_load(const char *arg)
{
    const char *p;
    char fname[32];
    unsigned int addr;
    unsigned char n;
    int fd;
    int got;
    static unsigned char lbuf[128];
    unsigned int total = 0;

    for (p = arg, n = 0; *p && *p != ' ' && n < (unsigned char)(sizeof(fname) - 1); p++, n++)
        fname[n] = *p;
    fname[n] = '\0';
    if (n == 0 || *p != ' ') { uart_puts("usage: load <file> <addr>" CRLF); return; }
    p++;
    addr = (*p == '$') ? parse_hex(p + 1) : parse_uint(p);

    fd = open(fname, O_RDONLY);
    if (fd < 0) { uart_puts("! load: cannot open" CRLF); return; }

    for (;;) {
        unsigned char i;
        got = read(fd, lbuf, sizeof(lbuf));
        if (got <= 0) break;
        for (i = 0; i < (unsigned char)got; i++)
            *(unsigned char *)(addr + total + i) = lbuf[i];
        total += (unsigned int)got;
    }
    close(fd);

    uart_puts("loaded ");
    uart_put_dec(total);
    uart_puts("B -> $");
    uart_put_hex16(addr);
    uart_puts(CRLF);
}

static void execute(char *buf)
{
    const char *arg;
    if (buf[0] == '\0') return;

    /* find argument after first space */
    for (arg = buf; *arg && *arg != ' '; arg++)
        ;
    if (*arg == ' ') arg++;

    if (!strcmp(buf, "help") || !strcmp(buf, "?"))
        cmd_help();
    else if (!strncmp(buf, "cd ", 3))
        cmd_cd(arg);
    else if (!strcmp(buf, "ls"))
        cmd_ls();
    else if (!strcmp(buf, "mem"))
        cmd_mem();
    else if (!strcmp(buf, "ps"))
        cmd_ps();
    #ifdef DEBUG
    else if (!strcmp(buf, "tst"))
        cmd_tst();
    #endif
    else if (!strcmp(buf, "uname"))
        uart_puts(UNAME CRLF);
    else if (!strcmp(buf, "cls"))
        uart_puts("\x1b[2J\x1b[H");
    else if (!strncmp(buf, "sleep ", 6))
        cmd_sleep(arg);
    else if (!strncmp(buf, "kill ", 5))
        cmd_kill(arg);
    else if (!strncmp(buf, "run ", 4))
        cmd_run(arg);
    else if (!strncmp(buf, "load ", 5))
        cmd_load(arg);
    else if (!strcmp(buf, "irqstat"))
        cmd_irqstat();
    else if (!strncmp(buf, "irqfreq ", 8))
        cmd_irqfreq(arg);
    else if (!strncmp(buf, "phi2 ", 5))
        cmd_phi2(arg);
    else {
        uart_puts("? ");
        uart_puts(buf);
        uart_puts(CRLF);
    }
}

int main(void)
{
    char buf[CMD_BUF_MAX];

    uart_puts(CRLF UNAME CRLF);

    /* Start TASK1 heartbeat (entry at $A000, slot 1) */
    kern_task_create_slot = 1;
    kern_task_create(0xA000);

    for (;;) {
        uart_puts(PROMPT_STR);
        read_line(buf, CMD_BUF_MAX);
        if (!strcmp(buf, "exit")) break;
        execute(buf);
    }

    {
        unsigned char i;
        for (i = 1; i < MAX_TASKS; i++)
            kern_task_kill(i);
        

        RIA.irq = 0;
        i = RIA.vsync;
        i = RIA.irq;
    }
    uart_puts(CRLF CRLF "All tasks killed, IRQ tick is off" CRLF);
    uart_puts(CRLF CRLF "Press Alt+F4 exits to the monitor" CRLF);
    return 0;

}
