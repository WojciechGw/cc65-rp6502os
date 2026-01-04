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

#include "commons.h"

extern struct _timezone _tz;

#ifndef __STACKSIZE__
    #define __STACKSIZE__ 0x0200
#endif
#define MEMTOP (0xFF00-__STACKSIZE__)
#define COM_LOAD_ADDR 0x9000  // lowest ram address where to load the external command code (binary shell extensions - .com files)

#define SHELLVER "20260105.0050"
#define SHELLDIRDEFAULT "USB0:/SHELL/"
#define SHELLPROMPT "> "
#define SHELLPROMPT_1ST "> " ANSI_GREEN "[F1] help" ANSI_RESET " > "

#define APP_MSG_START ANSI_DARK_GRAY "\x1b[13;24HOS Shell for Picocomputer 6502" 
#define APP_HOURGLASS "\x1b[14;34H" "..........\x1b[10D" ANSI_RESET
#define APP_MSG_TITLE "\x1b[2;1HOS Shell for Picocomputer 6502\x1b[2;60Hversion " SHELLVER
#define APP_MSG_HELP_COMADDRESS "\x1b[30;1H" ANSI_DARK_GRAY "Hint: press F1 for help RUN ADDRESS:" STR(COM_LOAD_ADDR) " version " SHELLVER ANSI_RESET
#define APP_MSG_EXIT NEWLINE "Exiting to the monitor." NEWLINE "Bye, bye !" NEWLINE NEWLINE

#define CMD_BUF_MAX 80
#define CMD_TOKEN_MAX 5
#define EDIT_BUF_MAX 2048
#define RUN_ARGS_BASE 0x0200      // where argc/argv block is stored for run (safe area outside shell BSS)
#define RUN_ARGS_MAX 4
#define RUN_ARGS_BUF 32
#define RUN_ARGS_BLOCK_SIZE (1 + RUN_ARGS_MAX*2 + RUN_ARGS_BUF)
#define HEXDUMP_LINE_SIZE 16

#define TX_READY (RIA.ready & RIA_READY_TX_BIT)
#define RX_READY (RIA.ready & RIA_READY_RX_BIT)
#define TX_READY_SPIN while(!TX_READY)
#define RX_READY_SPIN while(!RX_READY)

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

// wait on clock
uint32_t ticks = 0; // for PAUSE(millis)
#define PAUSE(millis) ticks=clock(); while(clock() < (ticks + millis)){}

#define APPFLAG_RTC 0b00000001
unsigned char appflags = 0b00000000;

static const char default_shelldir[] = SHELLDIRDEFAULT;
char shelldir[64];
char *filename[20] = {"                    "};

static const char hexdigits[] = "0123456789ABCDEF";

typedef struct {
    int bytes;
    int lastbytes;
    char buffer[CMD_BUF_MAX+1];
    char lastbuffer[CMD_BUF_MAX+1];
} cmdline_t;

typedef struct {
    const char *cmd;
    const char *msgsyntax;
    const char *msgerror;
    int (*func)(int argc, char **argv);
} cmd_t;

typedef void (*char_stream_func_t)(const char *buf, int size);
typedef void (*read_data_func_t)(uint8_t *buf, uint16_t addr, uint16_t size);

typedef struct {
    char name[FNAMELEN];
    unsigned long fsize;
    unsigned char fattrib;
    unsigned fdate;
    unsigned ftime;
} dir_list_entry_t;

