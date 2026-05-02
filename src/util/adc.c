#include "adc.h"
#include "board_config.h"

#include "hardware/adc.h"

void adc_sense_init(void) {
    adc_init();                    /* SDK: reset + enable, correcto para RP2040 y RP2350 */
    adc_gpio_init(PIN_VREF_ADC);   /* GP26 como entrada analógica */
}

/*
 * Lee el canal ADC0 (GP26) y devuelve la tensión de referencia del objetivo en mV.
 * GP26 está conectado a Vref/2 (divisor /2 en PCB):
 *   Vref_mV = (raw * 3300 / 4096) * 2  =  (raw * 6600) >> 12
 */
uint16_t adc_read_vref_mv(void) {
    adc_select_input(ADC_CHANNEL);   /* canal 0 — abstrae posición de AINSEL en RP2040/RP2350 */
    uint16_t raw = adc_read();       /* conversión one-shot, bloquea hasta fin (< 10 µs) */
    return (uint16_t)(((uint32_t)raw * 6600u) >> 12);
}
