#include <rp6502.h>

int __fastcall__ write(int fd, const void* buf, unsigned count) {
    const char* p = (const char*)buf;
    unsigned remaining = count;
    (void)fd; /* stdout/stderr are ignored, everything goes to UART */

    while (remaining--) {
        while (!(RIA.ready & RIA_READY_TX_BIT)) {
            /* spin until TX FIFO has space */
        }
        RIA.tx = *p++;
    }
    return (int)count;
}
