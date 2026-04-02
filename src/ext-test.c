// .COM razemOS VS Code Scaffolding for CC65
//
// command : test

#include "commons.h"

#define APPVER "20260401.1336"
#define APPNAME "Test"
#define APPDIRDEFAULT "MSC0:/"
#define APP_MSG_TITLE CSI_RESET CSI "[2;1H" CSI HIGHLIGHT_COLOR " " APPNAME " > " ANSI_RESET " " APPNAME ANSI_DARK_GRAY CSI "[2;60H" "version " APPVER ANSI_RESET
#define APP_MSG_START ANSI_DARK_GRAY CSI "[4;1H" "Welcome to test application." ANSI_RESET
#define APP_WORKBENCH_POS CSI "[6;1H"

int main(int argc, char **argv) {
    int i;
    char buff[32] = "";
    char argv_last[16] = "";
    int fd;    
    
    if(argc == 1 && strcmp(argv[0], "/?") == 0) {
        // notice lack of filename extension
        printf ("Test" NEWLINE
                "Scaffolding for external command" NEWLINE
                "Usage: test [arg1,...]" NEWLINE
                NEWLINE); 
    }

    printf(CSI_RESET APP_MSG_TITLE APP_MSG_START APP_WORKBENCH_POS);

    if (argc > 0) {
        strncpy(argv_last, argv[argc - 1], sizeof(argv_last) - 1);
        argv_last[sizeof(argv_last) - 1] = '\0';
    }

    // if (strcmp(argv[0], "/c") == 0  && strcmp(argv[1], "/y") == 0 )

    if(argc > 0 && strcmp(argv[argc - 1], "/a") == 0) {
        printf("\r\n--------------\r\nargc=%d\r\n", argc);
        for(i = 0; i < argc; i++) {
            printf("argv[%d]=\"%s\"" NEWLINE, i, argv[i]);
        }
        printf("last argument=%s" NEWLINE NEWLINE, argv_last);
    }

    sprintf(buff,"Hello!" NEWLINE "\0",strlen(buff));    
    fd = open("TTY:", O_RDWR);
    write(fd,buff,sizeof(buff)-1);
    close(fd);

    fd = open("CON:", O_RDWR);
    write(fd,buff,sizeof(buff)-1);
    close(fd);

    return 0;
}
