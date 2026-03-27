/*
 * cdc_rx.c — Buffer circular de recepción USB-CDC y despacho al parser de protocolo.
 *
 * cdc_rx_push() es llamada desde la ISR USB (cdc_data_out_handler en usb_device.c)
 * cuando el host envía datos por EP3 OUT (0x03).  Como la ISR escribe en s_rx_head
 * y el bucle principal lee de s_rx_tail, la única condición de carrera es en la
 * lectura de s_rx_head desde el bucle; se declara volatile para garantizar que el
 * compilador no la cachee en un registro.
 *
 * Ordenación de memoria: en cdc_rx_push la escritura al array s_rx_buf[s_rx_head]
 * usa el valor de s_rx_head (volatile) como índice, creando una dependencia de datos
 * que impide al compilador reordenar el write al array posterior al write volatile
 * de s_rx_head. En Cortex-M0+ (RP2040) no hay write buffers ni ejecución fuera de
 * orden, por lo que 'volatile' es suficiente para la sincronización ISR↔main en el
 * mismo core; no se requiere __dmb().
 *
 * El tamaño del buffer debe ser potencia de 2 para que la operación de módulo
 * se reduzca a un AND bit a bit sin división.
 */

#include "cdc_rx.h"
#include "pico_protocol.h"

#include <stdint.h>

#define CDC_RX_BUF_SIZE 2048u
#define CDC_RX_BUF_MASK (CDC_RX_BUF_SIZE - 1u)

_Static_assert((CDC_RX_BUF_SIZE & (CDC_RX_BUF_SIZE - 1u)) == 0u,
               "CDC_RX_BUF_SIZE debe ser potencia de 2");

static uint8_t           s_rx_buf[CDC_RX_BUF_SIZE];
static volatile uint16_t s_rx_head = 0;   /* escrito por ISR  */
static uint16_t          s_rx_tail = 0;   /* leído por main   */

/* ---------------------------------------------------------------------- */

void cdc_rx_init(void) {
    s_rx_head = 0;
    s_rx_tail = 0;
}

/*
 * Llamada desde la ISR (cdc_data_out_handler) con los bytes recibidos.
 * Descarta silenciosamente si el buffer está lleno.
 */
void cdc_rx_push(const uint8_t *data, uint16_t len) {
    for (uint16_t i = 0; i < len; i++) {
        uint16_t next_head = (s_rx_head + 1u) & CDC_RX_BUF_MASK;
        if (next_head != s_rx_tail) {   /* buffer no lleno */
            s_rx_buf[s_rx_head] = data[i];
            s_rx_head = next_head;
        }
    }
}

/*
 * Llamada desde el bucle principal.
 * Pasa cada byte disponible al parser de protocolo.
 */
void cdc_rx_task(void) {
    while (s_rx_tail != s_rx_head) {
        uint8_t b = s_rx_buf[s_rx_tail];
        s_rx_tail = (s_rx_tail + 1u) & CDC_RX_BUF_MASK;
        protocol_feed(b);
    }
}
