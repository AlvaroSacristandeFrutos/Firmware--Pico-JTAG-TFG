#include "jtag_pio.h"
#include "board_config.h"

/* BIT-BANG MODE: jtag.pio.h y el programa PIO no se cargan.
 * El motor PIO+DMA está comentado; jtag_pio_write_read usa SIO directamente.
 * Para restaurar PIO: descomentar las líneas marcadas "PIO RESTORE". */

/* PIO RESTORE: #include "jtag.pio.h" */
/* PIO RESTORE: static const struct pio_program jtag_xfer_program_compat = {
    .instructions = jtag_xfer_program_instructions,
    .length       = 8,  .origin = -1,
    .pio_version  = (int8_t)PICO_PIO_VERSION,
#if PICO_PIO_VERSION > 0
    .used_gpio_ranges = 0x1,
#endif
};
#define jtag_xfer_program jtag_xfer_program_compat */

#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/structs/sio.h"
#include "hardware/structs/io_bank0.h"
#include "hardware/structs/pads_bank0.h"
#include "pico/time.h"
#include "hardware/watchdog.h"

/* GPIO_FUNC_SIO y GPIO_FUNC_PIO0 se obtienen de hardware/gpio.h —
 * correctos para RP2040 (5, 6) y RP2350 (31, 7) automáticamente. */

static PIO      s_pio      = pio0;
static uint     s_sm       = 0;
/* PIO RESTORE: static uint  s_offset = 0; */
/* PIO RESTORE: static int   s_dma_tx = -1; */
/* PIO RESTORE: static int   s_dma_rx = -1; */
static uint32_t s_freq_khz = 4000u;   /* frecuencia actual en kHz */
/* PIO RESTORE: static uint8_t s_rx_bytes[1022]; */

/* Calcula el semi-período en µs para el bit-bang a la frecuencia actual.
 * mínimo 1 µs (≈ 500 kHz real con busy_wait_us_32). */
static uint32_t half_period_us(void) {
    uint32_t h = (s_freq_khz > 0u) ? (500u / s_freq_khz) : 5u;
    return (h == 0u) ? 1u : h;
}

/* BIT-BANG MODE: no hay DMA ni SM que abortar; solo asegurar TCK=0. */
/* PIO RESTORE: implementación completa con dma_channel_abort + pio_sm_restart */
static void jtag_pio_recover(void) {
    sio_hw->gpio_clr = (1u << PIN_TCK);
}

void jtag_pio_init(void) {
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
    sio_hw->gpio_set    = sio_ctrl_mask;   /* inactivo = alto */

    /* ---- Pines de read-back → SIO inputs (OE=0 por defecto) ---- */
    /* gpio_set_function() hace ISO-clear + FUNCSEL + IE=1 en un solo paso,
     * igual que gpio_init() para GP0. Evita el patrón manual que en RP2350
     * puede dejar IE=0 si sólo se escribe el registro CTRL directamente. */
    gpio_set_function(PIN_TDI_RD, GPIO_FUNC_SIO);
    gpio_set_function(PIN_TCK_RD, GPIO_FUNC_SIO);
    gpio_set_function(PIN_TMS_RD, GPIO_FUNC_SIO);
    /* Sin pull: el TXS0108E one-shot mantiene el nivel; pull-down arrastraría
     * la línea a 0 antes de que CMD_GET_PIN_STATE lea sio_hw->gpio_in. */
    gpio_set_pulls(PIN_TDI_RD, false, false);
    gpio_set_pulls(PIN_TCK_RD, false, false);
    gpio_set_pulls(PIN_TMS_RD, false, false);

    /* ---- CTRL_OE → SIO output, habilitar level-shifter (/OE activo LOW) ---- */
#ifdef PADS_BANK0_GPIO0_ISO_BITS
    hw_clear_bits_raw(&pads_bank0_hw->io[PIN_CTRL_OE], PADS_BANK0_GPIO0_ISO_BITS);
#endif
    io_bank0_hw->io[PIN_CTRL_OE].ctrl = (io_bank0_hw->io[PIN_CTRL_OE].ctrl & ~0x1Fu) | (uint32_t)GPIO_FUNC_SIO;
    sio_hw->gpio_oe_set = (1u << PIN_CTRL_OE);
    sio_hw->gpio_clr    = (1u << PIN_CTRL_OE);   /* LOW = habilitado (/OE activo-bajo) */

    /* ---- BIT-BANG MODE: TCK y TDI como SIO outputs ---- */
    /* gpio_set_function() hace ISO-clear + FUNCSEL + IE=1, necesario en RP2350. */
    gpio_set_function(PIN_TCK, GPIO_FUNC_SIO);
    gpio_set_function(PIN_TDI, GPIO_FUNC_SIO);
    sio_hw->gpio_oe_set = (1u << PIN_TCK) | (1u << PIN_TDI);
    sio_hw->gpio_clr    = (1u << PIN_TCK);   /* TCK=0 inicial */
    sio_hw->gpio_set    = (1u << PIN_TDI);   /* TDI=1 (JTAG idle) */

    /* TDO como SIO input con IE habilitado */
    gpio_set_function(PIN_TDO, GPIO_FUNC_SIO);
    gpio_set_pulls(PIN_TDO, false, false);

    /* PIO RESTORE: descomentar bloque de pio_add_program + pio_sm_init + DMA aquí */
}

