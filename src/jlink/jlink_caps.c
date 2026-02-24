#include "jlink_caps.h"
#include "jlink_protocol.h"
#include <string.h>

/*
 * Implementación de los comandos del protocolo J-Link.
 *
 * Los comandos de información (VERSION, GET_CAPS, GET_HW_VERSION, etc.) están
 * completamente implementados con valores correctos. Los comandos de acción
 * sobre el hardware (HW_JTAG3, HW_RESET, HW_TRST) devuelven respuestas
 * estructuralmente válidas pero sin mover los pines reales, a la espera de
 * que se implemente el motor PIO en jtag_pio.c.
 */

void jlink_cmd_version(uint8_t *tx_buf, uint16_t *tx_len) {
    /*
     * El formato es: 2 bytes de longitud (little-endian) seguidos de la cadena ASCII.
     * JLink Commander analiza el patrón "Vd.dd" para determinar la versión del firmware.
     * La cadena debe parecerse a la de un J-Link real para que la DLL la acepte.
     */
    /*
     * Cadena fija idéntica a la de un J-Link V9 real.
     * - Formato "V9.20": la DLL espera al menos dos dígitos después del punto.
     * - Fecha anterior a cualquier versión de JLink Commander → sin rechazo por fecha.
     * - Longitud = strlen sin '\0': las implementaciones que funcionan no lo incluyen.
     */
    static const char ver[] = "J-Link V9.20 compiled Feb 27 2018 10:52:34";
    uint16_t slen = (uint16_t)(sizeof(ver) - 1);  /* strlen, sin '\0' */
    tx_buf[0] = slen & 0xFF;
    tx_buf[1] = (slen >> 8) & 0xFF;
    memcpy(&tx_buf[2], ver, slen);
    *tx_len = 2 + slen;
}

void jlink_cmd_get_caps(uint8_t *tx_buf, uint16_t *tx_len) {
    /* Respuesta: 4 bytes en little-endian con el bitmap de capabilities */
    uint32_t caps = JLINK_CAPS;
    memcpy(tx_buf, &caps, 4);
    *tx_len = 4;
}

void jlink_cmd_get_caps_ex(const uint8_t *rx_buf, uint16_t rx_len,
                            uint8_t *tx_buf, uint16_t *tx_len) {
    /*
     * Versión extendida: hasta 32 bytes (256 bits) de capabilities.
     * Algunas versiones de la DLL mandan un byte opcional con el número
     * de bytes a devolver (ej: 0xED 0x20 para 32 bytes).
     * Si no hay parámetro, se devuelven los 32 bytes completos.
     */
    uint16_t count = 32;
    if (rx_len >= 2 && rx_buf[1] > 0 && rx_buf[1] <= 32)
        count = rx_buf[1];
    memset(tx_buf, 0, count);
    uint32_t caps = JLINK_CAPS;
    if (count >= 4) memcpy(tx_buf, &caps, 4);
    *tx_len = count;
}

void jlink_cmd_get_hw_version(uint8_t *tx_buf, uint16_t *tx_len) {
    /* Respuesta: 4 bytes en little-endian con la versión de hardware */
    uint32_t hw_ver = JLINK_HW_VERSION;
    memcpy(tx_buf, &hw_ver, 4);
    *tx_len = 4;
}

void jlink_cmd_get_hw_info(const uint8_t *rx_buf, uint16_t rx_len,
                            uint8_t *tx_buf, uint16_t *tx_len) {
    /*
     * Protocolo: Host → cmd(1) + mask(4 LE).
     * Respuesta:  popcount(mask) × 4 bytes — un uint32 por cada bit activo.
     * Si no llega la máscara, se asumen 4 campos (16 bytes) como fallback.
     * Todos los valores a cero hasta que se implemente la lectura ADC.
     */
    uint32_t mask = 0x0F;   /* fallback: 4 campos */
    if (rx_len >= 5) {
        mask = (uint32_t)rx_buf[1]
             | ((uint32_t)rx_buf[2] << 8)
             | ((uint32_t)rx_buf[3] << 16)
             | ((uint32_t)rx_buf[4] << 24);
    }
    uint16_t n = (uint16_t)__builtin_popcount(mask) * 4;
    if (n == 0) n = 4;      /* al menos un campo para no enviar 0 bytes */
    memset(tx_buf, 0, n);
    *tx_len = n;
}

