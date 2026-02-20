#include "adc.h"

/*
 * Lectura de tensión de referencia del objetivo mediante ADC — pendiente de implementar.
 *
 * adc_sense_init() deberá sacar el ADC del reset, configurar GP26 como
 * entrada analógica (función ADC, sin pull-ups), seleccionar el canal 0
 * y habilitar el ADC.
 */
void adc_sense_init(void) {
    (void)0;
}

/*
 * Lee el canal 0 del ADC y devuelve la tensión de referencia del objetivo en mV.
 * El pin GP26 está conectado a Vref/2 mediante un divisor resistivo en la placa,
 * así que la fórmula es: Vref = (raw * 3300 / 4096) * 2
 */
uint16_t adc_read_vref_mv(void) {
    return 0;
}
