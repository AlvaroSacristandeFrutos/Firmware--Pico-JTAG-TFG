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

/* Lee el IDCODE del TAP en software puro (SIO bit-bang, sin PIO).
 * Útil para verificar si el problema de TDO=0 está en PIO o en el hardware. */
uint32_t jtag_sw_read_idcode(void);

/* Rellena out[24] con 6 registros hardware para diagnosticar por qué TDO (GP17)
 * siempre devuelve 0 cuando el PIO lee con 'in pins, 1':
 *   [0..3]   sio->gpio_in               — estado actual de todos los pines
 *   [4..7]   io_bank0->io[PIN_TDO].ctrl  — FUNCSEL + campo INOVER (bits 17:16)
 *   [8..11]  pads_bank0->io[PIN_TDO]     — IE, OD, PUE, PDE del pad
 *   [12..15] sio->gpio_oe               — bit 17 debe ser 0 (dirección entrada)
 *   [16..19] sm->pinctrl                — IN_BASE bits[19:15] debe ser 17
 *   [20..23] sm->shiftctrl             — IN_SHIFTDIR bit18=1, AUTOPUSH bit16=1
 */
void jtag_pio_get_diag(uint8_t out[24]);
