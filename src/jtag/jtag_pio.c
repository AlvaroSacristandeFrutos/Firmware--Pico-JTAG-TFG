#include "jtag_pio.h"
#include "board_config.h"

/*
 * Cabecera generada por pico_generate_pio_header() a partir de jtag.pio.
 * Contiene jtag_xfer_program y jtag_xfer_program_get_default_config().
 */
#include "jtag.pio.h"

#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/structs/sio.h"
#include "hardware/structs/io_bank0.h"
#include "pico/time.h"

/* Valores de FUNCSEL para los pines GPIO (RP2040 datasheet, Table 5) */
#define GPIO_FUNC_SIO   5u
#define GPIO_FUNC_PIO0  6u

static PIO      s_pio      = pio0;
static uint     s_sm       = 0;
static uint     s_offset   = 0;
static int      s_dma_tx   = -1;
static int      s_dma_rx   = -1;
static uint32_t s_freq_khz = 4000u;   /* frecuencia actual en kHz */

/*
 * Buffer intermedio para el DMA RX.
 * jtag_pio_write_read usa IN shift_right → los datos capturados quedan en
 * los bits [31:24] de cada word de 32 bits del RX FIFO.  El DMA lee words
 * completos (DMA_SIZE_32) aquí; la extracción de bytes se hace después con >> 24.
 * Tamaño máximo: num_bytes ≤ 1022 (CMD_BUF_SIZE/2 - 2) para hw_jtag3.
 */
static uint32_t s_rx_words[1022];

/*
 * Recuperación tras timeout de DMA: aborta ambos canales y reinicia la SM
 * para que el PIO quede listo para la siguiente transferencia.
 * TCK queda en nivel bajo (side 0 del estado inicial del programa).
 */
static void jtag_pio_recover(void) {
    dma_channel_abort(s_dma_tx);
    dma_channel_abort(s_dma_rx);
    pio_sm_set_enabled(s_pio, s_sm, false);
    pio_sm_clear_fifos(s_pio, s_sm);
    pio_sm_restart(s_pio, s_sm);
    pio_sm_exec(s_pio, s_sm, pio_encode_jmp(s_offset));  /* PC → inicio del programa */
    pio_sm_set_enabled(s_pio, s_sm, true);
}

