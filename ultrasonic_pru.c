/**
 * PRU firmware for HC-SR04 on BeagleBone Black.
 * TRIG: P9.27 (R30 bit 5)   ECHO: P9.24 (R31 bit 16)
 * Fires PRU event 19 when a measurement is ready (must match ultrasonic_lkm.c).
 *
 * Each ECHO-count loop iteration costs 200 cycles (1 us at 200 MHz):
 * loop body ~3 cycles + __delay_cycles(197).
 */

#include <stdint.h>

volatile register uint32_t __R30;
volatile register uint32_t __R31;

#define TRIG         (1u << 5)
#define ECHO_MASK    (1u << 16)
#define SENSOR_COUNT 1
#define SHARED_MEM   0x00010000
#define HISTORY_SIZE 8
#define PRU_INT      19
#define US(x)        ((x) * 200)
#define MS(x)        ((x) * 200000)
#define TIMEOUT      MS(38)

typedef struct {
    uint32_t sensor_count;
    uint32_t latest[SENSOR_COUNT];
    uint32_t history[SENSOR_COUNT][HISTORY_SIZE];
    uint32_t index[SENSOR_COUNT];
    uint32_t sequence;
} data_t;

volatile data_t *data = (volatile data_t *) SHARED_MEM;

void main()
{
    uint32_t counter, waiting;

    data->sequence = 0;
    data->sensor_count = SENSOR_COUNT;
    data->latest[0] = 0;
    data->index[0] = 0;

    while (1) {
        /* PO1-R1: 10 us trigger pulse */
        __R30 |= TRIG;
        __delay_cycles(US(10));
        __R30 &= ~TRIG;

        counter = 0;
        waiting = 0;

        while (!(__R31 & ECHO_MASK)) {
            if (++waiting > TIMEOUT) {
                data->latest[0] = 0;
                goto done;
            }
        }

        /* Count ECHO high time in microseconds */
        while (__R31 & ECHO_MASK) {
            if (++counter > TIMEOUT)
                break;
            __delay_cycles(197);
        }

        data->latest[0] = counter;  /* PO1-R2 */

        /* PO1-R2.1: history ring buffer */
        data->history[0][data->index[0]++] = counter;
        if (data->index[0] >= HISTORY_SIZE)
            data->index[0] = 0;

done:
        data->sequence++;
        __R31 = (1u << 5) | PRU_INT;  /* PO1-R3 */
        __delay_cycles(MS(60));
    }
}
