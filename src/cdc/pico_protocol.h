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

/* Resetea el estado del parser a ST_WAIT_START. Llamar tras bus reset USB
 * para que los bytes residuales del buffer RX no se interpreten como
 * continuación de una trama anterior. */
void protocol_reset(void);

/* ---------------------------------------------------------------------- */
/*  Códigos de comando UART (debug del target)                            */
/* ---------------------------------------------------------------------- */

#define CMD_UART_SET_BAUD  0x20u   /* payload: u32 LE (Hz)  → RESP_OK   */
/* CMD_UART_SEND (0x21) y CMD_UART_RECV (0x22) eliminados:
 * el puente UART transparente usa ahora la segunda interfaz CDC (MI_02/MI_03). */