static char dir_cwd[FNAMELEN];
static f_stat_t dir_ent;
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
static char *exe_argv[CMD_TOKEN_MAX+1];
static unsigned char run_args_backup[RUN_ARGS_BLOCK_SIZE];
static char drv_args_buf[4] = {0};
static char *drv_args[2] = { (char *)"drive", drv_args_buf };
static int filehex_fd = -1;
static uint32_t filehex_base = 0;

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
int cmd_cp(int, char **);
int cmd_cpm(int, char **);
int cmd_ls(int, char **);
int cmd_drive(int, char **);
int cmd_drives(int, char **);
// int cmd_edit(int, char **);
int cmd_exit(int, char **);
int cmd_list(int, char **);
int cmd_mem(int, char **);
int cmd_hex(int, char **);
int cmd_memr(int, char **);
int cmd_memx(int, char **);
int cmd_mkdir(int, char **);
int cmd_mv(int, char **);
int cmd_phi2(int, char **);
int cmd_rename(int, char **);
int cmd_rm(int, char **);
int cmd_run(int, char **);
int cmd_stat(int, char **);
int cmd_time(int, char **);
// TO DO
// int cmd_cart(int, char **); // load and run <romname>.rp6502
// int cmd_crx(int, char**); // receive file from RIA UART
// int cmd_ctx(int, char**); // send file to RIA UART

static const cmd_t commands[] = {
    { "bload",  "", "", cmd_bload},
    { "brun",   "", "", cmd_brun},
    { "bsave",  "", "", cmd_bsave},
    { "cd",     "", "", cmd_cd},
    { "chmod",  "", "", cmd_chmod},
    { "cls",    "", "", cmd_cls },
    { "cp",     "", "", cmd_cp},
    { "cpm",     "", "", cmd_cpm},
    { "com",    "", "", cmd_com},
    { "exe",    "", "", cmd_exe},
    { "ls",     "", "", cmd_ls},
    { "drive",  "", "", cmd_drive},
    { "drives", "", "", cmd_drives},
 //   { "edit",   "", "", cmd_edit},
    { "exit",   "", "", cmd_exit},
    { "list",   "", "", cmd_list},
    { "mem",    "", "", cmd_mem},
    { "memr",   "", "", cmd_memr },
    { "memx",   "", "", cmd_memx },
    { "hex",    "", "", cmd_hex },
    { "mkdir",  "", "", cmd_mkdir},
    { "mv",     "", "", cmd_mv},
    { "phi2",   "", "", cmd_phi2},
    { "rename", "", "", cmd_rename},
    { "rm",     "", "", cmd_rm},
    { "run",    "", "", cmd_run},
    { "stat",   "", "", cmd_stat},
    { "time",   "", "", cmd_time },
// TO DO
//    { "cart",    "", "", cmd_cart},
//    { "crx",   "", "", cmd_crx },
//    { "ctx",   "", "", cmd_ctx },
};

static void load_setup(void);
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
// int set_time(void);
void show_time(void);
int hexstr(char *str, uint8_t val);
void hexdump(uint16_t addr, uint16_t bytes, char_stream_func_t streamer, read_data_func_t reader);
void cls();
void prompt(bool first_time);
static int tokenize(char *buf, int maxBuf, char **tokenList, int maxTokens);
static int execute(cmdline_t *cl);
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
int cmd_cp(int argc, char **argv);
int cmd_cpm(int argc, char **argv);
int cmd_ls(int argc, char **argv);
int cmd_drive(int argc, char **argv);
int cmd_drives(int argc, char **argv);
// int cmd_edit(int argc, char **argv);
int cmd_exit(int status, char **);
int cmd_list(int argc, char **argv);
int cmd_mem(int argc, char **argv);
int cmd_memr(int argc, char **argv);
int cmd_memx(int argc, char **argv);
int cmd_mkdir(int argc, char **argv);
int cmd_mv(int argc, char **argv);
int cmd_phi2(int argc, char **argv);
int cmd_rename(int argc, char **argv);
int cmd_rm(int argc, char **argv);
int cmd_run(int argc, char **argv);
int cmd_stat(int argc, char **argv);
int cmd_time(int argc, char **argv);
extern char _BSS_RUN__[];
extern char _BSS_SIZE__[];

// TO DO
// int cmd_cart(int argc, char **argv);
// int cmd_crx(int argc, char **argv);
// int cmd_ctx(int argc, char **argv);

