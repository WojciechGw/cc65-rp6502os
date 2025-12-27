#include "commons.h"

#define ARRAY_SIZE(x) (sizeof(x) / sizeof(x[0]))
#define NEWLINE "\r\n"

typedef struct {
    const char *cmd;
    const char *help;
    const char *usage;
} cmd_t;

static const cmd_t commands[] = {
    { "drive",  "set active drive", 
                "drive 0:"},
    { "drives", "show available drives", 
                "drives"},
    { "cd",     "change active directory", 
                "cd <directory>"},
    { "mkdir",  "create directory", 
                "mkdir <directory>"},
    { "chmod",  "set file attributes",
                "chmod file.bin A+"},
    { "cp",     "copy file", 
                "cp <source> <destination>"},
    { "cpm",    "copy/move multiple files, wildcards allowed",
                "cpm <source> <destination> (copy file)" NEWLINE
                "cpm <source> <destination> /m (move file)"},
    { "mv",     "move/rename a file or directory",
                "mv <source> <destination>"},
    { "rename", "rename a file or directory",
                "rename <oldname> <newname>"},
    { "rm",     "remove a file/files, wildcards allowed",
                "rm <filename>"},
    { "list",   "show a file content",
                "list <filename>"},
    { "ls",     "list active directory",
                "ls"},
//    { "edit",   "simple text editor",
//                "edit <filename>"},
    { "stat",   "show file/directory info", 
                "stat <filename>"},
    { "bload",  "load binary file to RAM/XRAM", 
                "bload code.bin 0600 (load file code.bin to RAM memory at address 0x0600)"},
    { "bsave",  "save RAM/XRAM to binary file", 
                "bsave code.bin 0600 256 (save 256 bytes start from RAM address 0x0600)" NEWLINE
                "bsave picture.bin 0000 8192 /x (save 8192 bytes start from XRAM address 0x0000)"},
    { "brun",   "load binary file to RAM and run",
                "brun hello.bin A300 (load and run binary file hello.bin at address 0xA300)"},
    { "com",    "load .com binary and run", 
                "com hello.com A000 (load and run file hello.com at address 0xA000)"},
    { "run",    "run code at address", 
                "run A000 (run code at 0xA000)"},
    { "mem",    "show memory informations : lowest and highest RAM address and size available for user program",
                "mem"},
    { "memr",   "show RAM from given address", 
                "memx 0x0600 512 (show 512 bytes of RAM start from address 0x0600)" },
    { "memx",   "show XRAM from given address", 
                "memx 0xA500 256 (show 256 bytes of XRAM start from address 0xA500)" },
    { "hex",    "dump file contents to screen", 
                "hex <filename> 0x0600 512 (show 512 bytes of a file start from offset 0x0600)" },
    { "cls",    "clear terminal", 
                "cls" },
    { "time",   "show local date and time", 
                "time" },
    { "phi2",   "show CPU clock frequency", 
                "phi2"},
    { "exit",   "exit to the system monitor", 
                "exit"},
};

static const cmd_t commands_ext[] = {
    { "calendar",   "run calendar application", 
                    "calendar                - current month" NEWLINE
                    "calendar /p yyyy [1-12] - particular month" NEWLINE
                    "calendar /n [yyyy]      - current or particular and neighbouring months" NEWLINE
                    "calendar /q [1-4]       - current or particular quarter" NEWLINE
                    "calendar /y [yyyy]      - current or particular year" },
    { "dir",        "show active drive directory, wildcards allowed",
                    "dir *.rp6502 (only .rp6502 files)" NEWLINE
                    "dir /da (sorted by date ascending)"},
    { "help",       "show these informations", 
                    "help" NEWLINE
                    "help mkdir" NEWLINE
                    "user can also write a command and press <F1> key to get help information" },
    { "keyboard",   "run keyboard visualiser", 
                    "keyboard" NEWLINE
                    "for exit press both Shift keys" },
    { "label",      "show or set active drive's volume label", 
                    "label          - show active drive's label" NEWLINE
                    "label NEWLABEL - set active drive label to NEWLABEL" },
    { "mass",       "Mini ASSembler application for OS Shell",
                    "mass                            - instant write and compile a code" NEWLINE
                    "mass <source>                   - <source> file, out.bin as a result" NEWLINE
                    "mass <source> -o <destination>  - <source> file, <destination> as a result"},
    { "courier",    "Courier - file transfer in/out application for OS Shell ", 
                    "courier        - run application" },
};

int main(int argc, char **argv) {
    int i;

    /* If a command name is provided, show only its description */
    if(argc >= 1 && argv[0][0]) {
        for(i = 0; i < ARRAY_SIZE(commands); i++) {
            if(strcmp(argv[0], commands[i].cmd) == 0) {
                printf(NEWLINE 
                       "Command: %s" NEWLINE 
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
    }

    /* If a command name is provided, show only its description */
    if(argc == 1 && argv[0][0]) {
        for(i = 0; i < ARRAY_SIZE(commands_ext); i++) {
            if(strcmp(argv[0], commands_ext[i].cmd) == 0) {
                printf(NEWLINE 
                       "Command: %s" NEWLINE 
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
    printf("\x1b" "c" "\x1b[2;1HOS Shell > Help information" NEWLINE NEWLINE
           "Description of a specified command : help <command>" NEWLINE NEWLINE
           "internal commands (case sensitive):" NEWLINE NEWLINE);
    for(i = 0; i < ARRAY_SIZE(commands); i++) {
        printf("%-10s", commands[i].cmd);
        if((i & 7) == 7) {
            printf(NEWLINE);
        }
    }
    printf(NEWLINE NEWLINE
           "external commands (*.com files in <0:-7:>/SHELL) (case insensitive):" NEWLINE NEWLINE);
    for(i = 0; i < ARRAY_SIZE(commands_ext); i++) {
        printf("%-10s", commands_ext[i].cmd);
        if((i & 7) == 7) {
            printf(NEWLINE);
        }
    }

    printf(NEWLINE NEWLINE "Keyboard:" NEWLINE NEWLINE);
    printf("<F1>    show these informations" NEWLINE);
    printf("<F2>    show keyboard status visualiser" NEWLINE);
    printf("<F3>    current date/time and calendar" NEWLINE);
    printf("<LEFT>  change active drive to previous if available" NEWLINE);
    printf("<RIGHT> change active drive to next if available" NEWLINE);
    printf("<UP>    recall last command" NEWLINE);
    printf("<DOWN>  a directory of active drive/catalog" NEWLINE NEWLINE);

    return 0;
}
