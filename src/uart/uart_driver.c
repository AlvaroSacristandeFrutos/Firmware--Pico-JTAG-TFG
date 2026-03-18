/*
 * uart_driver.c — Driver UART0 para debug serie del target.
 *
 * GP12 = TX (Pico → target)
 * GP13 = RX (target → Pico)
 *
 * Recepción: IRQ UART0_IRQ, buffer circular de 512 bytes.
 * Transmisión: uart_putc_raw() bloqueante (FIFO HW de 32 bytes).
 */

#include "uart_driver.h"
#include "board/board_config.h"

#include "hardware/uart.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"

/* ---------------------------------------------------------------------- */
/*  Buffer circular RX                                                     */
/* ---------------------------------------------------------------------- */

#define UART_RX_BUF_SIZE  512u
#define UART_RX_BUF_MASK  (UART_RX_BUF_SIZE - 1u)

static uint8_t           s_rx_buf[UART_RX_BUF_SIZE];
static volatile uint16_t s_rx_head = 0u;   /* escrito por ISR  */
static          uint16_t s_rx_tail = 0u;   /* leído por main   */

/* ---------------------------------------------------------------------- */
/*  ISR                                                                    */
/* ---------------------------------------------------------------------- */

static void uart0_rx_isr(void) {
    while (uart_is_readable(uart0)) {
        uint8_t c = uart_getc(uart0);
        uint16_t next = (s_rx_head + 1u) & UART_RX_BUF_MASK;
        if (next != s_rx_tail) {   /* descartar silenciosamente si lleno */
            s_rx_buf[s_rx_head] = c;
            s_rx_head = next;
        }
    }
}

/* ---------------------------------------------------------------------- */
/*  API pública                                                            */
/* ---------------------------------------------------------------------- */

void uart_driver_init(uint32_t baud_hz) {
    uart_init(uart0, baud_hz);
    gpio_set_function(PIN_UART_TX, GPIO_FUNC_UART);  /* GP12 */
    gpio_set_function(PIN_UART_RX, GPIO_FUNC_UART);  /* GP13 */

    irq_set_exclusive_handler(UART0_IRQ, uart0_rx_isr);
    irq_set_enabled(UART0_IRQ, true);
    uart_set_irq_enables(uart0, true, false);  /* RX IRQ activado, TX IRQ no */
}

void uart_driver_set_baud(uint32_t baud_hz) {
    uart_set_baudrate(uart0, baud_hz);
}

void uart_driver_send(const uint8_t *data, uint16_t len) {
    for (uint16_t i = 0u; i < len; i++)
        uart_putc_raw(uart0, data[i]);
}

uint16_t uart_driver_recv(uint8_t *buf, uint16_t max_len) {
    uint16_t count = 0u;
    while (count < max_len && s_rx_tail != s_rx_head) {
        buf[count++] = s_rx_buf[s_rx_tail];
        s_rx_tail = (s_rx_tail + 1u) & UART_RX_BUF_MASK;
    }
    return count;
}
