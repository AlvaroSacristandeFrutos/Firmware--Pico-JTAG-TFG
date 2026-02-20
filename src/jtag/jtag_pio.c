#include "jtag_pio.h"

/*
 * Motor JTAG basado en PIO — pendiente de implementar.
 *
 * jtag_pio_init() deberá:
 *   - Cargar el programa jtag_xfer en la memoria de instrucciones de PIO0
 *   - Configurar SM0: out_pin=GP16 (TDI), in_pin=GP17 (TDO), sideset=GP18 (TCK)
 *   - Activar autopush/autopull a 8 bits para que el DMA trabaje por bytes
 *   - Calcular y programar el divisor de reloj para la frecuencia por defecto
 *   - Habilitar SM0
 *   - Configurar GP19 (TMS) como salida SIO (el PIO solo controla TCK/TDI/TDO)
 *   - Configurar GP20 (nRST) y GP21 (nTRST) como open-drain
 *   - Reservar dos canales DMA: uno TX (RAM → PIO FIFO) y otro RX (PIO FIFO → RAM)
 *
 * jtag_pio_write_read() deberá:
 *   - Escribir el número de bits (N-1) en el TX FIFO
 *   - Lanzar el DMA de TX con tdi_buf como fuente
 *   - Lanzar el DMA de RX con tdo_buf como destino
 *   - Esperar a que el DMA de RX complete
 *   - Si len_bits % 8 != 0, ajustar el último byte (el PIO alinea los bits
 *     sobrantes a la derecha dentro del byte)
 */

void jtag_pio_init(void) {
    (void)0;
}

void jtag_pio_write_read(const uint8_t *tdi_buf, uint8_t *tdo_buf,
                         uint32_t len_bits) {
    (void)tdi_buf;
    (void)tdo_buf;
    (void)len_bits;
}

void jtag_pio_write(const uint8_t *tdi_buf, uint32_t len_bits) {
    (void)tdi_buf;
    (void)len_bits;
}

void jtag_set_freq(uint32_t freq_khz) {
    (void)freq_khz;
}
