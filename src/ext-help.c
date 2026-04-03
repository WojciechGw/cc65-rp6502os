#include "commons.h"

#define APPVER "20260403.1824"

#define APP_HEADER CSI_CLS CSI "[2;1H" CSI HIGHLIGHT_COLOR " razemOS > " ANSI_RESET " Help information                              " ANSI_DARK_GRAY "version " APPVER ANSI_RESET
#define APP_FOOTER ANSI_DARK_GRAY  "________________________________________________________________________________" NEWLINE NEWLINE ANSI_RESET

#define ARRAY_SIZE(x) (sizeof(x) / sizeof(x[0]))
#define NEWLINE "\r\n"

typedef struct {
    const char *cmd;
    const char *help;
    const char *usage;
} cmd_t;

static const cmd_t commands[] = {
    { "bload",  "load binary file to RAM/XRAM", 
                "bload code.bin 0600 (load file code.bin to RAM memory at address 0x0600)"},
    { "bsave",  "save RAM/XRAM to binary file", 
                "bsave code.bin 0600 256 (save 256 bytes start from RAM address 0x0600)" NEWLINE
                "bsave picture.bin 0000 8192 /x (save 8192 bytes start from XRAM address 0x0000)"},
    { "brun",   "load binary file to RAM and run",
                "brun hello.bin A300 (load and run binary file hello.bin at address 0xA300)"},
    { "cd",     "change active directory", 
                "cd <directory>"},
    { "chmod",  "set file attributes",
                "chmod file.bin A+"},
    { "cls",    "reset terminal", 
                "cls" },
    { "com",    "load .com binary and run", 
                "com hello.com A000 (load and run file hello.com at address 0xA000)"},
    { "cp",     "copy file", 
                "cp <source> <destination>"},
    { "cpm",    "copy/move multiple files, wildcards allowed",
                "cpm <source> <destination> (copy file)" NEWLINE
                "cpm <source> <destination> /m (move file)"},
    { "drive",  "set active drive", 
                "drive 0:"},
    { "exit",   "exit to the system monitor", 
                "exit"},
    { "hex",    "dump file contents to screen", 
                "hex <filename> 0x0600 512 (show 512 bytes of a file start from offset 0x0600)" },
    { "list",   "show a file content",
                "list <filename>"},
    { "ls",     "list active directory",
                "ls"},
    { "mem",    "show OS Shell RAM informations" NEWLINE "lowest and highest RAM address and size available for user program",
                "mem"},
//    { "memr",   "show RAM from given address", 
//                "memx 0x0600 512 (show 512 bytes of RAM start from address 0x0600)" },
//    { "memx",   "show XRAM from given address", 
//                "memx 0xA500 256 (show 256 bytes of XRAM start from address 0xA500)" },
    { "mkdir",  "create directory", 
                "mkdir <directory>"},
    { "mv",     "move/rename a file or directory",
                "mv <source> <destination>"},
    { "peek",   "memory viewer",
                "peek 0xA000 128 (show 128 bytes of base RAM start from address 0xA000)" NEWLINE
                "peek 0xF000 256 /X (show 256 bytes of XRAM start from address 0xF000)" },
    { "phi2",   "show CPU clock frequency", 
                "phi2"},
    { "rename", "rename a file or directory",
                "rename <oldname> <newname>"},
    { "rm",     "remove a file/files, wildcards allowed",
                "rm <filename>"},
    { "run",    "run code at address", 
                "run A000 (run code at 0xA000)"},
    { "stat",   "show file/directory info", 
                "stat <filename>"},
    { "time",   "show local date and time", 
                "time" },
};

static const cmd_t commands_rom[] = {
    { "date",       "shows current time and/or calendar in various ways",
                    "time                        - current time and date" NEWLINE
                    "time /s yyyy-mm-dd hh:mm:ss - set RTC to given date & time " NEWLINE
                    "time /a                     - current time, date and calendar of current month" NEWLINE
                    "time /c                     - calendar of current month" NEWLINE
                    "time /c /p yyyy mm          - calendar of particular month" NEWLINE
                    "time /c /n                  - calendar of current and neighbouring months" NEWLINE
                    "time /c /q                  - calendar of current quarter" NEWLINE
                    "time /c /y                  - calendar of current year" NEWLINE},
    { "crx",        "file transfer application - receiver (UART)", 
                    "crx" },
    { "ctx",        "file transfer application - sender (UART)", 
                    "ctx" },
    { "dir",        "show active drive directory, wildcards allowed",
                    "dir *.rp6502 (only .rp6502 files)" NEWLINE
                    "dir /da (sorted by date ascending)"},
    { "drives",     "show available drives", 
                    "drives"},
    { "help",       "show help informations", 
                    "help" NEWLINE
                    "help mkdir" NEWLINE
                    "user can also write a command and press <F1> key to get help information" },
    { "keyboard",   "keyboard visualiser", 
                    "keyboard" NEWLINE
                    "for exit press both Shift keys" },
    { "label",      "show or set volume label of active drive", 
                    "label          - show label of active drive" NEWLINE
                    "label NEWLABEL - set active drive label to NEWLABEL" },
    { "view",       "show BMP 640x480xbpp1", 
                    "view filename.bmp"}
};

