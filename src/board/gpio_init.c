#include "gpio_init.h"
#include "board_config.h"

#include "hardware/structs/resets.h"
#include "hardware/structs/io_bank0.h"
#include "hardware/structs/pads_bank0.h"
#include "hardware/structs/sio.h"
#include "hardware/regs/resets.h"

/* Asigna un GPIO a la función SIO (función 5 en el RP2040).
 * SIO es el periférico de propósito general que permite controlar
 * el pin desde el procesador de forma directa. */
static void gpio_set_function_sio(uint32_t pin) {
    io_bank0_hw->io[pin].ctrl = 5;  /* FUNCSEL = SIO */
}

/* Configura un GPIO como salida push-pull a través de SIO.
 * El pin se inicializa a nivel bajo. */
static void gpio_set_output(uint32_t pin) {
    gpio_set_function_sio(pin);
    sio_hw->gpio_oe_set = 1u << pin;
    sio_hw->gpio_clr    = 1u << pin;   /* empezar a nivel bajo */
}

void gpio_init_all(void) {
    /* Sacar IO_BANK0 y PADS_BANK0 del reset para poder acceder a los GPIO */
    hw_clear_bits_raw(&resets_hw->reset,
                      RESETS_RESET_IO_BANK0_BITS |
                      RESETS_RESET_PADS_BANK0_BITS);
    while ((resets_hw->reset_done &
            (RESETS_RESET_IO_BANK0_BITS | RESETS_RESET_PADS_BANK0_BITS)) !=
            (RESETS_RESET_IO_BANK0_BITS | RESETS_RESET_PADS_BANK0_BITS))
        ;

    /* LEDs externos del PCB.
     * El resto de pines se configuran dentro de su módulo correspondiente:
     *   - JTAG (GP16-GP22): en jtag_pio_init()
     *   - UART (GP12-GP13): en uart_driver_init()
     *   - ADC  (GP26):      en adc_sense_init()        */
    gpio_set_output(PIN_LED);         /* GP14 — LED verde    */
    gpio_set_output(PIN_LED_RED);     /* GP15 — LED rojo     */
    gpio_set_output(PIN_LED_ONBOARD); /* GP25 — LED onboard  */
}
