#include "cdc_uart.h"

/*
 * Puente UART bare-metal — pendiente de implementar.
 *
 * cdc_uart_init() deberá:
 *   - Sacar UART0 del reset via RESETS
 *   - Configurar GP12 como UART0 TX (función 2) y GP13 como UART0 RX
 *   - Programar UART0 a 115200 baudios, 8N1
 *   - Configurar un canal DMA circular para RX (UART → RAM), de modo que
 *     los bytes entrantes se almacenen sin intervención del procesador
 *   - Configurar un canal DMA para TX (RAM → UART)
 *
 * cdc_uart_task() deberá:
 *   - Comprobar el progreso del DMA de RX; si hay bytes nuevos, pasarlos
 *     a la función de envío USB CDC
 *   - Comprobar si el USB CDC tiene datos del host y lanzar el DMA de TX
 *     hacia el UART
 */

void cdc_uart_init(void) {
    (void)0;
}

void cdc_uart_task(void) {
    (void)0;
}
