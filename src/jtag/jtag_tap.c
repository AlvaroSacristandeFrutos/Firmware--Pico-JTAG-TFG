#include "jtag_tap.h"

/*
 * Máquina de estados TAP de JTAG — pendiente de implementar.
 *
 * La máquina de estados TAP tiene 16 estados definidos por el estándar IEEE 1149.1.
 * La transición entre estados se controla con TMS: TMS=1 avanza hacia Test-Logic-Reset,
 * TMS=0 avanza hacia Run-Test/Idle y los estados de desplazamiento.
 *
 * jtag_tap_reset() deberá:
 *   - Mantener TMS=1 durante al menos 5 ciclos de TCK para llegar a Test-Logic-Reset
 *     desde cualquier estado en que se encuentre la máquina
 *
 * jtag_tap_shift_ir() deberá:
 *   - Navegar: Run-Test/Idle → Select-DR → Select-IR → Capture-IR → Shift-IR
 *   - Desplazar ir_len bits con TMS=0, el último bit con TMS=1 (→ Exit1-IR)
 *   - Volver a Run-Test/Idle
 *
 * jtag_tap_shift_dr() deberá:
 *   - Navegar: Run-Test/Idle → Select-DR → Capture-DR → Shift-DR
 *   - Desplazar dr_len bits capturando TDO en tdo_data
 *   - Volver a Run-Test/Idle
 */

void jtag_tap_reset(void) {
    (void)0;
}

void jtag_tap_shift_ir(const uint8_t *ir_data, uint32_t ir_len) {
    (void)ir_data;
    (void)ir_len;
}

void jtag_tap_shift_dr(const uint8_t *tdi_data, uint8_t *tdo_data,
                       uint32_t dr_len) {
    (void)tdi_data;
    (void)tdo_data;
    (void)dr_len;
}

void jtag_tap_set_tms(bool level) {
    (void)level;
}
