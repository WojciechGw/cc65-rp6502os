#include <stdint.h>

#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)

// wait on clock
uint32_t ticks = 0; // for PAUSE(millis)
#define PAUSE(millis) ticks=clock(); while(clock() < (ticks + millis)){}

/* ---- TX helpers --------------------------------------------------------- */
#define RX_READY (RIA.ready & RIA_READY_RX_BIT)
#define TX_READY (RIA.ready & RIA_READY_TX_BIT)
#define RX_READY_SPIN while (!RX_READY)
#define TX_READY_SPIN while (!TX_READY)