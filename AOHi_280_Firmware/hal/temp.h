/* temp.h - device temperature via MCU ADC1 NTC sensors (2x). */
#ifndef TEMP_H
#define TEMP_H
#include <stdint.h>

/* [0] = ADC1 ch0 (stock flt_1FFF8308), [1] = ADC1 ch1 (stock flt_1FFF8304), in C. */
extern float    g_temp_sensor[2];
/* Displayed (rounded/calibrated) temperature, integer C - matches stock UI value. */
extern uint32_t g_temp_display;

/* Trigger ADC1, read both NTC channels, convert + calibrate. Call from temp task. */
void temp_update(void);

#endif
