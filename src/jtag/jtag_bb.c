#include "jtag_bb.h"
#include "board_config.h"

#include "hardware/gpio.h"
#include "hardware/structs/sio.h"
#include "hardware/structs/io_bank0.h"
#include "hardware/structs/pads_bank0.h"
#include "pico/time.h"
#include "hardware/watchdog.h"

static uint32_t s_freq_khz = 4000u;

/* Semi-período en µs para el bitbang a la frecuencia actual. Mínimo 1 µs. */
static uint32_t half_period_us(void) {
    uint32_t h = (s_freq_khz > 0u) ? (500u / s_freq_khz) : 5u;
    return (h == 0u) ? 1u : h;
}

/* Asegura TCK=0 tras cualquier transferencia interrumpida. */
static void jtag_bb_recover(void) {
    sio_hw->gpio_clr = (1u << PIN_TCK);
}

void jtag_bb_init(void) {
    /* ---- TMS, nRST, nTRST → SIO outputs, inactivos (nivel alto) ---- */
    /* En RP2350 los pads arrancan aislados (ISO=1); limpiar antes de activar OE. */
#ifdef PADS_BANK0_GPIO0_ISO_BITS
    hw_clear_bits_raw(&pads_bank0_hw->io[PIN_TMS],  PADS_BANK0_GPIO0_ISO_BITS);
    hw_clear_bits_raw(&pads_bank0_hw->io[PIN_RST],  PADS_BANK0_GPIO0_ISO_BITS);
    hw_clear_bits_raw(&pads_bank0_hw->io[PIN_TRST], PADS_BANK0_GPIO0_ISO_BITS);
#endif
    io_bank0_hw->io[PIN_TMS].ctrl  = (io_bank0_hw->io[PIN_TMS].ctrl  & ~0x1Fu) | (uint32_t)GPIO_FUNC_SIO;
    io_bank0_hw->io[PIN_RST].ctrl  = (io_bank0_hw->io[PIN_RST].ctrl  & ~0x1Fu) | (uint32_t)GPIO_FUNC_SIO;
    io_bank0_hw->io[PIN_TRST].ctrl = (io_bank0_hw->io[PIN_TRST].ctrl & ~0x1Fu) | (uint32_t)GPIO_FUNC_SIO;
    const uint32_t sio_ctrl_mask =
        (1u << PIN_TMS) | (1u << PIN_RST) | (1u << PIN_TRST);
    sio_hw->gpio_oe_set = sio_ctrl_mask;
    sio_hw->gpio_set    = sio_ctrl_mask;

    /* ---- Pines de read-back → SIO inputs ---- */
    /* gpio_set_function() hace ISO-clear + FUNCSEL + IE=1 en un solo paso,
     * necesario en RP2350 donde los pads arrancan con IE=0. */
    gpio_set_function(PIN_TDI_RD, GPIO_FUNC_SIO);
    gpio_set_function(PIN_TCK_RD, GPIO_FUNC_SIO);
    gpio_set_function(PIN_TMS_RD, GPIO_FUNC_SIO);
    gpio_set_pulls(PIN_TDI_RD, false, false);
    gpio_set_pulls(PIN_TCK_RD, false, false);
    gpio_set_pulls(PIN_TMS_RD, false, false);

    /* ---- CTRL_OE → SIO output, habilitar level-shifter (/OE activo LOW) ---- */
#ifdef PADS_BANK0_GPIO0_ISO_BITS
    hw_clear_bits_raw(&pads_bank0_hw->io[PIN_CTRL_OE], PADS_BANK0_GPIO0_ISO_BITS);
#endif
    io_bank0_hw->io[PIN_CTRL_OE].ctrl = (io_bank0_hw->io[PIN_CTRL_OE].ctrl & ~0x1Fu) | (uint32_t)GPIO_FUNC_SIO;
    sio_hw->gpio_oe_set = (1u << PIN_CTRL_OE);
    sio_hw->gpio_clr    = (1u << PIN_CTRL_OE);

    /* ---- TCK y TDI → SIO outputs ---- */
    gpio_set_function(PIN_TCK, GPIO_FUNC_SIO);
    gpio_set_function(PIN_TDI, GPIO_FUNC_SIO);
    sio_hw->gpio_oe_set = (1u << PIN_TCK) | (1u << PIN_TDI);
    sio_hw->gpio_clr    = (1u << PIN_TCK);
    sio_hw->gpio_set    = (1u << PIN_TDI);

    /* ---- TDO → SIO input ---- */
    gpio_set_function(PIN_TDO, GPIO_FUNC_SIO);
    gpio_set_pulls(PIN_TDO, false, false);
}

void jtag_set_freq(uint32_t freq_khz) {
    if (freq_khz == 0u) freq_khz = 1u;
    s_freq_khz = freq_khz;
}

uint32_t jtag_get_freq(void) {
    return s_freq_khz;
}

