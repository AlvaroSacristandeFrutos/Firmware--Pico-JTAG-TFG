#pragma once

#include <stdint.h>

/* EMU_CMD_VERSION — two-phase response matching real J-Link V9 clone.
 * Phase 1: call jlink_cmd_version   → sends 2 bytes [0x70, 0x00] (length=112).
 * Phase 2: call jlink_cmd_version_body → sends exactly 112 bytes (string+padding).
 * Both transfers must be sent as separate USB bulk transactions. */
void jlink_cmd_version(uint8_t *tx_buf, uint16_t *tx_len);
void jlink_cmd_version_body(uint8_t *tx_buf, uint16_t *tx_len);

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

/* EMU_CMD_IDSEGGER (0x16) — SEGGER authentication handshake.
 * Phase 1: jlink_cmd_idsegger()          → 4 bytes header [00 09 00 00].
 * Phase 2: jlink_cmd_idsegger_body(sub)  → 2304 bytes payload.
 * subcmd = rx_buf[1] from the original packet (0x02 or 0x00). */
void jlink_cmd_idsegger(uint8_t *tx_buf, uint16_t *tx_len);
void jlink_cmd_idsegger_body(uint8_t subcmd, uint8_t *tx_buf, uint16_t *tx_len);

/* Comandos de telemetría interna (caja negra) — respuestas capturadas. */
void jlink_cmd_unknown_0e(const uint8_t *rx_buf, uint16_t rx_len,
                          uint8_t *tx_buf, uint16_t *tx_len);
void jlink_cmd_unknown_0d(uint8_t *tx_buf, uint16_t *tx_len);
void jlink_cmd_unknown_09(const uint8_t *rx_buf, uint16_t rx_len,
                          uint8_t *tx_buf, uint16_t *tx_len);
