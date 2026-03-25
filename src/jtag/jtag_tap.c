#include "jtag_tap.h"
#include "jtag_pio.h"
#include "board_config.h"

#include "hardware/structs/sio.h"

/*
 * Máquina de estados TAP del estándar IEEE 1149.1.
 *
 * TMS se controla por SIO (GP19); TCK/TDI/TDO los gestiona el PIO.
 * Las funciones de navegación entre estados envían pulsos de TCK
 * mediante jtag_pio_write() con un buffer de ceros (TDI=0) y TMS
 * fijo al nivel que se haya establecido con tms_set().
 *
 * Diagrama de estados abreviado (desde Run-Test/Idle):
 *
 *   RTI ─TMS=1─► Sel-DR ─TMS=1─► Sel-IR ─TMS=0─► Cap-IR ─TMS=0─► Shift-IR
 *                 │
 *                 └─TMS=0─► Cap-DR ─TMS=0─► Shift-DR
 *
 *   Shift-IR/DR ─TMS=1─► Exit1 ─TMS=1─► Update ─TMS=0─► RTI
 */

/* Buffer de TDI=0 para navegación sin datos */
static const uint8_t k_zeros[64] = {0};

static void tms_set(bool level) {
    if (level)
        sio_hw->gpio_set = 1u << PIN_TMS;
    else
        sio_hw->gpio_clr = 1u << PIN_TMS;
}

/* Generar n pulsos de TCK con TMS al nivel previamente fijado */
static void pulse_tck(uint32_t n) {
    while (n > 0u) {
        uint32_t chunk = (n > 512u) ? 512u : n;
        jtag_pio_write(k_zeros, chunk);
        n -= chunk;
    }
}

/* ------------------------------------------------------------------ */
/*  API pública                                                         */
/* ------------------------------------------------------------------ */

void jtag_tap_set_tms(bool level) {
    tms_set(level);
}

void jtag_tap_reset(void) {
    /* TMS=1 durante ≥5 ciclos lleva al TAP a Test-Logic-Reset desde cualquier estado */
    tms_set(true);
    pulse_tck(5);
    /* TMS=0 durante 1 ciclo → Run-Test/Idle */
    tms_set(false);
    pulse_tck(1);
}

void jtag_tap_shift_ir(const uint8_t *ir_data, uint32_t ir_len) {
    if (ir_len == 0u) return;

    /* Navegar RTI → Shift-IR: TMS = 1,1,0,0 */
    tms_set(true);  pulse_tck(2);   /* Select-DR-Scan, Select-IR-Scan */
    tms_set(false); pulse_tck(2);   /* Capture-IR, Shift-IR           */

    /* Desplazar los primeros ir_len-1 bits con TMS=0 (TDO se descarta) */
    if (ir_len > 1u) {
        jtag_pio_write(ir_data, ir_len - 1u);
    }

    /* Último bit con TMS=1 → Exit1-IR */
    uint8_t last_tdi = (ir_data[(ir_len - 1u) / 8u] >> ((ir_len - 1u) & 7u)) & 1u;
    uint8_t tdo_last;
    tms_set(true);
    jtag_pio_write_read(&last_tdi, &tdo_last, 1u);

    /* Update-IR (TMS sigue en 1), luego RTI */
    pulse_tck(1);
    tms_set(false);
    pulse_tck(1);
}

void jtag_tap_shift_dr(const uint8_t *tdi_data, uint8_t *tdo_data,
                       uint32_t dr_len) {
    if (dr_len == 0u) return;

    /* Navegar RTI → Shift-DR: TMS = 1,0,0 */
    tms_set(true);  pulse_tck(1);   /* Select-DR-Scan */
    tms_set(false); pulse_tck(2);   /* Capture-DR, Shift-DR */

    /* Desplazar los primeros dr_len-1 bits con TMS=0, capturando TDO si hay buffer */
    if (dr_len > 1u) {
        if (tdo_data)
            jtag_pio_write_read(tdi_data, tdo_data, dr_len - 1u);
        else
            jtag_pio_write(tdi_data, dr_len - 1u);
    }

    /* Último bit con TMS=1 → Exit1-DR */
    uint8_t last_tdi = (tdi_data[(dr_len - 1u) / 8u] >> ((dr_len - 1u) & 7u)) & 1u;
    uint8_t tdo_last;
    tms_set(true);
    jtag_pio_write_read(&last_tdi, &tdo_last, 1u);

    /* Almacenar el último bit TDO en tdo_data si se proporcionó buffer */
    if (tdo_data) {
        uint32_t last_byte        = (dr_len - 1u) / 8u;
        uint32_t last_bit         = (dr_len - 1u) & 7u;
        uint32_t first_call_bytes = (dr_len > 1u) ? ((dr_len - 1u + 7u) / 8u) : 0u;
        if (last_byte >= first_call_bytes) {
            /* Byte no tocado por jtag_pio_write_read → inicializar en lugar de RMW */
            tdo_data[last_byte] = (uint8_t)((tdo_last & 1u) << last_bit);
        } else {
            tdo_data[last_byte] = (uint8_t)(
                (tdo_data[last_byte] & ~(uint8_t)(1u << last_bit)) |
                ((tdo_last & 1u) << last_bit));
        }
    }

    /* Update-DR (TMS sigue en 1), luego RTI */
    pulse_tck(1);
    tms_set(false);
    pulse_tck(1);
}
