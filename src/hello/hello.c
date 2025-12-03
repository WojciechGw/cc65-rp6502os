#include <rp6502.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
    int i;

    if(argc == 1 && strcmp(argv[0], "/?") == 0) {
        // notice lack of filename extension
        printf ("Hello\r\nScaffolding for external command\r\nUsage: hello [arg1,...]\r\n"); 
    }
    
    printf("\r\n--------------\r\nargc=%d\r\n", argc);
    for(i = 0; i < argc; i++) {
        printf("argv[%d]=\"%s\"\r\n", i, argv[i]);
    }

    return 0;
}
