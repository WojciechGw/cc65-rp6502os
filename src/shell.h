/**
 * @file      shell.h
 * @author    Wojciech Gwioździk <wojciech@post.pl>
 * @copyright Wojciech Gwioździk. All rights reserved.
 *
 * based on Jason Howard's code <jth@howardlogic.com>
 * See LICENSE file in the project root folder for license information
 */

// Picocomputer 6502 documentation
// https://picocomputer.github.io/index.html

#include <rp6502.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>

extern void quit(void);

#ifndef __STACKSIZE__
    #define __STACKSIZE__ 0x0800
#endif
#define MEMTOP (0xFD00-__STACKSIZE__)
#define SHELLDIR "USB0:/SHELL/"
#define COM_LOAD_ADDR 0xA000      /* where to upload the code (binary shell extensions - .com files) */

#define CMD_BUF_MAX 511
#define CMD_TOKEN_MAX 64
#define EDIT_BUF_MAX 2048
#define RUN_ARGS_BASE 0x0200      /* where argc/argv block is stored for run (safe area outside shell BSS) */
#define RUN_ARGS_MAX 4
#define RUN_ARGS_BUF 32
#define RUN_ARGS_BLOCK_SIZE (1 + RUN_ARGS_MAX*2 + RUN_ARGS_BUF)
#define HEXDUMP_LINE_SIZE 16

#define TX_READY (RIA.ready & RIA_READY_TX_BIT)
#define RX_READY (RIA.ready & RIA_READY_RX_BIT)
#define TX_READY_SPIN while(!TX_READY)
#define RX_READY_SPIN while(!RX_READY)

#define CHAR_BELL   0x07
#define CHAR_BS     0x08
#define CHAR_CR     0x0D
#define CHAR_LF     0x0A
#define CHAR_ESC    0x1B
#define CHAR_NCHR   0x5B
#define CHAR_UP     0x41
#define CHAR_DOWN   0x42
#define CHAR_RIGHT  0x43
#define CHAR_LEFT   0x44
#define CHAR_F1     0x50

#define KEY_DEL     0x7F
#define TAB         "\t"
#define NEWLINE     "\r\n"

#define ARRAY_SIZE(x) (sizeof(x) / sizeof(x[0]))

#ifndef AM_DIR
#define AM_DIR 0x10
#define AM_RDO 0x01
#define AM_HID 0x02
#define AM_SYS 0x04
#define AM_VOL 0x08
#define AM_ARC 0x20
#endif

extern void quit(void);

static const char hexdigits[] = "0123456789ABCDEF";

typedef struct {
    int bytes;
    int lastbytes;
    char buffer[CMD_BUF_MAX+1];
    char lastbuffer[CMD_BUF_MAX+1];
} cmdline_t;

typedef struct {
    const char *cmd;
    const char *help;
    int (*func)(int argc, char **argv);
} cmd_t;

typedef void (*char_stream_func_t)(const char *buf, int size);
typedef void (*read_data_func_t)(uint8_t *buf, uint16_t addr, uint16_t size);

// Statically allocate buffers to keep cc65 stack usage low.
#define FNAMELEN 64
#define CPMBUFFLEN 96
#define RMBUFFLEN 96
#define DIR_LIST_MAX 40
static char dir_cwd[FNAMELEN];
static f_stat_t dir_ent;
typedef struct {
    char name[FNAMELEN];
    unsigned long fsize;
    unsigned char fattrib;
    unsigned fdate;
    unsigned ftime;
} dir_list_entry_t;
static char dir_dt_buf[20];
static char dev_label[16];
static char saved_cwd[128];
static char current_drive = '0';
static char cpm_path[CPMBUFFLEN];
static char cpm_mask[CPMBUFFLEN];
static char cpm_dest[CPMBUFFLEN];
static char cpm_srcfile[CPMBUFFLEN];
static char cpm_dstfile[CPMBUFFLEN];
static char *cpm_args[3];
static char rm_path[RMBUFFLEN];
static char rm_mask[RMBUFFLEN];
static char rm_file[RMBUFFLEN];
static unsigned char bload_buf[128];
static char com_fname[FNAMELEN];
static char *com_argv[CMD_TOKEN_MAX+1];
static unsigned char run_args_backup[RUN_ARGS_BLOCK_SIZE];
static char shell_end_marker;
static char drv_args_buf[4] = {0};
static char *drv_args[2] = { (char *)"drive", drv_args_buf };
extern struct _timezone _tz;

