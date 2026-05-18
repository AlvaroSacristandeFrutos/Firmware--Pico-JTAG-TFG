#pragma once
/*
 * uart_driver.h — Driver UART0 para debug del target.
 *
 * Inicializa el periférico UART0 del RP2040 en GP12 (TX) / GP13 (RX).
 * La recepción es asíncrona (IRQ-driven) con buffer circular de 512 bytes.
 * La transmisión es bloqueante (usa el FIFO hardware de 32 bytes de UART0).
 *
 * El baudrate se configura vía USB SET_LINE_CODING en la interfaz MI_02.
 * Los datos se intercambian directamente sobre esa interfaz CDC transparente
 * (EP4 OUT/IN), sin protocolo PicoAdapter.
 */

#include <stdint.h>

/* Inicializa UART0 a la velocidad indicada y activa IRQ RX. */
void     uart_driver_init(uint32_t baud_hz);

/* Cambia la velocidad en tiempo de ejecución. */
void     uart_driver_set_baud(uint32_t baud_hz);

/* Devuelve la velocidad configurada actualmente (en Hz). */
uint32_t uart_driver_get_baud(void);

/* Envía len bytes al target (bloqueante). */
void     uart_driver_send(const uint8_t *data, uint16_t len);

/* Copia hasta max_len bytes del buffer RX en buf. No bloqueante.
 * Devuelve el número de bytes copiados (puede ser 0). */
uint16_t uart_driver_recv(uint8_t *buf, uint16_t max_len);
