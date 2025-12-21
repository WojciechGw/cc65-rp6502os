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

/* Czeka aż TX FIFO nie będzie pełne i wysyła bajt. */
static void ria_tx_putc_blocking(unsigned char c)
{
    while ((RIA.READY & RIA_READY_TX_BIT) == 0) {
        /* busy wait */
    }
    RIA.TX = c;
}

/* Echo: odbiera bajty i wysyła je na terminal. */
int main(void)
{
    unsigned char c;

    while(1) {

        c = RIA.RX;
        /* czekaj na bajt w RX FIFO */
        while ((RIA.READY & RIA_READY_RX_BIT) == 0) {
            /* busy wait */
        }
        c = RIA.RX;
        ria_tx_putc_blocking(c);
        
    }

    // return 0;
}
