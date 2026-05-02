/**
 * @file      shell.h
 * @author    Wojciech Gwioździk <wojciech@post.pl>
 * @copyright Wojciech Gwioździk. All rights reserved.
 *
 * based on Jason Howard's ideas & code <jth@howardlogic.com>
 * https://github.com/jthwho/rp6502-shell
 * See LICENSE file in the project root folder for license information
 */

// Picocomputer 6502 documentation
// https://picocomputer.github.io/index.html

#include "commons.h"

extern struct _timezone _tz;

#ifndef __STACKSIZE__
    #define __STACKSIZE__ 0x0200
#endif
#define MEMTOP (0xFF00-__STACKSIZE__-1)
#define COM_LOAD_ADDR 0xA000  // lowest ram address where to load the external command code (binary shell extensions - .com files)
static uint16_t com_load_addr = COM_LOAD_ADDR;

#define SHELLDRIVEDIRDEFAULT "MSC0:/SHELL/"
#define SHELLDIRDEFAULT "ROM:"
#define SHELLPROMPT "> "
#define SHELLPROMPT_1ST "> " ANSI_GREEN "[F1] help" ANSI_RESET " > "

#define PROMPT_FIRST 0
#define PROMPT_CLS   1
#define PROMPT       2

#define SHELLWALLPAPER "ROM:wallpaper.bin"

#define CMD_BUF_MAX 80
#define CMD_TOKEN_MAX 5
#define EDIT_BUF_MAX 2048
#define RUN_ARGS_BASE 0x0200      // where argc/argv block is stored for run (safe area outside shell BSS)
#define RUN_ARGS_MAX 4
#define RUN_ARGS_BUF 32
#define RUN_ARGS_BLOCK_SIZE (1 + RUN_ARGS_MAX*2 + RUN_ARGS_BUF)
#define HEXDUMP_LINE_SIZE 16

#define ARRAY_SIZE(x) (sizeof(x) / sizeof(x[0]))

#ifndef AM_DIR
#define AM_DIR 0x10
#define AM_RDO 0x01
#define AM_HID 0x02
#define AM_SYS 0x04
#define AM_VOL 0x08
#define AM_ARC 0x20
#endif

// for files transfers
#define ESC 0x1B
#define SOH 0x01
#define STX 0x02
#define ETX 0x03
#define EOT 0x04

#define FNAMELEN 64
#define CPMBUFFLEN 96
#define RMBUFFLEN 96
#define DIR_LIST_MAX 40

#define APPFLAG_RTC 0b00000001
unsigned char appflags = 0b00000000;

static const char default_shelldir[] = SHELLDIRDEFAULT;
char shelldir[64];
char *filename[20] = {"                    "};

static const char hexdigits[] = "0123456789ABCDEF";

typedef struct {
    int bytes;
    char buffer[CMD_BUF_MAX+1];
} cmdline_t;

typedef struct {
    const char *cmd;
    const char *msgsyntax;
    const char *msgerror;
    int (*func)(int argc, char **argv);
} cmd_t;

typedef struct {
    char name[FNAMELEN];
    unsigned long fsize;
    unsigned char fattrib;
    unsigned fdate;
    unsigned ftime;
} dir_list_entry_t;

static cmdline_t cmdline = {0};
static char dir_cwd[FNAMELEN];
static f_stat_t dir_ent;
static char dir_dt_buf[20];
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
static char *exe_argv[CMD_TOKEN_MAX+1];
static unsigned char run_args_backup[RUN_ARGS_BLOCK_SIZE];
static char drv_args_buf[4] = {0};
static char *drv_args[2] = { (char *)"drive", drv_args_buf };

static void refresh_current_drive(void);
static void build_run_args(int user_argc, char **user_argv);

int cmd_bload(int, char **);
int cmd_brun(int, char **);
int cmd_bsave(int, char **);
int cmd_cd(int, char **);
int cmd_chmod(int, char **);
int cmd_cls(int, char **);
int cmd_com(int, char **);
int cmd_exe(int, char **);
int cmd_copy(int, char **);
int cmd_cp(int, char **);
int cmd_ls(int, char **);
int cmd_drive(int, char **);
int cmd_exit(int, char **);
int cmd_list(int, char **);
int cmd_mem(int, char **);
int cmd_mkdir(int, char **);
int cmd_rename(int, char **);
int cmd_rm(int, char **);
int cmd_run(int, char **);
int cmd_stat(int, char **);
int cmd_time(int, char **);
int cmd_phi2(int, char **);
int cmd_launcher(int, char **); 
int cmd_cart(int, char **);