static void refresh_current_drive(void);
// int cmd_help(int, char **);
int cmd_cls(int, char **);
int cmd_memx(int, char **);
int cmd_memr(int, char **);
int cmd_bload(int, char **);
int cmd_bsave(int, char **);
int cmd_brun(int, char **);
int cmd_run(int, char **);
int cmd_dir(int, char **);
int cmd_drive(int, char **);
int cmd_drives(int, char **);
int cmd_list(int, char **);
int cmd_cd(int, char **);
int cmd_mkdir(int, char **);
int cmd_rm(int, char **);
int cmd_cp(int, char **);
int cmd_mv(int, char **);
int cmd_rename(int, char **);
int cmd_com(int, char **);
int cmd_mem(int, char **);
int cmd_cm(int, char **);
int cmd_phi2(int, char **);
int cmd_chmod(int, char **);
int cmd_exit(int, char **);
int cmd_time(int, char **);
int cmd_stat(int, char **);
int cmd_edit(int, char **);

static void build_run_args(int user_argc, char **user_argv);

static const cmd_t commands[] = {
    { "dir",    "active drive directory", cmd_dir},
    { "drive",  "set active drive", cmd_drive},
    { "drives", "shows available drives", cmd_drives},
    { "cd",     "change active directory", cmd_cd},
    { "mkdir",  "create directory", cmd_mkdir},
    { "chmod",  "set file attributes", cmd_chmod},
    { "cp",     "copy file", cmd_cp},
    { "cm",     "copy/move multiple files", cmd_cm},
    { "mv",     "moves/renames a file or directory", cmd_mv},
    { "rename", "renames a file or directory", cmd_rename},
    { "rm",     "removes a file/files", cmd_rm},
    { "list",   "shows a file content", cmd_list},
    { "edit",   "simple text editor", cmd_edit},
    { "stat",   "returns file or directory info", cmd_stat},
    { "bload",  "load binary file to RAM/XRAM", cmd_bload},
    { "bsave",  "save RAM/XRAM to binary file", cmd_bsave},
    { "brun",   "load binary file to RAM and run", cmd_brun},
    { "com",    "load .com binary and run with args", cmd_com},
    { "run",    "run code at address", cmd_run},
    { "mem",    "information about memory", cmd_mem},
    { "memx",   "reads xram", cmd_memx },
    { "memr",   "reads ram", cmd_memr },
    { "cls",    "clears terminal", cmd_cls },
    { "time",   "shows local time", cmd_time },
    { "phi2",   "shows CPU clock frequency", cmd_phi2},
    // { "help",   "prints a list of commands", cmd_help },
    { "exit",   "exit to the system monitor", cmd_exit},
};

inline void tx_char(char c);
void tx_chars(const char *buf, int ct);
void tx_string(const char *buf);
void tx_hex32(unsigned long val);
void tx_hex16(uint16_t val);
bool match_mask(const char *name, const char *mask);
void tx_dec32(unsigned long val);
static int read_line_editor(char *buf, int maxlen);
static void tx_print_existing(const char *buf, unsigned len);
const char *format_fat_datetime(unsigned fdate, unsigned ftime);
void ram_reader(uint8_t *buf, uint16_t addr, uint16_t size);
void ram_writer(const uint8_t *buf, uint16_t addr, uint16_t size);
void xram_reader(uint8_t *buf, uint16_t addr, uint16_t size);
void xram_writer(const uint8_t *buf, uint16_t addr, uint16_t size);
uint16_t mem_lo(void);
uint16_t mem_top(void);
uint16_t mem_free(void);
void show_time(void);
int hexstr(char *str, uint8_t val);
void hexdump(uint16_t addr, uint16_t bytes, char_stream_func_t streamer, read_data_func_t reader);
void clearterminal();
void cls();
void prompt();
void help();
int tokenize(char *buf, int maxBuf, char **tokenList, int maxTokens);
int execute(cmdline_t *cl);

// shell commands
// int cmd_help(int, char **);
int cmd_cls(int, char **);
int cmd_exit(int status, char **);
int cmd_cd(int argc, char **argv);
int cmd_drives(int argc, char **argv);
int cmd_drive(int argc, char **argv);
int cmd_list(int argc, char **argv);
int cmd_edit(int argc, char **argv);
int cmd_mkdir(int argc, char **argv);
int cmd_rm(int argc, char **argv);
int cmd_cp(int argc, char **argv);
int cmd_mv(int argc, char **argv);
int cmd_rename(int argc, char **argv);
int cmd_bload(int argc, char **argv);
int cmd_bsave(int argc, char **argv);
int cmd_brun(int argc, char **argv);
int cmd_com(int argc, char **argv);
int cmd_run(int argc, char **argv);
static void build_run_args(int user_argc, char **user_argv);
int cmd_cm(int argc, char **argv);
int cmd_memx(int argc, char **argv);
int cmd_memr(int argc, char **argv);
int cmd_dir(int argc, char **argv);
int cmd_time(int argc, char **argv);
int cmd_phi2(int argc, char **argv);
int cmd_mem(int argc, char **argv);
int cmd_stat(int argc, char **argv);
int cmd_chmod(int argc, char **argv);

// end of file shell.h