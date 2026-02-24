#include "jlink_handler.h"
#include "jlink_protocol.h"
#include "jlink_caps.h"
#include <stdbool.h>

/*
 * Despachador de comandos J-Link.
 * Lee el primer byte del buffer de recepción para determinar el comando
 * y llama a la función de respuesta correspondiente en jlink_caps.c.
 * Los comandos desconocidos se responden con un payload vacío.
 */

/*
 * Flag de sesión: asegura que la respuesta VERSION al flush 0x00 solo
 * se envía una vez por sesión USB (el flush dura 1024 paquetes de 64 B).
 * Se resetea con jlink_handler_reset() cuando el host reconfigura el USB.
 */
static bool s_flush_version_sent = false;

void jlink_handler_reset(void) {
    s_flush_version_sent = false;
}

void jlink_handle_command(const uint8_t *rx_buf, uint16_t rx_len,
                          uint8_t *tx_buf, uint16_t *tx_len) {
    if (rx_len == 0) {
        *tx_len = 0;
        return;
    }

    uint8_t cmd = rx_buf[0];

    switch (cmd) {
    case 0x00:
        /*
         * jlink.sys V8.90 abre la sesión enviando 65536 bytes de ceros
         * (1024 paquetes USB de 64 B) mientras tiene una petición de lectura
         * pendiente en EP2 IN (0x82).  Solo respondemos al PRIMER paquete
         * con la cadena de versión; los 1023 restantes se descartan sin TX.
         * main.c omite el bloqueo de 500 ms para este primer envío, de modo
         * que EP1 OUT se rearma inmediatamente y el burst de ceros fluye sin
         * esperar confirmación del host (evita el error E1 del historial).
         */
        if (!s_flush_version_sent) {
            jlink_cmd_version(tx_buf, tx_len);
            s_flush_version_sent = true;
        } else {
            *tx_len = 0;
        }
        break;
    case EMU_CMD_VERSION:
        jlink_cmd_version(tx_buf, tx_len);
        break;
    case EMU_CMD_GET_CAPS:
        jlink_cmd_get_caps(tx_buf, tx_len);
        break;
    case EMU_CMD_GET_CAPS_EX:
        jlink_cmd_get_caps_ex(rx_buf, rx_len, tx_buf, tx_len);
        break;
    case EMU_CMD_GET_HW_VERSION:
        jlink_cmd_get_hw_version(tx_buf, tx_len);
        break;
    case EMU_CMD_GET_HW_INFO:
        jlink_cmd_get_hw_info(rx_buf, rx_len, tx_buf, tx_len);
        break;
    case EMU_CMD_GET_SPEEDS:
        jlink_cmd_get_speeds(tx_buf, tx_len);
        break;
    case EMU_CMD_SELECT_IF:
        jlink_cmd_select_if(rx_buf, rx_len, tx_buf, tx_len);
        break;
    case EMU_CMD_SET_SPEED:
        jlink_cmd_set_speed(rx_buf, rx_len, tx_buf, tx_len);
        break;
    case EMU_CMD_GET_STATE:
        jlink_cmd_get_state(tx_buf, tx_len);
        break;
    case EMU_CMD_HW_JTAG3:
        jlink_cmd_hw_jtag3(rx_buf, rx_len, tx_buf, tx_len);
        break;
    case EMU_CMD_HW_RESET0:
    case EMU_CMD_HW_RESET1:
        jlink_cmd_hw_reset(rx_buf, rx_len, tx_buf, tx_len);
        break;
    case EMU_CMD_HW_TRST0:
    case EMU_CMD_HW_TRST1:
        jlink_cmd_hw_trst(rx_buf, rx_len, tx_buf, tx_len);
        break;
    case EMU_CMD_GET_MAX_MEM_BLOCK:
        jlink_cmd_get_max_mem_block(tx_buf, tx_len);
        break;
    case EMU_CMD_READ_CONFIG:
        jlink_cmd_read_config(tx_buf, tx_len);
        break;
    case EMU_CMD_WRITE_CONFIG:
        jlink_cmd_write_config(rx_buf, rx_len, tx_buf, tx_len);
        break;
    default:
        /* Comando no implementado — responder con payload vacío */
        *tx_len = 0;
        break;
    }
}
