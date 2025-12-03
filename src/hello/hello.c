#include <rp6502.h>
#include <stdio.h>

int main(int argc, char **argv) {
    int i;

    printf ("scaffolding for external file command\r\nusage: hello [arg1,...]");

    printf("\r\nargc=%d\r\n", argc);
    for(i = 0; i < argc; i++) {
        printf("arg[%d]=\"%s\"\r\n", i, argv[i]);
    }
    return 0;
}
