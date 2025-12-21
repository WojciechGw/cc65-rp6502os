/* ext-rx-echo.c - C89, cc65 (Picocomputer RP6502-RIA UART)
 *
 * $FFE0 READY:
 *   bit 7 - TX FIFO not full (OK to send)
 *   bit 6 - RX FIFO has data ready
 * $FFE1 TX: write byte to UART
 * $FFE2 RX: read byte from UART
 *
 * Uwaga: nie używaj bezpośrednio UART jednocześnie z funkcjami stdio RP6502-OS. :contentReference[oaicite:1]{index=1}
 */

#include <time.h>

typedef unsigned int u16;

typedef struct {
    volatile unsigned char READY; /* $FFE0 */
    volatile unsigned char TX;    /* $FFE1 */
    volatile unsigned char RX;    /* $FFE2 */
} ria_uart_t;

#define RIA_BASE_ADDR ((u16)0xFFE0u)
#define RIA_PTR       ((ria_uart_t*)(RIA_BASE_ADDR))
#define RIA           (*RIA_PTR)


#define RIA_READY_TX_BIT 0x80u /* bit 7 */
#define RIA_READY_RX_BIT 0x40u /* bit 6 */
#define RX_TIMEOUT_TICKS 200u /* ~2s */

#define RX_READY (RIA.READY & RIA_READY_RX_BIT)
#define TX_READY (RIA.READY & RIA_READY_TX_BIT)

u16 bytecounter;

/* Czeka aż TX FIFO nie będzie pełne i wysyła bajt. */
static void ria_tx_putc_blocking(unsigned char c)
{
    while (TX_READY == 0) {
        /* busy wait */
    }
    RIA.TX = c;
}

static void drop_console_rx(void) {
    unsigned char c;
    while (RX_READY) 
    {
        c = RIA.RX;
        bytecounter++;
        ria_tx_putc_blocking(c);
    }
}

/* Echo: odbiera bajty i wysyła je na terminal. */
int main(void)
{
    unsigned char c;
    clock_t wait_start;
    int action;

    bytecounter = 0;
    // start 2 second timeout
    wait_start = clock();

    while(1) {

        while (!RX_READY) {
            // if 2 seconds pause in transmited data occur break to drop_console_rx()
            if ((clock() - wait_start) >= (clock_t)RX_TIMEOUT_TICKS) break;
        }
        c = RIA.RX;
        bytecounter++;
        ria_tx_putc_blocking(c);
        // start new 2 seconds timeout
        wait_start = clock();
        
    }

    drop_console_rx();
    printf("Transmission end. %i bytes", bytecounter);

    return 0;
}