bool jtag_bb_write_read(const uint8_t *tdi_buf, uint8_t *tdo_buf,
                        uint32_t len_bits) {
    if (len_bits == 0u) return true;
    if (len_bits > 8176u) return false;

    uint32_t num_bytes = (len_bits + 7u) / 8u;
    uint32_t half_us   = half_period_us();

    for (uint32_t i = 0u; i < num_bytes; i++)
        tdo_buf[i] = 0u;

    for (uint32_t bit = 0u; bit < len_bits; bit++) {
        uint8_t tdi_bit = (tdi_buf[bit >> 3u] >> (bit & 7u)) & 1u;

        sio_hw->gpio_clr = (1u << PIN_TCK);
        if (tdi_bit) sio_hw->gpio_set = (1u << PIN_TDI);
        else         sio_hw->gpio_clr = (1u << PIN_TDI);
        busy_wait_us_32(half_us);

        sio_hw->gpio_set = (1u << PIN_TCK);
        busy_wait_us_32(half_us);

        uint32_t tdo_bit = (sio_hw->gpio_in >> PIN_TDO) & 1u;
        tdo_buf[bit >> 3u] |= (uint8_t)(tdo_bit << (bit & 7u));

        if ((bit & 0x3FFu) == 0x3FFu)
            watchdog_update();
    }

    sio_hw->gpio_clr = (1u << PIN_TCK);
    watchdog_update();
    return true;
}

bool jtag_bb_write_read_exit(const uint8_t *tdi_buf, uint8_t *tdo_buf,
                              uint32_t len_bits, bool exit_shift) {
    if (len_bits == 0u) return true;

    if (!exit_shift)
        return jtag_bb_write_read(tdi_buf, tdo_buf, len_bits);

    if (len_bits > 1u) {
        if (!jtag_bb_write_read(tdi_buf, tdo_buf, len_bits - 1u))
            return false;
    }

    /* Elevar TMS=1 para que el TAP transite a Exit1 en el último TCK. */
    sio_hw->gpio_set = (1u << PIN_TMS);

    uint32_t last_idx  = len_bits - 1u;
    uint8_t  tdi_last  = (tdi_buf[last_idx >> 3u] >> (last_idx & 7u)) & 1u;
    uint8_t  tdo_last  = 0u;
    if (!jtag_bb_write_read(&tdi_last, &tdo_last, 1u))
        return false;

    uint32_t byte_idx         = last_idx >> 3u;
    uint8_t  bit_pos          = (uint8_t)(last_idx & 7u);
    uint32_t first_call_bytes = (len_bits > 1u) ? ((len_bits - 1u + 7u) / 8u) : 0u;

    if (byte_idx >= first_call_bytes) {
        tdo_buf[byte_idx] = tdo_last & 1u;
    } else {
        tdo_buf[byte_idx] &= ~(uint8_t)(1u << bit_pos);
        if (tdo_last & 1u)
            tdo_buf[byte_idx] |= (uint8_t)(1u << bit_pos);
    }
    return true;
}

bool jtag_bb_write(const uint8_t *tdi_buf, uint32_t len_bits) {
    static uint8_t tdo_discard[1022];

    while (len_bits > 0u) {
        uint32_t chunk_bits  = (len_bits > 8176u) ? 8176u : len_bits;
        uint32_t chunk_bytes = (chunk_bits + 7u) / 8u;

        if (!jtag_bb_write_read(tdi_buf, tdo_discard, chunk_bits))
            return false;
        tdi_buf  += chunk_bytes;
        len_bits -= chunk_bits;
    }
    return true;
}

void jtag_get_diag(uint8_t out[24]) {
    uint32_t w[6] = {
        sio_hw->gpio_in,
        io_bank0_hw->io[PIN_TDO].ctrl,
        pads_bank0_hw->io[PIN_TDO],
        sio_hw->gpio_oe,
        0u,   /* reservado (era SM.pinctrl) */
        0u,   /* reservado (era SM.shiftctrl) */
    };
    for (int i = 0; i < 6; i++) {
        out[i*4+0] = (uint8_t)( w[i]        & 0xFFu);
        out[i*4+1] = (uint8_t)((w[i] >>  8) & 0xFFu);
        out[i*4+2] = (uint8_t)((w[i] >> 16) & 0xFFu);
        out[i*4+3] = (uint8_t)((w[i] >> 24) & 0xFFu);
    }
}

uint32_t jtag_bb_read_idcode(void) {
    /* TMS=1, TCK=0, TDI=1 ya están en ese estado desde jtag_bb_init. */
    sio_hw->gpio_set = (1u << PIN_TMS);

    /* TAP reset: 6 pulsos con TMS=1 */
    for (int i = 0; i < 6; i++) {
        sio_hw->gpio_set = (1u << PIN_TCK);
        busy_wait_us_32(5u);
        sio_hw->gpio_clr = (1u << PIN_TCK);
        busy_wait_us_32(5u);
    }

    /* TLR → Shift-DR: TMS = 0,1,0,0 en cada flanco de subida */
    static const uint8_t nav[4] = {0, 1, 0, 0};
    for (int i = 0; i < 4; i++) {
        if (nav[i]) sio_hw->gpio_set = (1u << PIN_TMS);
        else        sio_hw->gpio_clr = (1u << PIN_TMS);
        sio_hw->gpio_set = (1u << PIN_TCK);
        busy_wait_us_32(5u);
        sio_hw->gpio_clr = (1u << PIN_TCK);
        busy_wait_us_32(5u);
    }

    sio_hw->gpio_clr = (1u << PIN_TMS);

    uint32_t idcode = 0u;
    for (int i = 0; i < 32; i++) {
        sio_hw->gpio_set = (1u << PIN_TCK);
        busy_wait_us_32(5u);
        uint32_t tdo = (sio_hw->gpio_in >> PIN_TDO) & 1u;
        idcode |= (tdo << (uint32_t)i);
        sio_hw->gpio_clr = (1u << PIN_TCK);
        busy_wait_us_32(5u);
    }

    jtag_bb_recover();
    return idcode;
}