static const cmd_t commands[] = {
    { "bload",  "", "", cmd_bload},
    { "brun",   "", "", cmd_brun},
    { "bsave",  "", "", cmd_bsave},
    { "cd",     "", "", cmd_cd},
    { "chmod",  "", "", cmd_chmod},
    { "cls",    "", "", cmd_cls },
    { "copy",   "", "", cmd_copy},
    { "cp",     "", "", cmd_cp},
    { "com",    "", "", cmd_com},
    { "exe",    "", "", cmd_exe},
    { "ls",     "", "", cmd_ls},
    { "drive",  "", "", cmd_drive},
    { "exit",   "", "", cmd_exit},
    { "list",   "", "", cmd_list},
    { "mem",    "", "", cmd_mem},
    { "mkdir",  "", "", cmd_mkdir},
    { "rename", "", "", cmd_rename},
    { "rm",     "", "", cmd_rm},
    { "run",    "", "", cmd_run},
    { "stat",   "", "", cmd_stat},
    { "time",   "", "", cmd_time },
    { "phi2",   "", "", cmd_phi2},
    { "launcher",   "", "", cmd_launcher },
    { "cart",   "", "", cmd_cart },
};

// static void load_asset2xram(const char *path, unsigned xram_addr);
// static void load_setup(void);
static int startstage_boot();
static int startstage_shell();
inline void tx_char(char c);
void tx_chars(const char *buf, int ct);
void tx_string(const char *buf);
void tx_hex32(unsigned long val);
void tx_hex16(uint16_t val);
void tx_dec32(unsigned long val);
static void tx_print_existing(const char *buf, unsigned len);
static int read_line_editor(char *buf, int maxlen);
bool match_mask(const char *name, const char *mask);
const char *format_fat_datetime(unsigned fdate, unsigned ftime);
void ram_reader(uint8_t *buf, uint16_t addr, uint16_t size);
void ram_writer(const uint8_t *buf, uint16_t addr, uint16_t size);
void xram_reader(uint8_t *buf, uint16_t addr, uint16_t size);
void xram_writer(const uint8_t *buf, uint16_t addr, uint16_t size);
uint16_t mem_lo(void);
uint16_t mem_top(void);
uint16_t mem_free(void);
struct tm *get_time(void);
void show_time(void);
int hexstr(char *str, uint8_t val);
void cls();
void prompt(uint8_t mode);
static int tokenize(char *buf, int maxBuf, char **tokenList, int maxTokens);
static int execute(cmdline_t *cl);
static int execute_cmd(cmdline_t *cl, const char *cmd);
static void build_run_args(int user_argc, char **user_argv);

// shell commands
int cmd_bload(int argc, char **argv);
int cmd_brun(int argc, char **argv);
int cmd_bsave(int argc, char **argv);
int cmd_cd(int argc, char **argv);
int cmd_chmod(int argc, char **argv);
int cmd_cls(int, char **);
int cmd_com(int argc, char **argv);
int cmd_exe(int argc, char **argv);
int cmd_copy(int argc, char **argv);
int cmd_cp(int argc, char **argv);
int cmd_ls(int argc, char **argv);
int cmd_drive(int argc, char **argv);
int cmd_exit(int status, char **);
int cmd_list(int argc, char **argv);
int cmd_mem(int argc, char **argv);
int cmd_mkdir(int argc, char **argv);
int cmd_rename(int argc, char **argv);
int cmd_rm(int argc, char **argv);
int cmd_run(int argc, char **argv);
int cmd_stat(int argc, char **argv);
int cmd_time(int argc, char **argv);
int cmd_phi2(int argc, char **argv);
int cmd_launcher(int argc, char **argv);
int cmd_cart(int argc, char **argv);
extern char _BSS_RUN__[];
extern char _BSS_SIZE__[];
