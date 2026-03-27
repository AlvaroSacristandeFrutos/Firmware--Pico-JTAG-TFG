#pragma once
#include <stdint.h>

/* Inicializa el estado del puente. Llamar antes de usb_device_init(). */
void uart_bridge_init(void);

/* Llamar en el bucle principal.
 * Dirección UART→USB: envía al host los bytes pendientes en el buffer RX.
 * Dirección USB→UART: drena el buffer de salida pendiente hacia UART TX. */
void uart_bridge_task(void);

/* Handlers de endpoint USB — implementados en uart_bridge.c, llamados desde la ISR USB */
void uart_notify_in_handler(uint8_t *buf, uint16_t len);
void uart_data_out_handler(uint8_t *buf, uint16_t len);
/* uart_data_in_handler is implemented in usb_device.c (needs s_uart_cdc_tx_busy) */
