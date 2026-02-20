#include "jlink_handler.h"
#include "jlink_protocol.h"
#include "jlink_caps.h"

/*
 * Despachador de comandos J-Link.
 * Lee el primer byte del buffer de recepción para determinar el comando
 * y llama a la función de respuesta correspondiente en jlink_caps.c.
 * Los comandos desconocidos se responden con un payload vacío.
 */
void jlink_handle_command(const uint8_t *rx_buf, uint16_t rx_len,
                          uint8_t *tx_buf, uint16_t *tx_len) {
    if (rx_len == 0) {
        *tx_len = 0;
        return;
    }

    uint8_t cmd = rx_buf[0];

    switch (cmd) {
    case EMU_CMD_VERSION:
        jlink_cmd_version(tx_buf, tx_len);
        break;
    case EMU_CMD_GET_CAPS:
        jlink_cmd_get_caps(tx_buf, tx_len);
        break;
    case EMU_CMD_GET_CAPS_EX:
        jlink_cmd_get_caps_ex(tx_buf, tx_len);
        break;
    case EMU_CMD_GET_HW_VERSION:
        jlink_cmd_get_hw_version(tx_buf, tx_len);
        break;
    case EMU_CMD_GET_HW_INFO:
        jlink_cmd_get_hw_info(tx_buf, tx_len);
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
    default:
        /* Comando no implementado — responder con payload vacío */
        *tx_len = 0;
        break;
    }
}
