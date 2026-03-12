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
#include "hardware/structs/sio.h"
#include "hardware/structs/io_bank0.h"

/* Valores de FUNCSEL para los pines GPIO (RP2040 datasheet, Table 5) */
#define GPIO_FUNC_SIO   5u
#define GPIO_FUNC_PIO0  6u

static PIO  s_pio    = pio0;
static uint s_sm     = 0;
static uint s_offset = 0;
static int  s_dma_tx = -1;
static int  s_dma_rx = -1;

/*
 * Buffer intermedio para el DMA RX.
 * jtag_pio_write_read usa IN shift_right → los datos capturados quedan en
 * los bits [31:24] de cada word de 32 bits del RX FIFO.  El DMA lee words
 * completos (DMA_SIZE_32) aquí; la extracción de bytes se hace después con >> 24.
 * Tamaño máximo: num_bytes ≤ 1022 (CMD_BUF_SIZE/2 - 2) para hw_jtag3.
 */
static uint32_t s_rx_words[1022];

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
     * Cada bit JTAG requiere 2 ciclos de PIO (TCK bajo + TCK alto).
     *   div = clk_sys / (freq_hz * 2)
     */
    float div = (float)clock_get_hz(clk_sys) /
                ((float)(4000u * 1000u) * 2.0f);  /* 4000 kHz por defecto */
    if (div < 1.0f) div = 1.0f;
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
     * Bypass del sincronizador de entrada de 2 ciclos en el pin TDO.
     * Elimina ~16 ns de latencia de captura a 125 MHz de clk_sys.
     * Es seguro porque TDO en JTAG está síncrono con TCK y ya se ha
     * estabilizado cuando el PIO lo muestrea (flanco descendente de TCK).
     * Técnica adoptada de pico-dirtyJtag (BOARD_PICO, mismo pinout).
     */
    hw_set_bits(&s_pio->input_sync_bypass, 1u << PIN_TDO);

    /* Reservar canales DMA: TX (tdi_buf → PIO TX FIFO) y RX (PIO RX FIFO → s_rx_words) */
    s_dma_tx = dma_claim_unused_channel(true);
    s_dma_rx = dma_claim_unused_channel(true);
}

void jtag_set_freq(uint32_t freq_khz) {
    if (freq_khz == 0u) freq_khz = 1u;
    float div = (float)clock_get_hz(clk_sys) /
                ((float)freq_khz * 1000.0f * 2.0f);
    if (div < 1.0f) div = 1.0f;
    pio_sm_set_clkdiv(s_pio, s_sm, div);
}

void jtag_pio_write_read(const uint8_t *tdi_buf, uint8_t *tdo_buf,
                         uint32_t len_bits) {
    if (len_bits == 0u) return;

    uint32_t num_bytes = (len_bits + 7u) / 8u;

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

    /* Esperar a que ambos DMA terminen */
    dma_channel_wait_for_finish_blocking(s_dma_tx);
    dma_channel_wait_for_finish_blocking(s_dma_rx);

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
}

void jtag_pio_write(const uint8_t *tdi_buf, uint32_t len_bits) {
    static uint8_t tdo_discard[64];   /* 512 bits por iteración como máximo */

    while (len_bits > 0u) {
        uint32_t chunk_bits  = (len_bits > 512u) ? 512u : len_bits;
        uint32_t chunk_bytes = (chunk_bits + 7u) / 8u;

        if (chunk_bytes > (uint32_t)sizeof(tdo_discard)) {
            chunk_bytes = (uint32_t)sizeof(tdo_discard);
            chunk_bits  = chunk_bytes * 8u;
        }

        jtag_pio_write_read(tdi_buf, tdo_discard, chunk_bits);
        tdi_buf  += chunk_bytes;
        len_bits -= chunk_bits;
    }
}
