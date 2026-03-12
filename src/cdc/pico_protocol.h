#pragma once

#include <stdint.h>

/*
 * Alimenta un byte al parser de protocolo PicoAdapter.
 * Llamar desde cdc_uart_task() por cada byte disponible en el buffer RX.
 *
 * El parser mantiene internamente una máquina de estados:
 *   WAIT_START → WAIT_CMD → WAIT_LEN_LO → WAIT_LEN_HI →
 *   RECV_PAYLOAD → RECV_CRC
 *
 * Cuando la trama está completa y el CRC es correcto, despacha el comando
 * al handler correspondiente y envía la respuesta por cdc_send().
 * Si el CRC es incorrecto, la trama se descarta silenciosamente.
 */
void protocol_feed(uint8_t byte);
