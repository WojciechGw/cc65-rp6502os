#include "commons.h"

// #define DEBUG

#define FNAMELEN 64

int main(int argc, char **argv) {
    int i;
    char cwd[FNAMELEN];
    char drv[3] = {0};
    char label[16];

#ifdef DEBUG
    printf("\r\n--------------\r\nargc=%d\r\n", argc);
    for(i = 0; i < argc; i++) {
        printf("argv[%d]=\"%s\"\r\n", i, argv[i]);
    }
#endif

    if(argc == 0) {
        if(f_getcwd(cwd, sizeof(cwd)) < 0 || cwd[4] != ':') {
            printf("label: unable to determine current drive\r\n");
            return -1;
        }
        cwd[5] = 0; // NULL terminated after drive symbol

        drv[0] = cwd[3];
        drv[1] = ':';
        drv[2] = 0;
        if(f_getlabel(drv, label) < 0) {
            printf("label: get failed\r\n");
            return -1;
        }
        if(label[0]) {
            printf("Current drive %s label is \"%s\"\r\n\r\n", cwd, label);
        } else {
            printf("Current drive %s has no label\r\n\r\n", cwd);
        }
        return 0;
    }

    /* Uppercase label argument (argv[0]) in place and truncate to 11 chars */
    for(i = 0; argv[0][i] && i < 11; i++) {
        argv[0][i] = (char)toupper((unsigned char)argv[0][i]);
    }
    argv[0][i] = 0;

    if(f_setlabel(argv[0]) < 0) {
        printf("label: set failed\r\n");
        return -1;
    }
    printf("Current drive label set to \"%s\"\r\n", argv[0]);
    return 0;
}