static const cmd_t commands_ext[] = {
    { "hass",       "Handy ASSembler for 65C02S list hass-manual-en.txt for more informations",
                    "hass                            - interactive code entry and assembly" NEWLINE
                    "hass <source>                   - <source> file, out.bin as a result" NEWLINE
                    "hass <source> -o <destination>  - <source> file, <destination> as a result"},
};

int main(int argc, char **argv) {
    int i;

    /* If a command name is provided, show only its description */
    if(argc >= 1 && argv[0][0]) {
        for(i = 0; i < ARRAY_SIZE(commands); i++) {
            if(strcmp(argv[0], commands[i].cmd) == 0) {
                printf(NEWLINE "Command: %s" NEWLINE 
                       "---------------------" NEWLINE 
                       NEWLINE 
                       "Description:" NEWLINE 
                       "%s" NEWLINE 
                       NEWLINE 
                       "Usage:" NEWLINE 
                       "%s" NEWLINE NEWLINE, commands[i].cmd, commands[i].help, commands[i].usage);
                return 0;
            }
        }

        for(i = 0; i < ARRAY_SIZE(commands_rom); i++) {
            if(strcmp(argv[0], commands_rom[i].cmd) == 0) {
                printf(NEWLINE "Command: %s" NEWLINE 
                       "---------------------" NEWLINE 
                       NEWLINE 
                       "Description:" NEWLINE 
                       "%s" NEWLINE 
                       NEWLINE 
                       "Usage:" NEWLINE 
                       "%s" NEWLINE NEWLINE, commands_rom[i].cmd, commands_rom[i].help, commands_rom[i].usage);
                return 0;
            }
        }

        for(i = 0; i < ARRAY_SIZE(commands_ext); i++) {
            if(strcmp(argv[0], commands_ext[i].cmd) == 0) {
                printf(NEWLINE "Command: %s" NEWLINE 
                       "---------------------" NEWLINE 
                       NEWLINE 
                       "Description:" NEWLINE 
                       "%s" NEWLINE 
                       NEWLINE 
                       "Usage:" NEWLINE 
                       "%s" NEWLINE NEWLINE, commands_ext[i].cmd, commands_ext[i].help, commands_ext[i].usage);
                return 0;
            }
        }

        printf(NEWLINE "Command not found" NEWLINE);
        return -1;
    }

    /* No args: print full list */
    printf(APP_HEADER NEWLINE NEWLINE
           "Description of a specified command : help <command>");

    printf( NEWLINE NEWLINE "INTERNAL (case sensitive):" NEWLINE);
    for(i = 0; i < ARRAY_SIZE(commands); i++) {
        printf("%-10s", commands[i].cmd);
        if((i & 7) == 7) {
            printf(NEWLINE);
        }
    }

    printf(NEWLINE NEWLINE "INTERNAL IN ROM: (case insensitive):" NEWLINE);
    for(i = 0; i < ARRAY_SIZE(commands_rom); i++) {
        printf("%-10s", commands_rom[i].cmd);
        if((i & 7) == 7) {
            printf(NEWLINE);
        }
    }

    printf(NEWLINE NEWLINE "EXTERNAL IN MSC0:/SHELL directory (case insensitive):" NEWLINE);
    for(i = 0; i < ARRAY_SIZE(commands_ext); i++) {
        printf("%-10s", commands_ext[i].cmd);
        if((i & 7) == 7) {
            printf(NEWLINE);
        }
    }

    printf(NEWLINE NEWLINE "Keyboard:" NEWLINE);
    printf("<F1>    help informations" NEWLINE);
    printf("<F2>    keyboard visualiser" NEWLINE);
    printf("<F3>    current date/time and calendar" NEWLINE);
    printf("<LEFT>  change active drive to previous if available" NEWLINE);
    printf("<RIGHT> change active drive to next if available" NEWLINE);
    printf("<UP>    recall last command" NEWLINE);
    printf("<DOWN>  a directory of active drive/catalog" NEWLINE);
    printf(APP_FOOTER);

    return 0;
}