void jtag_set_freq(uint32_t freq_khz) {
    if (freq_khz == 0u) freq_khz = 1u;
    s_freq_khz = freq_khz;
    /* PIO RESTORE: quitar el comentario de la línea siguiente */
    /* pio_sm_set_clkdiv no-op en bit-bang mode (SM no inicializado); solo s_freq_khz controla half_period_us */
    float div = (float)clock_get_hz(clk_sys) /
                ((float)freq_khz * 1000.0f * 4.0f);
    if (div < 2.0f) div = 2.0f;
    pio_sm_set_clkdiv(s_pio, s_sm, div);
}

uint32_t jtag_get_freq(void) {
    return s_freq_khz;
}

bool jtag_pio_write_read(const uint8_t *tdi_buf, uint8_t *tdo_buf,
                         uint32_t len_bits) {
    if (len_bits == 0u) return true;
    if (len_bits > 8176u) return false;   /* mismo límite que la versión DMA */

    uint32_t num_bytes = (len_bits + 7u) / 8u;
    uint32_t half_us   = half_period_us();

    for (uint32_t i = 0u; i < num_bytes; i++)
        tdo_buf[i] = 0u;

    for (uint32_t bit = 0u; bit < len_bits; bit++) {
        /* Extraer TDI bit, LSB-first */
        uint8_t tdi_bit = (tdi_buf[bit >> 3u] >> (bit & 7u)) & 1u;

        /* TCK=0, fijar TDI antes del flanco de subida */
        sio_hw->gpio_clr = (1u << PIN_TCK);
        if (tdi_bit) sio_hw->gpio_set = (1u << PIN_TDI);
        else         sio_hw->gpio_clr = (1u << PIN_TDI);
        busy_wait_us_32(half_us);

        /* TCK=1 — target captura TDI en este flanco */
        sio_hw->gpio_set = (1u << PIN_TCK);
        busy_wait_us_32(half_us);

        /* Muestrear TDO durante TCK=1, igual que el bit-bang SW y el PIO */
        uint32_t tdo_bit = (sio_hw->gpio_in >> PIN_TDO) & 1u;
        tdo_buf[bit >> 3u] |= (uint8_t)(tdo_bit << (bit & 7u));

        /* Alimentar watchdog cada 1024 bits (~10 ms a 100 kHz) */
        if ((bit & 0x3FFu) == 0x3FFu)
            watchdog_update();
    }

    /* TCK=0 al final — consistente con el comportamiento PIO (side 0) */
    sio_hw->gpio_clr = (1u << PIN_TCK);
    watchdog_update();
    return true;
}

