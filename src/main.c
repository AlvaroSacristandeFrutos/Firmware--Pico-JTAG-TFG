#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "pico/stdlib.h"

#include "board/board_config.h"
#include "board/clock_init.h"
#include "board/gpio_init.h"
#include "util/led.h"
#include "util/adc.h"
#include "usb/usb_device.h"
#include "cdc/cdc_uart.h"
#include "jtag/jtag_pio.h"
#include "jlink/jlink_handler.h"
#include "jlink/jlink_protocol.h"

/*
 * Acceso directo al registro TIMELR del periférico Timer del RP2040.
 * El SDK de Pico lleva su propio wrapper, pero para mantener el código
 * completamente bare-metal accedemos al registro mediante su dirección.
 * TIMELR contiene los 32 bits bajos del contador de 64 bits que corre a 1 MHz.
 */
#include "hardware/regs/addressmap.h"
#define TIMER_TIMELR    (*(volatile uint32_t *)(TIMER_BASE + 0x0Cu))

static uint32_t time_us(void) {
    return TIMER_TIMELR;
}

static void delay_ms(uint32_t ms) {
    uint32_t start = time_us();
    uint32_t target_us = ms * 1000u;
    while ((time_us() - start) < target_us)
        ;
}

/* --------------------------------------------------------------------------
 * Rutinas de diagnóstico por LED
 *
 * Durante el desarrollo es la única forma de saber qué está pasando sin
 * tener un UART o un debugger conectado. Las funciones de abajo generan
 * patrones de parpadeos legibles a simple vista.
 * -------------------------------------------------------------------------- */

/* Parpadea el LED N veces: 200 ms encendido, 200 ms apagado. */
static void led_blink_n(uint32_t n) {
    for (uint32_t i = 0; i < n; i++) {
        led_set(true);
        delay_ms(200);
        led_set(false);
        delay_ms(200);
    }
}

/* Pausa larga (1 s apagado) para separar grupos de parpadeos. */
static void led_separator(void) {
    led_set(false);
    delay_ms(1000);
}

/*
 * Muestra un patrón de diagnóstico de forma indefinida.
 *
 * El patrón se divide en 5 grupos separados por pausas largas:
 *   Grupo 1 — número de comandos recibidos desde que enumeró
 *   Grupo 2 — resets de bus USB ocurridos tras la enumeración inicial
 *   Grupo 3 — nibble alto del primer byte de comando recibido (0-15)
 *   Grupo 4 — nibble bajo del primer byte de comando recibido (0-15)
 *   Grupo 5 — si la última respuesta se envió correctamente (1=sí, 0=no)
 *
 * Con esto se puede distinguir si el host envió comandos, qué comando
 * fue el primero, y si el EP1 IN llegó a completar la transferencia.
 */
static void led_show_diagnostic(uint32_t cmd_count, uint32_t bus_resets,
                                uint8_t first_cmd, bool tx_ok) {
    while (1) {
        led_blink_n(cmd_count);
        led_separator();

        led_blink_n(bus_resets);
        led_separator();

        led_blink_n((first_cmd >> 4) & 0x0F);
        led_separator();

        led_blink_n(first_cmd & 0x0F);
        led_separator();

        led_blink_n(tx_ok ? 1 : 0);

        /* Pausa larga antes de repetir el ciclo */
        led_set(false);
        delay_ms(3000);
    }
}

/* Buffers para un ciclo de comando/respuesta. El protocolo J-Link es
 * síncrono: el host espera la respuesta antes de enviar el siguiente
 * comando, así que un único par de buffers es suficiente. */
static uint8_t rx_data[CMD_BUF_SIZE];
static uint8_t tx_data[TX_BUF_SIZE];

/* --------------------------------------------------------------------------
 * Punto de entrada (Core 0)
 * -------------------------------------------------------------------------- */
