#include <rp6502.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

int main(int argc, char **argv) {
    int i;

    if(argc < 1) {
        printf("Usage: label <new_label>\r\n");
        return -1;
    }

    /*
    printf("\r\n--------------\r\nargc=%d\r\n", argc);
    for(i = 0; i < argc; i++) {
        printf("argv[%d]=\"%s\"\r\n", i, argv[i]);
    }
    */

    /* Uppercase label argument (argv[0]) in place and truncate to 11 chars */
    for(i = 0; argv[0][i] && i < 11; i++) {
        argv[0][i] = (char)toupper((unsigned char)argv[0][i]);
    }
    argv[0][i] = 0;

    if(f_setlabel(argv[0]) < 0) {
        printf("label: set failed\r\n");
        return -1;
    }
    printf("current drive label set to \"%s\"\r\n", argv[0]);
    return 0;
}