void jtag_pio_init(void) {
    /* ---- TMS, nRST, nTRST → SIO outputs, inactivos (nivel alto) ---- */
    io_bank0_hw->io[PIN_TMS].ctrl  = GPIO_FUNC_SIO;
    io_bank0_hw->io[PIN_RST].ctrl  = GPIO_FUNC_SIO;
    io_bank0_hw->io[PIN_TRST].ctrl = GPIO_FUNC_SIO;
    const uint32_t sio_ctrl_mask =
        (1u << PIN_TMS) | (1u << PIN_RST) | (1u << PIN_TRST);
    sio_hw->gpio_oe_set = sio_ctrl_mask;
    sio_hw->gpio_set    = sio_ctrl_mask;   /* inactivo = alto */

    /* ---- Pines de read-back → SIO inputs (OE=0 por defecto) ---- */
    io_bank0_hw->io[PIN_TDI_RD].ctrl = GPIO_FUNC_SIO;
    io_bank0_hw->io[PIN_TCK_RD].ctrl = GPIO_FUNC_SIO;
    io_bank0_hw->io[PIN_TMS_RD].ctrl = GPIO_FUNC_SIO;

    /* ---- CTRL_OE → SIO output, habilitar level-shifter (/OE activo LOW) ---- */
    io_bank0_hw->io[PIN_CTRL_OE].ctrl = GPIO_FUNC_SIO;
    sio_hw->gpio_oe_set = (1u << PIN_CTRL_OE);
    sio_hw->gpio_clr    = (1u << PIN_CTRL_OE);   /* LOW = habilitado */

    /* ---- Cargar el programa PIO ---- */
    s_offset = pio_add_program(s_pio, &jtag_xfer_program);

    pio_sm_config c = jtag_xfer_program_get_default_config(s_offset);
    sm_config_set_out_pins(&c, PIN_TDI, 1);         /* OUT  → TDI (GP16) */
    sm_config_set_in_pins(&c, PIN_TDO);              /* IN   ← TDO (GP17) */
    sm_config_set_sideset_pins(&c, PIN_TCK);         /* side → TCK (GP18) */
    /* JTAG es LSB-first. OUT shift a la derecha: el bit 0 del byte sale primero.
     * IN shift a la derecha: tras 8 bits el byte capturado queda en bits [31:24]. */
    sm_config_set_out_shift(&c, true, true, 8);      /* shift_right, autopull, threshold=8 */
    sm_config_set_in_shift(&c, true, true, 8);       /* shift_right, autopush, threshold=8 */

    /*
     * Divisor de reloj para la frecuencia JTAG por defecto.
     * El bucle PIO tiene 4 instrucciones por bit (out, nop, in, jmp), por lo que
     * la frecuencia TCK es clk_sys / (div * 4):
     *   div = clk_sys / (freq_hz * 4)
     */
    float div = (float)clock_get_hz(clk_sys) /
                ((float)(4000u * 1000u) * 4.0f);  /* 4000 kHz por defecto */
    /* div mínimo = 2: TCK máx = 125 MHz / (2×4) ≈ 15.6 MHz.
     * div = 1 daría 31.25 MHz, por encima del límite de muchos targets ARM
     * (Cortex-M: 25 MHz típico) y del nivel-shifter del PCB. */
    if (div < 2.0f) div = 2.0f;
    sm_config_set_clkdiv(&c, div);

    /* Direcciones de los pines */
    pio_sm_set_consecutive_pindirs(s_pio, s_sm, PIN_TDI, 1, true);   /* salida */
    pio_sm_set_consecutive_pindirs(s_pio, s_sm, PIN_TDO, 1, false);  /* entrada */
    pio_sm_set_consecutive_pindirs(s_pio, s_sm, PIN_TCK, 1, true);   /* salida */

    /* Conectar TDI, TDO y TCK al PIO0 */
    io_bank0_hw->io[PIN_TDI].ctrl = GPIO_FUNC_PIO0;
    io_bank0_hw->io[PIN_TDO].ctrl = GPIO_FUNC_PIO0;
    io_bank0_hw->io[PIN_TCK].ctrl = GPIO_FUNC_PIO0;

    /* Inicializar y arrancar la state machine */
    pio_sm_init(s_pio, s_sm, s_offset, &c);
    pio_sm_set_enabled(s_pio, s_sm, true);

    /*
     * Pull-down interno en TDO: cuando no hay target conectado o está apagado,
     * TDO flota y el PIO leería bits aleatorios.  El pull-down fuerza nivel 0
     * estable en ausencia de señal, evitando IDCODEs falsos en el escaneo.
     */
    gpio_set_pulls(PIN_TDO, false, true);

    /*
     * Bypass del sincronizador de entrada de 2 ciclos en el pin TDO.
     * Elimina ~16 ns de latencia de captura a 125 MHz de clk_sys.
     * Es seguro porque TDO en JTAG se estabiliza tras el flanco descendente
     * de TCK y permanece válido durante todo el período TCK alto, que es
     * cuando el PIO ejecuta la instrucción 'in pins, 1 side 1'.
     * Técnica adoptada de pico-dirtyJtag (BOARD_PICO, mismo pinout).
     */
    hw_set_bits(&s_pio->input_sync_bypass, 1u << PIN_TDO);

    /* Reservar canales DMA: TX (tdi_buf → PIO TX FIFO) y RX (PIO RX FIFO → s_rx_words) */
    s_dma_tx = dma_claim_unused_channel(true);
    s_dma_rx = dma_claim_unused_channel(true);
}

