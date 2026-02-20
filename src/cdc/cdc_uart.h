#pragma once

/* Initialize CDC-ACM UART bridge (UART0 with DMA). */
void cdc_uart_init(void);

/* Move data between UART RX buffer and USB CDC, and vice versa.
 * Called from the Core 0 main loop. */
void cdc_uart_task(void);
