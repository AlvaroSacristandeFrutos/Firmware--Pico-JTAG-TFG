#pragma once

#include <stdint.h>

/* Initialize ADC for reading target Vref on GP26/ADC0. */
void adc_sense_init(void);

/* Read target voltage in millivolts (Vref = raw * 3300 / 4096 * 2). */
uint16_t adc_read_vref_mv(void);
