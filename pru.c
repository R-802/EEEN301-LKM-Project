/* PRU firmware: drives HC-SR04 and writes echo time to shared RAM
 * TRIG: P9.27 (R30 bit 5)
 * ECHO: P9.24 (R31 bit 16)
 */

#include <stdint.h>

/* PRU GPIO registers — R30 = outputs, R31 = inputs */
volatile register uint32_t __R30;
volatile register uint32_t __R31;

#define TRIG (1u << 5)       // P9.27
#define ECHO_MASK (1u << 16) // P9.24
#define SENSOR_COUNT 1
#define SHARED_MEM 0x00010000 // PRU local address, seen by LKM at 0x4A310000
#define HISTORY_SIZE 8
#define PRU_INT 19
#define US(x) ((x) * 200) // 200 MHz PRU clock
#define MS(x) ((x) * 200000)
#define TIMEOUT MS(38) // max HC-SR04 echo window

// Must match data_t in lkm.c
typedef struct {
  uint32_t sensor_count;
  uint32_t latest[SENSOR_COUNT];
  uint32_t history[SENSOR_COUNT][HISTORY_SIZE];
  uint32_t index[SENSOR_COUNT];
  uint32_t sequence;
} data_t;

volatile data_t *data = (volatile data_t *)SHARED_MEM;

void main() {
  uint32_t counter, waiting;

  // Clear shared data before first measurement
  data->sequence = 0;
  data->sensor_count = SENSOR_COUNT;
  data->latest[0] = 0;
  data->index[0] = 0;

  while (1) {
    // Send 10 us trigger pulse to start the sensor
    __R30 |= TRIG;
    __delay_cycles(US(10));
    __R30 &= ~TRIG;

    counter = 0;
    waiting = 0;

    // Wait for ECHO high
    while (!(__R31 & ECHO_MASK)) {
      if (++waiting > TIMEOUT) {
        data->latest[0] = 0;
        goto done;
      }
    }

    // Count ECHO high time (1 count = 1 us)
    while (__R31 & ECHO_MASK) {
      if (++counter > TIMEOUT)
        break;
      __delay_cycles(197); // + loop overhead 200 cycles = 1 us
    }

    data->latest[0] = counter;

    // Keep last 8 readings in a ring buffer
    data->history[0][data->index[0]++] = counter;
    if (data->index[0] >= HISTORY_SIZE)
      data->index[0] = 0;

  done:
    data->sequence++; // tells LKM a new sample is ready
    __R31 = (1u << 5) | PRU_INT;
    __delay_cycles(MS(60)); // wait before next trigger
  }
}
