#include <rp6502.h>
#include <stdio.h>

#define ESC "\033"

int main(void)
{
    printf(ESC ")0");     /* G1 = DEC Special Graphics */

    putchar(0x0E);        /* SO: przełącz na G1 */
    printf("lqqqqk\n");   /* znaki DEC: narożniki i linie */
    printf("xMENUx\n");
    printf("mqqqqj\n");
    putchar(0x0F);        /* SI: wróć do G0 */

    return 0;
}