int main(void) {
    stdio_init_all();
    gpio_init_all();
    led_init();
    jtag_pio_init();

    usb_device_init();

    /*
     * Esperar a que el host complete la enumeración USB.
     * Mientras tanto el LED parpadea lentamente para indicar que estamos
     * esperando. Si el host no enumera en 15 segundos, pasamos al modo
     * de error de abajo.
     */
    bool did_enumerate = false;
    {
        uint32_t enum_start = time_us();
        while ((time_us() - enum_start) < 15000000u) {
            if (usb_is_enumerated()) {
                did_enumerate = true;
                break;
            }
            led_toggle();
            delay_ms(250);
        }
    }

    if (!did_enumerate) {
        /*
         * No se produjo la enumeración. Mostramos el número de resets de
         * bus que el controlador USB contó — si es cero, el cable no está
         * conectado; si es mayor que cero, el host detectó el dispositivo
         * pero algo falló en los descriptores.
         * Patrón: [N parpadeos] — 2 s LED encendido — 1 s apagado — repetir.
         */
        while (1) {
            uint32_t resets = usb_get_bus_reset_count();
            led_blink_n(resets > 0 ? resets : 0);
            led_set(true);
            delay_ms(2000);
            led_set(false);
            delay_ms(1000);
        }
    }

    /* Enumeración exitosa — LED fijo para indicar que estamos listos */
    led_set(true);

    adc_sense_init();
    cdc_uart_init();

    /* Variables de diagnóstico para el modo de diagnóstico por LED */
    uint32_t cmd_count            = 0;
    uint8_t  first_cmd_byte       = 0;
    bool     last_tx_ok           = false;
    uint32_t bus_resets_at_start  = usb_get_bus_reset_count();
    uint32_t last_activity_us     = time_us();

    /* --------------------------------------------------------------------------
     * Bucle principal de comandos J-Link
     *
     * El protocolo J-Link es síncrono: el host envía un comando por EP1 OUT,
     * nosotros lo procesamos y respondemos por EP1 IN, y el host espera la
     * respuesta antes de enviar el siguiente. No hace falta usar múltiples
     * buffers ni dos núcleos para esto.
     * -------------------------------------------------------------------------- */
    while (1) {
        usb_device_task();

        uint8_t pkt[64];
        uint16_t len = usb_vendor_read(pkt, sizeof(pkt));

        if (len > 0) {
            last_activity_us = time_us();
            cmd_count++;
            if (cmd_count == 1) first_cmd_byte = pkt[0];

            memcpy(rx_data, pkt, len);

            uint16_t tx_len = 0;
            jlink_handle_command(rx_data, len, tx_data, &tx_len);

            /*
             * Enviar la respuesta en trozos de 64 bytes (tamaño máximo de
             * paquete bulk del RP2040). Para respuestas largas como VERSION
             * o JTAG3 esto puede suponer varios paquetes consecutivos.
             */
            if (tx_len > 0) {
                uint16_t tx_offset = 0;
                while (tx_offset < tx_len) {
                    uint16_t remaining = tx_len - tx_offset;
                    uint16_t chunk = (remaining > 64) ? 64 : remaining;
                    if (usb_vendor_write(&tx_data[tx_offset], chunk)) {
                        tx_offset += chunk;
                    }
                    usb_device_task();
                }

                /* Esperar a que EP1 IN complete la transferencia.
                 * Timeout de 500 ms — si el host no lee en ese tiempo,
                 * el driver probablemente no está abierto correctamente. */
                uint32_t wait_start = time_us();
                last_tx_ok = false;
                while ((time_us() - wait_start) < 500000u) {
                    if (!usb_vendor_tx_busy()) {
                        last_tx_ok = true;
                        break;
                    }
                    usb_device_task();
                }
            } else {
                last_tx_ok = true;  /* el comando no requería respuesta */
            }

            /* Rearmar EP1 OUT ahora que hemos terminado con la respuesta */
            usb_vendor_arm_rx();
        }

        /*
         * Si llevan 5 segundos sin actividad y ya hemos recibido algún
         * comando, entrar en el modo de diagnóstico por LED para poder
         * depurar sin necesidad de un debugger.
         */
        if (cmd_count > 0 && (time_us() - last_activity_us) > 5000000u) {
            uint32_t resets = usb_get_bus_reset_count() - bus_resets_at_start;
            led_show_diagnostic(cmd_count, resets, first_cmd_byte, last_tx_ok);
            /* No retorna nunca */
        }

        cdc_uart_task();
    }
}