void jtag_set_freq(uint32_t freq_khz) {
    if (freq_khz == 0u) freq_khz = 1u;
    s_freq_khz = freq_khz;
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

    uint32_t num_bytes = (len_bits + 7u) / 8u;

    /* Guardia: el buffer DMA s_rx_words tiene 1022 posiciones (u32).
     * Una transferencia mayor desborda el buffer → rechazar. */
    if (num_bytes > (uint32_t)(sizeof(s_rx_words) / sizeof(s_rx_words[0])))
        return false;

    /*
     * Vaciar bytes residuales del RX FIFO.
     * El 'push' al final del programa PIO puede dejar un word sobrante
     * cuando len_bits era múltiplo de 8 en la transferencia anterior.
     */
    while (!pio_sm_is_rx_fifo_empty(s_pio, s_sm))
        (void)pio_sm_get(s_pio, s_sm);

    /*
     * Enviar el contador de bits (N-1) directamente antes de lanzar los DMA.
     * El programa PIO comienza con 'pull block' para recogerlo.
     */
    pio_sm_put_blocking(s_pio, s_sm, len_bits - 1u);

    /*
     * TX DMA: transfiere tdi_buf al TX FIFO byte a byte (DMA_SIZE_8).
     * OUT shift a la derecha → LSB del byte sale primero → LSB-first JTAG.
     */
    dma_channel_config tx_cfg = dma_channel_get_default_config(s_dma_tx);
    channel_config_set_read_increment(&tx_cfg, true);
    channel_config_set_write_increment(&tx_cfg, false);
    channel_config_set_dreq(&tx_cfg, pio_get_dreq(s_pio, s_sm, true));
    channel_config_set_transfer_data_size(&tx_cfg, DMA_SIZE_8);
    dma_channel_configure(s_dma_tx, &tx_cfg,
        &s_pio->txf[s_sm], tdi_buf, num_bytes, true);

    /*
     * RX DMA: captura words de 32 bits del RX FIFO → s_rx_words (DMA_SIZE_32).
     * Con IN shift a la derecha, cada word del FIFO contiene el byte TDO
     * capturado en los bits [31:24].  La extracción con >> 24 se hace abajo.
     *
     * Ambos DMAs corren en paralelo: la CPU queda libre mientras el PIO
     * genera los clocks JTAG.  En modo multi-core (Core 1), esto permite
     * que Core 0 siga atendiendo USB durante la transferencia.
     */
    dma_channel_config rx_cfg = dma_channel_get_default_config(s_dma_rx);
    channel_config_set_read_increment(&rx_cfg, false);   /* origen fijo: RX FIFO */
    channel_config_set_write_increment(&rx_cfg, true);
    channel_config_set_dreq(&rx_cfg, pio_get_dreq(s_pio, s_sm, false));
    channel_config_set_transfer_data_size(&rx_cfg, DMA_SIZE_32);
    dma_channel_configure(s_dma_rx, &rx_cfg,
        s_rx_words, &s_pio->rxf[s_sm], num_bytes, true);

    /*
     * Esperar a que el DMA RX termine con timeout calculado.
     * Tiempo esperado = bits / freq_hz; margen 3× + 100 ms fijo para latencias
     * de arranque.  Si el target deja de responder (reset, corte de alimentación)
     * el DMA nunca completa → la SM PIO hace stall en 'out' esperando DREQ.
     * En ese caso abortamos y reiniciamos el PIO para no bloquear Core 1.
     *
     * El DMA TX termina siempre antes que el RX (el FIFO absorbe los datos),
     * por lo que basta supervisar el RX.
     */
    uint64_t expected_us = ((uint64_t)len_bits * 1000u) / s_freq_khz;
    uint64_t timeout_us  = expected_us * 3u + 100000u;   /* 3× margen + 100 ms */
    uint64_t deadline    = time_us_64() + timeout_us;

    while (dma_channel_is_busy(s_dma_rx)) {
        if (time_us_64() >= deadline) {
            jtag_pio_recover();
            return false;
        }
    }
    dma_channel_wait_for_finish_blocking(s_dma_tx);   /* ya casi terminado */

    /* Extraer bytes de los bits [31:24] de cada word */
    for (uint32_t i = 0u; i < num_bytes; i++)
        tdo_buf[i] = (uint8_t)(s_rx_words[i] >> 24);

    /*
     * Corrección del byte parcial final.
     * Los bits incompletos aterrizan en los bits altos del byte 3 del word;
     * desplazar a la derecha los alinea a las posiciones LSB que espera J-Link.
     */
    uint32_t rem = len_bits & 7u;
    if (rem != 0u)
        tdo_buf[num_bytes - 1u] >>= (8u - rem);

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

    /* Raise TMS=1 so the TAP will transition to Exit1 on the next TCK edge. */
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

bool jtag_pio_write(const uint8_t *tdi_buf, uint32_t len_bits) {
    static uint8_t tdo_discard[64];   /* 512 bits por iteración como máximo */

    while (len_bits > 0u) {
        uint32_t chunk_bits  = (len_bits > 512u) ? 512u : len_bits;
        uint32_t chunk_bytes = (chunk_bits + 7u) / 8u;

        if (chunk_bytes > (uint32_t)sizeof(tdo_discard)) {
            chunk_bytes = (uint32_t)sizeof(tdo_discard);
            chunk_bits  = chunk_bytes * 8u;
        }

        if (!jtag_pio_write_read(tdi_buf, tdo_discard, chunk_bits))
            return false;
        tdi_buf  += chunk_bytes;
        len_bits -= chunk_bits;
    }
    return true;
}
