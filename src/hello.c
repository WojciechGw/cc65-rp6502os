#include <rp6502.h>
#include <stdio.h>
#include <stdint.h>

void cls()
{
    printf("\x1b[2J");
}

void setpos(uint8_t col, uint8_t row)
{
    printf("\x1b[%d;%dH", col, row);
}

void main()
{
    cls();
    setpos(20,10);
    puts("Hello, world!");
    setpos(22,10);
    printf("END\n");
}