void jlink_cmd_get_speeds(uint8_t *tx_buf, uint16_t *tx_len) {
    /*
     * Respuesta: frecuencia base (4 bytes) + divisor mínimo (2 bytes).
     * La velocidad máxima es base_freq / min_div = 48 MHz / 4 = 12 MHz.
     * La DLL usa estos valores para calcular qué velocidades puede pedir.
     */
    uint32_t base = JLINK_BASE_FREQ;
    uint16_t div  = JLINK_MIN_DIV;
    memcpy(&tx_buf[0], &base, 4);
    memcpy(&tx_buf[4], &div, 2);
    *tx_len = 6;
}

void jlink_cmd_select_if(const uint8_t *rx_buf, uint16_t rx_len,
                         uint8_t *tx_buf, uint16_t *tx_len) {
    /*
     * El byte [1] del comando indica la interfaz solicitada (0=JTAG, 1=SWD).
     * La respuesta son 4 bytes con el número de la interfaz anterior.
     * Por ahora siempre devolvemos JTAG como interfaz previa.
     */
    (void)rx_buf;
    (void)rx_len;
    uint32_t prev_if = JLINK_IF_JTAG;
    memcpy(tx_buf, &prev_if, 4);
    *tx_len = 4;
}

/* ---- Estado interno ---- */
static uint16_t jtag_speed_khz = JLINK_DEFAULT_SPEED;

/* ------------------------------------------------------------------ */
/*  EMU_CMD_SET_SPEED (0x05)                                          */
/*  Petición: cmd(1) + speed_kHz(2 LE)                               */
/*  Respuesta: ninguna                                                */
/* ------------------------------------------------------------------ */
void jlink_cmd_set_speed(const uint8_t *rx_buf, uint16_t rx_len,
                         uint8_t *tx_buf, uint16_t *tx_len) {
    (void)tx_buf;
    if (rx_len >= 3) {
        jtag_speed_khz = (uint16_t)(rx_buf[1] | (rx_buf[2] << 8));
        /* TODO: traducir jtag_speed_khz al divisor del PIO y configurarlo */
    }
    *tx_len = 0;
}

/* ------------------------------------------------------------------ */
/*  EMU_CMD_GET_STATE (0x07)                                          */
/*  Respuesta: 8 bytes                                                */
/*    [0..1] tensión objetivo en mV (LE)                              */
/*    [2] TDI  [3] TMS  [4] TCK  [5] TDO  [6] nTRST  [7] nRESET    */
/* ------------------------------------------------------------------ */
void jlink_cmd_get_state(uint8_t *tx_buf, uint16_t *tx_len) {
    /* Tensión fija de 3.3 V hasta que el ADC esté implementado */
    uint16_t vref_mv = 3300;
    tx_buf[0] = vref_mv & 0xFF;
    tx_buf[1] = (vref_mv >> 8) & 0xFF;
    tx_buf[2] = 1;   /* TDI    = alto */
    tx_buf[3] = 1;   /* TMS    = alto */
    tx_buf[4] = 0;   /* TCK    = bajo */
    tx_buf[5] = 1;   /* TDO    = alto */
    tx_buf[6] = 1;   /* nTRST  = alto (no activo) */
    tx_buf[7] = 1;   /* nRESET = alto (no activo) */
    *tx_len = 8;
}

/* ------------------------------------------------------------------ */
/*  EMU_CMD_HW_JTAG3 (0xCF)                                          */
/*  Petición: cmd(1) + dummy(1) + numBits(4 LE) + TMS[] + TDI[]     */
/*  Respuesta: TDO[] solamente (sin byte de estado — según el driver  */
/*             OpenOCD j-link)                                         */
/* ------------------------------------------------------------------ */
void jlink_cmd_hw_jtag3(const uint8_t *rx_buf, uint16_t rx_len,
                        uint8_t *tx_buf, uint16_t *tx_len) {
    if (rx_len < 6) {
        *tx_len = 0;
        return;
    }

    uint32_t num_bits = rx_buf[2]
                      | ((uint32_t)rx_buf[3] << 8)
                      | ((uint32_t)rx_buf[4] << 16)
                      | ((uint32_t)rx_buf[5] << 24);

    uint32_t num_bytes = (num_bits + 7) / 8;

    if (num_bytes > TX_BUF_SIZE) {
        *tx_len = 0;
        return;
    }

    /* TODO: enviar TMS y TDI al PIO y capturar TDO.
     * Por ahora devolvemos ceros (sin objetivo conectado). */
    memset(tx_buf, 0, num_bytes);
    *tx_len = (uint16_t)num_bytes;
}

