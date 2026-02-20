#pragma once

#include <stdint.h>
#include <stdbool.h>

/* Load the JTAG PIO program and configure SM0 on PIO0.
 * Also sets up DMA channels for bidirectional transfers. */
void jtag_pio_init(void);

/* Bidirectional JTAG transfer: send tdi_buf, capture into tdo_buf.
 * len_bits: number of JTAG clock cycles. */
void jtag_pio_write_read(const uint8_t *tdi_buf, uint8_t *tdo_buf,
                         uint32_t len_bits);

/* Write-only JTAG transfer (TDO discarded). */
void jtag_pio_write(const uint8_t *tdi_buf, uint32_t len_bits);

/* Set JTAG clock frequency in kHz. */
void jtag_set_freq(uint32_t freq_khz);
