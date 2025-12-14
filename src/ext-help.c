#include <rp6502.h>
#include <stdio.h>
#include <string.h>

#define ARRAY_SIZE(x) (sizeof(x) / sizeof(x[0]))

typedef struct {
    const char *cmd;
    const char *help;
    const char *example;
} cmd_t;

static const cmd_t commands[] = {
    { "dir",    "show active drive directory, wildcards allowed", "dir *.rp6502 (only .rp6502 files)\r\ndir /da (sorted by date ascending)"},
    { "drive",  "set active drive", "drive 0:"},
    { "drives", "show available drives", "drives"},
    { "cd",     "change active directory", "cd <directory>"},
    { "mkdir",  "create directory", "mkdir <directory>"},
    { "chmod",  "set file attributes", "chmod file.bin A+"},
    { "cp",     "copy file", "cp <source> <destination>"},
    { "cm",     "copy/move multiple files, wildcards allowed", "cp <source> <destination> (copy file)\r\ncp <source> <destination> /m (move file)"},
    { "mv",     "move/rename a file or directory", "cmd_mv"},
    { "rename", "rename a file or directory", "cmd_rename"},
    { "rm",     "remove a file/files", "cmd_rm"},
    { "list",   "show a file content", "cmd_list"},
    { "edit",   "simple text editor", "cmd_edit"},
    { "stat",   "show file/directory info", "cmd_stat"},
    { "bload",  "load binary file to RAM/XRAM", "bload code.bin 0600 (load file code.bin to RAM memory at address 0x0600)"},
    { "bsave",  "save RAM/XRAM to binary file", "bsave code.bin 0600 256 (save 256 bytes from RAM address 0x0600)\r\nbsave picture.bin 0000 8192 /x (save 8192 bytes from XRAM address 0x0000)"},
    { "brun",   "load binary file to RAM and run", "brun hello.bin A300 (load and run binary file hello.bin at address 0xA300)"},
    { "com",    "load .com binary and run", "com hello.com A000 (load and run file hello.com at address 0xA000)"},
    { "run",    "run code at address", "run A000 (run code at 0xA000)"},
    { "mem",    "show memory information", "mem"},
    { "memr",   "show RAM from given address", "memx 0x0600 512 (show 512 bytes of RAM from address 0x0600)" },
    { "memx",   "show XRAM from given address", "memx 0xA500 256 (show 256 bytes of XRAM from address 0xA500)" },
    { "cls",    "clear terminal", "cls" },
    { "time",   "show local time", "time" },
    { "phi2",   "show CPU clock frequency", "phi2"},
    { "help",   "show these informations", "help\r\nhelp mkdir" },
    { "exit",   "exit to the system monitor", "exit"},
};

int main(int argc, char **argv) {
    int i;

    /* If a command name is provided, show only its description */
    if(argc >= 1 && argv[0][0]) {
        for(i = 0; i < ARRAY_SIZE(commands); i++) {
            if(strcmp(argv[0], commands[i].cmd) == 0) {
                printf("\r\nCommand: %s\r\n---------------------\r\n\r\nDescription:\r\n%s\r\n\r\nExamples:\r\n%s\r\n\r\n", commands[i].cmd, commands[i].help, commands[i].example);
                return 0;
            }
        }
        printf("Command not found\r\n");
        return -1;
    }

    /* No args: print full list */
    printf("\r\nAvailable commands (case sensitive):\r\n\r\n");
    for(i = 0; i < ARRAY_SIZE(commands); i++) {
        printf("%-10s", commands[i].cmd);
        if((i & 7) == 7) {
            printf("\r\n");
        }
    }
    if((i & 7) != 0) printf("\r\n");
    printf("\r\nDescription of a specific command : help <command name>\r\n");

    printf("\r\nKeys:\r\n\r\n");
    printf("<F1>   \tshow these informations\r\n");
    printf("<LEFT> \tchange active drive to previous if available\r\n");
    printf("<RIGHT>\tchange active drive to next if available\r\n");
    printf("<UP>   \trecall last command\r\n");
    printf("<DOWN> \ta directory of active drive\r\n\r\n");

    return 0;
}
