#pragma once

#include <stdint.h>
#include <stdbool.h>

/* Configura todos los pines JTAG como SIO (bitbanging por software). */
void jtag_bb_init(void);

/* Transferencia JTAG bidireccional: envía tdi_buf y captura en tdo_buf.
 * Devuelve true en éxito. Nunca falla salvo len_bits > 8176. */
bool jtag_bb_write_read(const uint8_t *tdi_buf, uint8_t *tdo_buf,
                        uint32_t len_bits);

/* Igual que jtag_bb_write_read pero, si exit_shift=true, eleva TMS=1 en el
 * último ciclo TCK para que el TAP transite a Exit1-DR/IR automáticamente. */
bool jtag_bb_write_read_exit(const uint8_t *tdi_buf, uint8_t *tdo_buf,
                              uint32_t len_bits, bool exit_shift);

/* Transferencia solo escritura (TDO descartado). */
bool jtag_bb_write(const uint8_t *tdi_buf, uint32_t len_bits);

/* Fija la frecuencia JTAG en kHz (determina el semi-período del bitbang). */
void jtag_set_freq(uint32_t freq_khz);

/* Devuelve la última frecuencia configurada, en kHz. */
uint32_t jtag_get_freq(void);

/* Rellena out[24] con 6 registros hardware de 32 bits para diagnóstico:
 * [gpio_in, gpio_ctrl(TDO), pads(TDO), gpio_oe, 0, 0] */
void jtag_get_diag(uint8_t out[24]);

/* Lee el IDCODE JTAG usando bitbanging puro (secuencia TAP-reset → Shift-DR). */
uint32_t jtag_bb_read_idcode(void);
