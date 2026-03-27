/*
 * main.c — Sonda JTAG para Boundary Scan (TFG).
 *
 * El firmware recibe comandos del PC a través del puerto COM virtual USB-CDC
 * (VID 0x2E8A / PID 0x000A, "Raspberry Pi Pico") y los despacha al motor
 * JTAG PIO+DMA.  El protocolo serie está implementado en pico_protocol.c.
 *
 * Core 0:
 *   - Atiende USB (interrupt-driven, usb_device_task() es un no-op)
 *   - Drena el buffer CDC RX y alimenta el parser (cdc_rx_task())
 *
 * Core 1: no utilizado en esta versión.
 */

#include <stdint.h>
#include <stdbool.h>

#include "board/board_config.h"
#include "board/gpio_init.h"
#include "util/led.h"
#include "util/adc.h"
#include "usb/usb_device.h"
#include "jtag/jtag_pio.h"
#include "cdc/cdc_rx.h"
#include "uart/uart_driver.h"
#include "uart/uart_bridge.h"

#include "hardware/regs/addressmap.h"
#include "hardware/watchdog.h"
#define TIMER_TIMELR    (*(volatile uint32_t *)(TIMER_BASE + 0x0Cu))

static uint32_t time_us(void) {
    return TIMER_TIMELR;
}

/* ---------------------------------------------------------------------- */
/*  Punto de entrada                                                       */
/* ---------------------------------------------------------------------- */
int main(void) {
    gpio_init_all();
    adc_sense_init();
    jtag_pio_init();
    uart_driver_init(115200u);
    uart_bridge_init();
    cdc_rx_init();
    usb_device_init();

    /* Esperar hasta 15 s a que el host configure el dispositivo USB.
     * El LED parpadea a ~4 Hz mientras espera. */
    {
        uint32_t start      = time_us();
        uint32_t last_blink = start;
        bool     blink      = false;

        while ((time_us() - start) < 15000000u) {
            if (usb_is_configured()) break;
            if ((time_us() - last_blink) >= 250000u) {
                blink      = !blink;
                led_set(blink);
                last_blink += 250000u;
            }
        }
    }

    led_set(true);       /* verde fijo: enumeración OK */
    led_red_set(false);  /* rojo apagado: sin errores al arrancar */

    /* Iniciar watchdog con ventana de 2 s.
     * El segundo parámetro (pause_on_debug=true) pausa el temporizador
     * cuando el depurador detiene la ejecución, evitando resets espúreos. */
    watchdog_enable(2000, true);

    /* ------------------------------------------------------------------
     * Bucle principal
     * ------------------------------------------------------------------ */
    while (true) {
        watchdog_update();
        cdc_rx_task();        /* drena buffer RX y llama a protocol_feed() */
        uart_bridge_task();   /* puente UART↔USB transparente (MI_02/MI_03) */
    }
}
