#pragma once

#include <stdint.h>
#include <stdbool.h>

/* Load the JTAG PIO program and configure SM0 on PIO0.
 * Also sets up DMA channels for bidirectional transfers. */
void jtag_pio_init(void);

/* Bidirectional JTAG transfer: send tdi_buf, capture into tdo_buf.
 * Returns true on success, false on DMA timeout (target unresponsive). */
bool jtag_pio_write_read(const uint8_t *tdi_buf, uint8_t *tdo_buf,
                         uint32_t len_bits);

/* Same as jtag_pio_write_read but, if exit_shift=true, raises TMS=1 on the
 * last TCK cycle so the TAP transitions to Exit1-DR/IR automatically.
 * Returns true on success, false on DMA timeout. */
bool jtag_pio_write_read_exit(const uint8_t *tdi_buf, uint8_t *tdo_buf,
                               uint32_t len_bits, bool exit_shift);

/* Write-only JTAG transfer (TDO discarded).
 * Returns true on success, false on DMA timeout. */
bool jtag_pio_write(const uint8_t *tdi_buf, uint32_t len_bits);

/* Set JTAG clock frequency in kHz. */
void jtag_set_freq(uint32_t freq_khz);

/* Returns the last frequency set via jtag_set_freq(), in kHz. */
uint32_t jtag_get_freq(void);
