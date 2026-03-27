/*
 * uart_bridge.c — Puente transparente USB-CDC ↔ UART.
 *
 * Dirección USB→UART: los bytes que llegan por EP4 OUT se encolan en un
 * buffer circular (s_tx_buf) desde la ISR USB. uart_bridge_task() los drena
 * en chunks de 64 bytes hacia UART TX en el bucle principal.
 * El buffer circular evita la race condition del buffer único anterior
 * (ISR podía sobreescribir datos que uart_driver_send() estaba leyendo)
 * y elimina el drop silencioso de paquetes cuando el envío UART es lento.
 *
 * Dirección UART→USB: uart_bridge_task() sondea el buffer RX de UART y envía
 * los bytes disponibles al host por EP4 IN usando uart_cdc_send().
 */

#include "uart_bridge.h"
#include "uart_driver.h"
#include "usb/usb_device.h"
#include "usb/usb_descriptors.h"

#include <string.h>

/* ---------------------------------------------------------------------- */
/*  Buffer circular USB→UART                                               */
/*  Mismo patrón que uart_driver.c (s_rx_buf): ISR escribe s_tx_head,    */
/*  bucle principal lee s_tx_tail. Ninguno toca el índice del otro.       */
/* ---------------------------------------------------------------------- */

#define TX_BUF_SIZE  512u
#define TX_BUF_MASK  (TX_BUF_SIZE - 1u)

_Static_assert((TX_BUF_SIZE & (TX_BUF_SIZE - 1u)) == 0u,
               "TX_BUF_SIZE debe ser potencia de 2");

static uint8_t           s_tx_buf[TX_BUF_SIZE];
static volatile uint16_t s_tx_head = 0u;   /* escrito por ISR  */
static          uint16_t s_tx_tail = 0u;   /* leído por main   */

void uart_bridge_init(void) {
    s_tx_head = 0u;
    s_tx_tail = 0u;
}

/* Llamado desde la ISR USB cuando llegan datos del host por EP4 OUT */
void uart_data_out_handler(uint8_t *buf, uint16_t len) {
    uint16_t n = (len > 64u) ? 64u : len;
    for (uint16_t i = 0u; i < n; i++) {
        uint16_t next = (s_tx_head + 1u) & TX_BUF_MASK;
        if (next != s_tx_tail) {   /* descartar silenciosamente si lleno */
            s_tx_buf[s_tx_head] = buf[i];
            s_tx_head = next;
        }
    }
    /* Re-armar EP4 OUT para el siguiente paquete */
    struct usb_endpoint_configuration *ep =
        usb_get_endpoint_configuration(UART_EP_DATA_OUT);
    if (ep) usb_start_transfer(ep, NULL, 64);
}

/* Llamado desde la ISR USB: EP2 IN notify — nunca armado, no-op */
void uart_notify_in_handler(uint8_t *buf, uint16_t len) {
    (void)buf; (void)len;
}

void uart_bridge_task(void) {
    /* USB → UART: drenar el buffer circular en chunks de 64 bytes.
     * Se copia a un buffer local antes de llamar a uart_driver_send()
     * para que la ISR pueda seguir escribiendo en s_tx_buf sin riesgo. */
    if (s_tx_tail != s_tx_head) {
        uint8_t  local[64];
        uint16_t count = 0u;
        while (count < sizeof(local) && s_tx_tail != s_tx_head) {
            local[count++] = s_tx_buf[s_tx_tail];
            s_tx_tail = (s_tx_tail + 1u) & TX_BUF_MASK;
        }
        uart_driver_send(local, count);
    }

    /* UART → USB: leer buffer RX y enviar al host */
    static uint8_t s_rx_buf[64];
    uint16_t got = uart_driver_recv(s_rx_buf, sizeof(s_rx_buf));
    if (got > 0u) {
        uart_cdc_send(s_rx_buf, got);
    }
}