/* ------------------------------------------------------------------ */
/*  EMU_CMD_HW_RESET0/1 (0xDC/0xDD) — control de nRESET. Sin respuesta. */
/* ------------------------------------------------------------------ */
void jlink_cmd_hw_reset(const uint8_t *rx_buf, uint16_t rx_len,
                        uint8_t *tx_buf, uint16_t *tx_len) {
    (void)rx_buf; (void)rx_len; (void)tx_buf;
    /* TODO: activar/desactivar el GPIO nRESET según el comando recibido */
    *tx_len = 0;
}

/* ------------------------------------------------------------------ */
/*  EMU_CMD_HW_TRST0/1 (0xDE/0xDF) — control de nTRST. Sin respuesta. */
/* ------------------------------------------------------------------ */
void jlink_cmd_hw_trst(const uint8_t *rx_buf, uint16_t rx_len,
                       uint8_t *tx_buf, uint16_t *tx_len) {
    (void)rx_buf; (void)rx_len; (void)tx_buf;
    /* TODO: activar/desactivar el GPIO nTRST según el comando recibido */
    *tx_len = 0;
}

/* ------------------------------------------------------------------ */
/*  EMU_CMD_GET_MAX_MEM_BLOCK (0xD4)                                  */
/*  Respuesta: 4 bytes (LE) con el tamaño máximo de bloque.           */
/* ------------------------------------------------------------------ */
void jlink_cmd_get_max_mem_block(uint8_t *tx_buf, uint16_t *tx_len) {
    uint32_t max_block = 1024;
    memcpy(tx_buf, &max_block, 4);
    *tx_len = 4;
}

/* ------------------------------------------------------------------ */
/*  Bloque de configuración del J-Link (256 bytes).                   */
/*  Se inicializa a ceros (valores por defecto).                      */
/*  jlink.sys escribe su config vía WRITE_CONFIG y la lee de vuelta   */
/*  vía READ_CONFIG para validarla — necesitamos devolver exactamente */
/*  lo que jlink.sys escribió, de lo contrario entra en bucle.        */
/* ------------------------------------------------------------------ */
static uint8_t jlink_config[256];

void jlink_config_update(const uint8_t *data, uint16_t len) {
    if (len > 256) len = 256;
    memcpy(jlink_config, data, len);
}

/* ------------------------------------------------------------------ */
/*  EMU_CMD_READ_CONFIG (0xF2)                                        */
/*  Respuesta: 256 bytes con el bloque de configuración actual.       */
/* ------------------------------------------------------------------ */
void jlink_cmd_read_config(uint8_t *tx_buf, uint16_t *tx_len) {
    memcpy(tx_buf, jlink_config, 256);
    *tx_len = 256;
}

/* ------------------------------------------------------------------ */
/*  EMU_CMD_WRITE_CONFIG (0xF3)                                       */
/*  Petición: cmd(1) + config[256] = 257 bytes en total.             */
/*  IMPORTANTE: este comando es multi-paquete (257 > 64 bytes).      */
/*  La acumulación real se hace en main.c vía jlink_config_update().  */
/*  Este handler se llama solo para el primer paquete (con 0xF3).    */
/* ------------------------------------------------------------------ */
void jlink_cmd_write_config(const uint8_t *rx_buf, uint16_t rx_len,
                             uint8_t *tx_buf, uint16_t *tx_len) {
    (void)rx_buf; (void)rx_len; (void)tx_buf;
    *tx_len = 0;  /* Sin respuesta; la acumulación la gestiona main.c */
}
