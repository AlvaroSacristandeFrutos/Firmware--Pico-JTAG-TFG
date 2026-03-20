#pragma once

#include <stdint.h>

/* Inicializa el buffer circular de recepción USB-CDC. */
void cdc_rx_init(void);

/* Llamada desde el bucle principal; pasa cada byte disponible a protocol_feed(). */
void cdc_rx_task(void);

/* Llamada desde la ISR USB (cdc_data_out_handler) para escribir bytes recibidos
 * en el buffer circular.  Es seguro llamarla desde contexto de interrupción. */
void cdc_rx_push(const uint8_t *data, uint16_t len);
