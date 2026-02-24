#pragma once

#include <stdint.h>

/* Process a J-Link command received from the host.
 * rx_buf/rx_len: incoming command data.
 * tx_buf: output buffer for the response.
 * tx_len: set to the response length on return. */
void jlink_handle_command(const uint8_t *rx_buf, uint16_t rx_len,
                          uint8_t *tx_buf, uint16_t *tx_len);

/* Reset per-session handler state (flush flag, etc.).
 * Call whenever the USB host reconfigures the device. */
void jlink_handler_reset(void);