// ------------------- SCRATCHPAD -----------------------
/*

#define GFX_CANVAS_CONSOLE 0
#define GFX_CANVAS_320x240 1
#define GFX_CANVAS_320x180 2
#define GFX_CANVAS_640x480 3
#define GFX_CANVAS_640x360 4

#define GFX_MODE_CONSOLE   0
#define GFX_MODE_CHARACTER 1
#define GFX_MODE_TILE      2
#define GFX_MODE_BITMAP    3
#define GFX_MODE_SPRITE    4

#define GFX_PLANE_0 0
#define GFX_PLANE_1 1
#define GFX_PLANE_2 2

#define GFX_FONT_CUSTOM 0xF700        // custom fontset
#define GFX_CHARACTER_FONT_PTR GFX_FONT_CUSTOM // GFX_FONT_CUSTOM // standard fontset
#define GFX_CHARACTER_PAL_PTR  0xFFFF
#define GFX_CANVAS_DATA   0x0000
#define GFX_CANVAS_STRUCT 0xFF00

#define GFX_CHARACTER_bpp1         0b00000000
#define GFX_CHARACTER_bpp4         0b00000010
#define GFX_CHARACTER_bpp4_REVERSE 0b00000001
#define GFX_CHARACTER_bpp8         0b00000011
#define GFX_CHARACTER_bpp16        0b00000100
#define GFX_CHARACTER_FONTSIZE8x16 0b00001000
#define GFX_CHARACTER_FONTSIZE8x8  0b00000000

#define GFX_CANVAS_SIZE GFX_CANVAS_640x480

#define GFX_CANVAS_WIDTH  640
#define GFX_CANVAS_HEIGHT 480

#define GFX_FONTSIZE8 8
//#define GFX_FONTSIZE16 16

#define GFX_CHARACTER_COLUMNS (GFX_CANVAS_WIDTH / 8)
#ifdef GFX_FONTSIZE8
#define GFX_CHARACTER_ROWS    (GFX_CANVAS_HEIGHT / GFX_FONTSIZE8)
#else
#define GFX_CHARACTER_ROWS    (GFX_CANVAS_HEIGHT / GFX_FONTSIZE16)
#endif

static uint16_t canvas_struct = GFX_CANVAS_STRUCT;
static uint16_t canvas_data = GFX_CANVAS_DATA;
// static uint8_t plane = GFX_PLANE_1;
static uint8_t canvas_type = GFX_CANVAS_SIZE;
// static uint16_t canvas_w = GFX_CANVAS_WIDTH;
// static uint16_t canvas_h = GFX_CANVAS_HEIGHT;
static uint8_t canvas_c = GFX_CHARACTER_COLUMNS;
static uint8_t canvas_r = GFX_CHARACTER_ROWS;
// static uint8_t font_w = 8;
#ifdef GFX_FONTSIZE8
// static uint8_t font_h = 8;
#endif
#ifdef GFX_FONTSIZE16
static uint8_t font_h = 16;
#endif
static uint8_t fg_clr = DARK_GRAY;
static uint8_t bg_clr = BLACK;
// static uint8_t curcol = 0; // current column
// static uint8_t currow = 0; // current row
static char msg[80] = {0};

void DrawChar(uint8_t row, uint8_t col, char ch, uint8_t fg, uint8_t bg);
void GetChar(uint8_t row, uint8_t col, char *pch, uint8_t *pfg, uint8_t *pbg);
bool BackupChars(uint8_t row, uint8_t col, uint8_t width, uint8_t height, uint8_t *pstash);
bool RestoreChars(uint8_t row, uint8_t col, uint8_t width, uint8_t height, uint8_t *pstash);
void printText(char *text, uint8_t x, uint8_t y, uint8_t fg, uint8_t bg);
void DrawLetters_PL(uint8_t x, uint8_t y, uint8_t fg, uint8_t bg);
void DrawFontTable(uint8_t x, uint8_t y, uint8_t fg, uint8_t bg, uint8_t bgc, uint8_t bgr);

void InitTerminalFont(void);
void ClearDisplayMemory(void);
void ClearDisplay(uint8_t fg, uint8_t bg);
void InitDisplay(void);

*/

// EOF shell.h
