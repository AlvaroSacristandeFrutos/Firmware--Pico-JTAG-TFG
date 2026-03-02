#include "adc.h"
#include "board_config.h"

#include "hardware/structs/adc.h"
#include "hardware/structs/resets.h"
#include "hardware/structs/io_bank0.h"
#include "hardware/structs/pads_bank0.h"
#include "hardware/regs/resets.h"
#include "hardware/regs/adc.h"
#include "hardware/regs/pads_bank0.h"

void adc_sense_init(void) {
    /* Sacar el ADC del reset */
    hw_clear_bits_raw(&resets_hw->reset, RESETS_RESET_ADC_BITS);
    while (!(resets_hw->reset_done & RESETS_RESET_ADC_BITS))
        ;

    /*
     * Configurar GP26 como entrada analógica:
     *   - FUNCSEL = 0x1F (null/analog) — desconecta el periférico digital
     *   - OD  = 1 (output disable)
     *   - IE  = 0 (input buffer digital deshabilitado — reduce interferencias)
     *   - Sin pull-up ni pull-down
     */
    io_bank0_hw->io[PIN_VREF_ADC].ctrl = 0x1Fu;
    pads_bank0_hw->io[PIN_VREF_ADC]    = PADS_BANK0_GPIO0_OD_BITS; /* OD=1, IE=0, pulls=0 */

    /* Habilitar ADC y esperar a que esté listo */
    adc_hw->cs = ADC_CS_EN_BITS;
    while (!(adc_hw->cs & ADC_CS_READY_BITS))
        ;
}

/*
 * Lee el canal ADC0 (GP26) y devuelve la tensión de referencia del objetivo en mV.
 *
 * El pin GP26 está conectado al nodo Vref/2 del divisor resistivo del PCB,
 * así que hay que multiplicar por 2:
 *
 *   Vref_mV = (raw * 3300 / 4096) * 2  =  (raw * 6600) >> 12
 *
 * Con raw máximo = 4095: resultado máximo = 6597 mV (cabe en uint16_t).
 */
uint16_t adc_read_vref_mv(void) {
    /* Seleccionar canal 0 y lanzar conversión única */
    uint32_t cs = adc_hw->cs;
    cs = (cs & ~(uint32_t)ADC_CS_AINSEL_BITS) | (0u << ADC_CS_AINSEL_LSB);
    cs |= ADC_CS_START_ONCE_BITS;
    adc_hw->cs = cs;

    /* Esperar fin de conversión */
    while (!(adc_hw->cs & ADC_CS_READY_BITS))
        ;

    uint32_t raw = adc_hw->result & 0xFFFu;  /* bits [11:0] — resultado 12-bit */
    return (uint16_t)((raw * 6600u) >> 12);
}
