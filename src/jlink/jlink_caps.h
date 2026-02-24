#pragma once

#include <stdint.h>

/* EMU_CMD_VERSION — return firmware version string. */
void jlink_cmd_version(uint8_t *tx_buf, uint16_t *tx_len);

/* EMU_CMD_GET_CAPS — return 32-bit capabilities bitmap. */
void jlink_cmd_get_caps(uint8_t *tx_buf, uint16_t *tx_len);

/* EMU_CMD_GET_CAPS_EX — return extended capabilities (256 bits).
 * Some DLL versions send an optional 1-byte count after the command. */
void jlink_cmd_get_caps_ex(const uint8_t *rx_buf, uint16_t rx_len,
                            uint8_t *tx_buf, uint16_t *tx_len);

/* EMU_CMD_GET_HW_VERSION — return hardware version uint32. */
void jlink_cmd_get_hw_version(uint8_t *tx_buf, uint16_t *tx_len);

/* EMU_CMD_GET_HW_INFO — return hardware info fields selected by mask.
 * Host sends cmd(1) + mask(4 LE); probe returns popcount(mask)*4 bytes. */
void jlink_cmd_get_hw_info(const uint8_t *rx_buf, uint16_t rx_len,
                            uint8_t *tx_buf, uint16_t *tx_len);

/* EMU_CMD_GET_SPEEDS — return supported JTAG speeds. */
void jlink_cmd_get_speeds(uint8_t *tx_buf, uint16_t *tx_len);

/* EMU_CMD_SELECT_IF — select debug interface (JTAG or SWD). */
void jlink_cmd_select_if(const uint8_t *rx_buf, uint16_t rx_len,
                         uint8_t *tx_buf, uint16_t *tx_len);

/* EMU_CMD_SET_SPEED — set JTAG/SWD clock speed. */
void jlink_cmd_set_speed(const uint8_t *rx_buf, uint16_t rx_len,
                         uint8_t *tx_buf, uint16_t *tx_len);

/* EMU_CMD_GET_STATE — read JTAG pin states + target voltage. */
void jlink_cmd_get_state(uint8_t *tx_buf, uint16_t *tx_len);

/* EMU_CMD_HW_JTAG3 — raw JTAG shift (TMS+TDI in, TDO out). */
void jlink_cmd_hw_jtag3(const uint8_t *rx_buf, uint16_t rx_len,
                        uint8_t *tx_buf, uint16_t *tx_len);

/* EMU_CMD_HW_RESET0/1 — target nRESET control. */
void jlink_cmd_hw_reset(const uint8_t *rx_buf, uint16_t rx_len,
                        uint8_t *tx_buf, uint16_t *tx_len);

/* EMU_CMD_HW_TRST0/1 — JTAG nTRST control. */
void jlink_cmd_hw_trst(const uint8_t *rx_buf, uint16_t rx_len,
                       uint8_t *tx_buf, uint16_t *tx_len);

/* EMU_CMD_GET_MAX_MEM_BLOCK — max block size for memory ops. */
void jlink_cmd_get_max_mem_block(uint8_t *tx_buf, uint16_t *tx_len);

/* EMU_CMD_READ_CONFIG (0xF2) — read 256-byte J-Link configuration block. */
void jlink_cmd_read_config(uint8_t *tx_buf, uint16_t *tx_len);

/* EMU_CMD_WRITE_CONFIG (0xF3) — write 256-byte J-Link configuration block. */
void jlink_cmd_write_config(const uint8_t *rx_buf, uint16_t rx_len,
                             uint8_t *tx_buf, uint16_t *tx_len);

/* Called from main.c to store accumulated WRITE_CONFIG data (256 bytes). */
void jlink_config_update(const uint8_t *data, uint16_t len);