bool jtag_pio_write_read_exit(const uint8_t *tdi_buf, uint8_t *tdo_buf,
                               uint32_t len_bits, bool exit_shift) {
    if (len_bits == 0u) return true;

    if (!exit_shift)
        return jtag_pio_write_read(tdi_buf, tdo_buf, len_bits);

    /* Shift bits 0..len_bits-2 with TMS=0 (current state). */
    if (len_bits > 1u) {
        if (!jtag_pio_write_read(tdi_buf, tdo_buf, len_bits - 1u))
            return false;
    }

    /* Raise TMS=1 so the TAP will transition to Exit1 on the next TCK edge.
     * TMS remains HIGH after this function returns (TAP is in Exit1).
     * Contract: the caller must set TMS explicitly before generating any further
     * TCK edges. handle_write_tms always calls jtag_tap_set_tms() for the first
     * bit, so this residual HIGH is safe in the current protocol layer. */
    sio_hw->gpio_set = (1u << PIN_TMS);

    /* Extract last TDI bit and shift it through the PIO. */
    uint32_t last_idx  = len_bits - 1u;
    uint8_t  tdi_last  = (tdi_buf[last_idx >> 3u] >> (last_idx & 7u)) & 1u;
    uint8_t  tdo_last  = 0u;
    if (!jtag_pio_write_read(&tdi_last, &tdo_last, 1u))
        return false;

    /* Insert last TDO bit into tdo_buf at the correct bit position. */
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

void jtag_pio_get_diag(uint8_t out[24]) {
    uint32_t w[6] = {
        sio_hw->gpio_in,
        io_bank0_hw->io[PIN_TDO].ctrl,
        pads_bank0_hw->io[PIN_TDO],
        sio_hw->gpio_oe,
        s_pio->sm[s_sm].pinctrl,    /* IN_BASE bits[19:15] debe = 17 */
        s_pio->sm[s_sm].shiftctrl,  /* IN_SHIFTDIR bit19=1, AUTOPUSH bit17=1, PUSH_THRESH bits[29:25]=8 */
    };
    for (int i = 0; i < 6; i++) {
        out[i*4+0] = (uint8_t)( w[i]        & 0xFFu);
        out[i*4+1] = (uint8_t)((w[i] >>  8) & 0xFFu);
        out[i*4+2] = (uint8_t)((w[i] >> 16) & 0xFFu);
        out[i*4+3] = (uint8_t)((w[i] >> 24) & 0xFFu);
    }
}

uint32_t jtag_sw_read_idcode(void) {
    /* Bit-bang JTAG completamente en software (SIO), sin PIO.
     * Permite comparar con jtag_pio_write_read para aislar si el bug es PIO o hardware. */

    pio_sm_set_enabled(s_pio, s_sm, false);

    /* Tomar TCK y TDI bajo control SIO */
    io_bank0_hw->io[PIN_TCK].ctrl =
        (io_bank0_hw->io[PIN_TCK].ctrl & ~0x1Fu) | (uint32_t)GPIO_FUNC_SIO;
    io_bank0_hw->io[PIN_TDI].ctrl =
        (io_bank0_hw->io[PIN_TDI].ctrl & ~0x1Fu) | (uint32_t)GPIO_FUNC_SIO;
    sio_hw->gpio_oe_set = (1u << PIN_TCK) | (1u << PIN_TDI);
    sio_hw->gpio_clr    = (1u << PIN_TCK);   /* TCK=0 */
    sio_hw->gpio_set    = (1u << PIN_TDI);   /* TDI=1 */
    sio_hw->gpio_set    = (1u << PIN_TMS);   /* TMS=1 */

    /* TAP reset: 6 pulsos con TMS=1 (5 son suficientes por estándar; 6 es conservador) */
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
        sio_hw->gpio_clr = (1u << PIN_TCK);   /* TCK cae → target pone TDO[i] */
        busy_wait_us_32(5u);
    }
    /* Ahora en Shift-DR, TDO[0] del target ya está en GP17 */

    sio_hw->gpio_clr = (1u << PIN_TMS);   /* TMS=0 durante todo el shift */

    uint32_t idcode = 0u;
    for (int i = 0; i < 32; i++) {
        sio_hw->gpio_set = (1u << PIN_TCK);   /* TCK sube */
        busy_wait_us_32(5u);
        uint32_t tdo = (sio_hw->gpio_in >> PIN_TDO) & 1u;   /* leer TDO */
        idcode |= (tdo << (uint32_t)i);
        sio_hw->gpio_clr = (1u << PIN_TCK);   /* TCK baja → target actualiza TDO */
        busy_wait_us_32(5u);
    }

    /* Restaurar SIO (bit-bang mode: TCK y TDI deben quedar como SIO, no PIO) */
    gpio_set_function(PIN_TCK, GPIO_FUNC_SIO);
    gpio_set_function(PIN_TDI, GPIO_FUNC_SIO);
    /* PIO RESTORE: cambiar las dos líneas anteriores por GPIO_FUNC_PIO0 */

    jtag_pio_recover();
    return idcode;
}

bool jtag_pio_write(const uint8_t *tdi_buf, uint32_t len_bits) {
    /* Buffer de descarte: mismo límite que jtag_pio_write_read (8176 bits = 1022 bytes). */
    static uint8_t tdo_discard[1022];

    while (len_bits > 0u) {
        uint32_t chunk_bits  = (len_bits > 8176u) ? 8176u : len_bits;
        uint32_t chunk_bytes = (chunk_bits + 7u) / 8u;

        if (!jtag_pio_write_read(tdi_buf, tdo_discard, chunk_bits))
            return false;
        tdi_buf  += chunk_bytes;
        len_bits -= chunk_bits;
    }
    return true;
}
