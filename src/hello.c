#include <rp6502.h>
#include <stdio.h>
#include <stdint.h>

char * menu[5] =  {"edit", "run", "save", "load", "quit"};

void cls()
{
    printf("\x1b[2J");
}

void setpos(uint8_t row, uint8_t col)
{
    printf("\x1b[%d;%dH", row, col);
}

void printat(uint8_t row, uint8_t col, char *txt)
{
    printf("\x1b[%d;%dH%s", row, col, txt);
}

void main()
{
    cls();
    printat(10,10,"Hello, world!");
    printat(20,20,"END");
    puts("\n");

    while(1){}
}
