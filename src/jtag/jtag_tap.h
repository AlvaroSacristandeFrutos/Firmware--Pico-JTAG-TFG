#pragma once

#include <stdint.h>
#include <stdbool.h>

/* Navigate the JTAG TAP state machine to Test-Logic-Reset. */
void jtag_tap_reset(void);

/* Shift data into the Instruction Register (IR).
 * ir_data: data to shift in, ir_len: number of bits. */
void jtag_tap_shift_ir(const uint8_t *ir_data, uint32_t ir_len);

/* Shift data through the Data Register (DR).
 * Sends tdi_data and captures TDO into tdo_data.
 * dr_len: number of bits. */
void jtag_tap_shift_dr(const uint8_t *tdi_data, uint8_t *tdo_data,
                       uint32_t dr_len);

/* Set TMS pin to a specific level. */
void jtag_tap_set_tms(bool level);
