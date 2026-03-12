#include "jlink_caps.h"
#include "jlink_protocol.h"
#include "jtag_pio.h"
#include "board_config.h"
#include "adc.h"
#include <string.h>

#include "hardware/structs/sio.h"
#include "hardware/structs/io_bank0.h"

/* Valores de FUNCSEL (RP2040 datasheet, Table 5) */
#define GPIO_FUNC_SIO   5u
#define GPIO_FUNC_PIO0  6u

/* ------------------------------------------------------------------ */
/*  Helpers para manipulación de bits en buffers de bytes.             */
/*  Se usan en el algoritmo híbrido PIO+SIO de hw_jtag3.              */
/* ------------------------------------------------------------------ */

/*
 * bits_extract — copia 'count' bits de src[src_off..src_off+count)
 * al inicio de dst (byte-alineado, bit 0 de dst = bit src_off de src).
 * Orden LSB-first (compatible con el protocolo J-Link).
 */
static void bits_extract(const uint8_t *src, uint32_t src_off,
                         uint8_t *dst, uint32_t count) {
    uint32_t dst_bytes = (count + 7u) / 8u;
    for (uint32_t j = 0u; j < dst_bytes; j++) dst[j] = 0u;
    for (uint32_t b = 0u; b < count; b++) {
        uint32_t p = src_off + b;
        if ((src[p >> 3u] >> (p & 7u)) & 1u)
            dst[b >> 3u] |= (uint8_t)(1u << (b & 7u));
    }
}

/*
 * bits_insert — copia 'count' bits de src (byte-alineado) a dst
 * a partir del bit 'dst_off' de dst.
 * Solo activa bits; dst debe estar inicializado a cero previamente.
 */
static void bits_insert(const uint8_t *src, uint32_t dst_off,
                        uint8_t *dst, uint32_t count) {
    for (uint32_t b = 0u; b < count; b++) {
        if ((src[b >> 3u] >> (b & 7u)) & 1u) {
            uint32_t p = dst_off + b;
            dst[p >> 3u] |= (uint8_t)(1u << (p & 7u));
        }
    }
}

/*
 * jtag_bitbang_bits — bit-bang SIO para 'count' bits de TDI/TMS
 * a partir del bit 'start' dentro de tdi_buf.
 * TMS ya debe estar fijado por el llamador antes de entrar.
 * TDI y TCK se cambian temporalmente a SIO y se restauran a PIO0 al salir.
 */
static void jtag_bitbang_bits(const uint8_t *tdi_buf, uint8_t *tdo_buf,
                               uint32_t start, uint32_t count) {
    io_bank0_hw->io[PIN_TDI].ctrl = GPIO_FUNC_SIO;
    io_bank0_hw->io[PIN_TCK].ctrl = GPIO_FUNC_SIO;
    sio_hw->gpio_oe_set = (1u << PIN_TDI) | (1u << PIN_TCK);

    for (uint32_t b = 0u; b < count; b++) {
        uint32_t p = start + b;
        uint32_t tdi = (tdi_buf[p >> 3u] >> (p & 7u)) & 1u;

        if (tdi) sio_hw->gpio_set = (1u << PIN_TDI);
        else     sio_hw->gpio_clr = (1u << PIN_TDI);

        sio_hw->gpio_set = (1u << PIN_TCK);               /* TCK alto */

        if ((sio_hw->gpio_in >> PIN_TDO) & 1u)            /* capturar TDO */
            tdo_buf[p >> 3u] |= (uint8_t)(1u << (p & 7u));

        sio_hw->gpio_clr = (1u << PIN_TCK);               /* TCK bajo */
    }

    io_bank0_hw->io[PIN_TDI].ctrl = GPIO_FUNC_PIO0;
    io_bank0_hw->io[PIN_TCK].ctrl = GPIO_FUNC_PIO0;
}

/* Buffers estáticos alineados para el run PIO actual (BSS, no stack) */
static uint8_t s_pio_tdi_tmp[TX_BUF_SIZE];
static uint8_t s_pio_tdo_tmp[TX_BUF_SIZE];

/*
 * Implementación de los comandos del protocolo J-Link.
 *
 * Los comandos de información (VERSION, GET_CAPS, GET_HW_VERSION, etc.) están
 * completamente implementados con valores correctos. Los comandos de acción
 * sobre el hardware (HW_JTAG3, HW_RESET, HW_TRST) devuelven respuestas
 * estructuralmente válidas pero sin mover los pines reales, a la espera de
 * que se implemente el motor PIO en jtag_pio.c.
 */

/*
 * Cadena exacta volcada del clon chino funcionando (USBlyzer, CSV líneas 42-49).
 * Wireshark muestra que la respuesta a EMU_CMD_VERSION (0x01) se envía en DOS
 * transferencias bulk separadas:
 *   Fase 1: [0x70, 0x00]  — longitud 112 (short packet, fin de transfer)
 *   Fase 2: 112 bytes     — string ASCII + relleno de ceros
 * jlink.sys lee los 2 bytes de longitud, luego emite un nuevo URB para 112 bytes.
 *
 * Formato capturado (hex):
 *   "J-Link V9 compiled Dec 13 2022 11:14:50" (40 bytes)
 *   0x00 (separador nulo — la DLL lee cadena1 hasta el \0)
 *   "Copyright 2003-2012 SEGGER: www.segger.com" (42 bytes)
 *   0x00 + ceros hasta 112 bytes
 *
 * NOTA: el separador \0 embebido se copia correctamente porque usamos
 * sizeof(s_ver_str)-1 que incluye el null interno pero no el terminador final.
 */
#define VERSION_BODY_LEN 112
static const char s_ver_str[] =
    "J-Link V9 compiled Mar 05 2026 21:14:50\0"
    "Copyright 2003-2012 SEGGER: www.segger.com";

void jlink_cmd_version(uint8_t *tx_buf, uint16_t *tx_len) {
    /* Fase 1: solo los 2 bytes de longitud, como short packet. */
    tx_buf[0] = VERSION_BODY_LEN & 0xFF;
    tx_buf[1] = (VERSION_BODY_LEN >> 8) & 0xFF;
    *tx_len = 2;
}

void jlink_cmd_version_body(uint8_t *tx_buf, uint16_t *tx_len) {
    /* Fase 2: string + ceros hasta completar VERSION_BODY_LEN bytes. */
    uint16_t slen = (uint16_t)(sizeof(s_ver_str) - 1);  /* sin '\0' */
    memset(tx_buf, 0, VERSION_BODY_LEN);
    memcpy(tx_buf, s_ver_str, slen);
    *tx_len = VERSION_BODY_LEN;
}

void jlink_cmd_get_caps(uint8_t *tx_buf, uint16_t *tx_len) {
    /* Respuesta: 4 bytes en little-endian con el bitmap de capabilities */
    uint32_t caps = JLINK_CAPS;
    memcpy(tx_buf, &caps, 4);
    *tx_len = 4;
}

/*
 * Array exacto de 32 bytes capturado del clon chino (CSV líneas 127-130):
 *   BF 7B FF B9  0D 7C B1 4F  21 00 00 00  00 00 00 00  ...00...
 * DWORD[0] = 0xB9FF7BBF = JLINK_CAPS (capabilities básicas, coincide con GET_CAPS)
 * DWORD[1] = 0x4FB17C0D (capabilities extendidas de la DLL v8.90+)
 * DWORD[2] = 0x00000021 (bits 0 y 5: BG Memory Access + Real-time power sense)
 * DWORDs[3-7] = 0x00000000
 */
static const uint8_t s_caps_ex[32] = {
    0xBF, 0x7B, 0xFF, 0xB9,   /* DWORD[0] */
    0x0D, 0x7C, 0xB1, 0x4F,   /* DWORD[1] */
    0x21, 0x00, 0x00, 0x00,   /* DWORD[2] */
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
};

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
    memcpy(tx_buf, s_caps_ex, count);
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
    memset(tx_buf, 0xFF, n);
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
        jtag_set_freq(jtag_speed_khz);
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
    /*
     * Tensión leída del ADC (GP26 = Vref/2 → multiplicar x2 en adc_read_vref_mv).
     * La DLL verifica que Vref > 1500 mV antes de continuar; si el target no está
     * conectado el divisor da ~0 V y la DLL detiene la operación (comportamiento correcto).
     */
    uint16_t vref_mv = adc_read_vref_mv();
    tx_buf[0] = (uint8_t)(vref_mv & 0xFFu);
    tx_buf[1] = (uint8_t)(vref_mv >> 8);
    tx_buf[2] = 0;     /* TDI    */
    tx_buf[3] = 0;     /* TMS    */
    tx_buf[4] = 0;     /* TCK    */
    tx_buf[5] = 0;     /* TDO    */
    tx_buf[6] = 1;     /* nTRST  (activo-bajo; inactivo = 1) */
    tx_buf[7] = 1;     /* nRESET (activo-bajo; inactivo = 1) */
    *tx_len = 8;
}

/* ------------------------------------------------------------------ */
/*  EMU_CMD_HW_JTAG3 (0xCF)                                          */
/*  Petición: cmd(1) + dummy(1) + numBits(2 LE) + TMS[] + TDI[]     */
/*  Respuesta: TDO[] solamente (sin byte de estado — según el driver  */
/*             OpenOCD j-link / libjaylink)                           */
/* ------------------------------------------------------------------ */
void jlink_cmd_hw_jtag3(const uint8_t *rx_buf, uint16_t rx_len,
                        uint8_t *tx_buf, uint16_t *tx_len) {
    if (rx_len < 4) {
        *tx_len = 0;
        return;
    }

    /*
     * numBits es un campo de 16 bits (2 bytes LE) — NO de 32 bits.
     * Formato real del protocolo (libjaylink, OpenOCD j-link driver):
     *   cmd(1) + dummy(1) + numBits(2 LE) + TMS[ceil(N/8)] + TDI[ceil(N/8)]
     * La acumulación multi-paquete se gestiona en main.c antes de llamar aquí,
     * por lo que rx_buf contiene el comando completo al entrar en esta función.
     */
    uint32_t num_bits  = (uint32_t)rx_buf[2] | ((uint32_t)rx_buf[3] << 8);
    uint32_t num_bytes = (num_bits + 7u) / 8u;

    if (num_bytes > TX_BUF_SIZE) {
        *tx_len = 0;
        return;
    }

    /* Verificar que el buffer acumulado contiene los datos de TMS y TDI completos */
    if ((uint32_t)rx_len < 4u + 2u * num_bytes) {
        *tx_len = 0;
        return;
    }

    const uint8_t *tms_buf = &rx_buf[4];
    const uint8_t *tdi_buf = &rx_buf[4u + num_bytes];

    /*
     * Algoritmo híbrido PIO+SIO:
     *
     * El stream TMS se divide en runs de valor constante (0 ó 1).
     * Para cada run:
     *   · TMS se fija por SIO (siempre en modo SIO).
     *   · Si run_bits < 8: bit-bang SIO (bajo overhead para transiciones TAP cortas).
     *   · Si run_bits ≥ 8: PIO con DMA — se extraen los bits a un buffer byte-alineado,
     *     se llama a jtag_pio_write_read, y se insertan los TDO de vuelta en tx_buf.
     *
     * TDI y TCK permanecen en modo PIO0 salvo durante los runs de bit-bang cortos,
     * donde se cambian temporalmente a SIO y se restauran al salir.
     * TDO es siempre legible desde sio_hw->gpio_in independientemente del FUNCSEL.
     *
     * Beneficio: las fases de datos (TMS=0, típicamente 32-1000+ bits) van a la
     * frecuencia del PIO configurada (por defecto 4 MHz), mientras que las
     * transiciones de estado TAP (1-6 bits) se sirven con bit-bang de bajo overhead.
     */
    memset(tx_buf, 0, num_bytes);

    uint32_t i = 0u;
    while (i < num_bits) {
        /* Valor de TMS para este run */
        uint8_t tms_val = (tms_buf[i >> 3u] >> (i & 7u)) & 1u;

        /* Encontrar el fin del run (último bit con el mismo TMS) */
        uint32_t run_end = i + 1u;
        while (run_end < num_bits &&
               ((tms_buf[run_end >> 3u] >> (run_end & 7u)) & 1u) == tms_val)
            run_end++;
        uint32_t run_bits = run_end - i;

        /* Fijar TMS para el run completo */
        if (tms_val) sio_hw->gpio_set = (1u << PIN_TMS);
        else         sio_hw->gpio_clr = (1u << PIN_TMS);

        if (run_bits < 8u) {
            /* Run corto: transición de estado TAP — bit-bang directo */
            jtag_bitbang_bits(tdi_buf, tx_buf, i, run_bits);
        } else {
            /* Run largo: fase de datos — usar PIO con DMA */
            uint32_t run_bytes = (run_bits + 7u) / 8u;
            bits_extract(tdi_buf, i, s_pio_tdi_tmp, run_bits);
            for (uint32_t j = 0u; j < run_bytes; j++) s_pio_tdo_tmp[j] = 0u;
            jtag_pio_write_read(s_pio_tdi_tmp, s_pio_tdo_tmp, run_bits);
            bits_insert(s_pio_tdo_tmp, i, tx_buf, run_bits);
        }

        i = run_end;
    }

    *tx_len = (uint16_t)num_bytes;
}

/* ------------------------------------------------------------------ */
/*  EMU_CMD_HW_RESET0/1 (0xDC/0xDD) — control de nRESET. Sin respuesta. */
/* ------------------------------------------------------------------ */
void jlink_cmd_hw_reset(const uint8_t *rx_buf, uint16_t rx_len,
                        uint8_t *tx_buf, uint16_t *tx_len) {
    (void)rx_len; (void)tx_buf;
    /* RESET0 → nRST bajo (activo); RESET1 → nRST alto (inactivo) */
    if (rx_buf[0] == EMU_CMD_HW_RESET0)
        sio_hw->gpio_clr = (1u << PIN_RST);
    else
        sio_hw->gpio_set = (1u << PIN_RST);
    *tx_len = 0;
}

/* ------------------------------------------------------------------ */
/*  EMU_CMD_HW_TRST0/1 (0xDE/0xDF) — control de nTRST. Sin respuesta. */
/* ------------------------------------------------------------------ */
void jlink_cmd_hw_trst(const uint8_t *rx_buf, uint16_t rx_len,
                       uint8_t *tx_buf, uint16_t *tx_len) {
    (void)rx_len; (void)tx_buf;
    /* TRST0 → nTRST bajo (activo); TRST1 → nTRST alto (inactivo) */
    if (rx_buf[0] == EMU_CMD_HW_TRST0)
        sio_hw->gpio_clr = (1u << PIN_TRST);
    else
        sio_hw->gpio_set = (1u << PIN_TRST);
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

/* ------------------------------------------------------------------ */
/*  EMU_CMD_IDSEGGER (0x16) — handshake de autenticación SEGGER.      */
/*                                                                      */
/*  El host envía dos variantes:                                        */
/*    • 16 02 00 00 00 00 49 44 53 45 47 47 45 52  (subcmd = 0x02)    */
/*    • 16 00 00 00 00 00 49 44 53 45 47 47 45 52  (subcmd = 0x00)    */
/*                                                                      */
/*  Respuesta en DOS fases (igual que EMU_CMD_VERSION):                 */
/*    Fase 1: 00 09 00 00  — cabecera que anuncia 2304 bytes           */
/*    Fase 2: 2304 bytes   — volcado de licencias (ver body más abajo) */
/*                                                                      */
/*  Capturado del clon chino con jlink.sys V8.90 (CSV líneas 207-282). */
/* ------------------------------------------------------------------ */
void jlink_cmd_idsegger(uint8_t *tx_buf, uint16_t *tx_len) {
    /* Fase 1: cabecera de 4 bytes que indica el tamaño del payload.  */
    tx_buf[0] = 0x00;
    tx_buf[1] = 0x09;  /* 0x0900 = 2304 bytes de payload */
    tx_buf[2] = 0x00;
    tx_buf[3] = 0x00;
    *tx_len = 4;
}

/* ------------------------------------------------------------------ */
/*  jlink_cmd_idsegger_body — payload de 2304 bytes (Fase 2).         */
/*                                                                      */
/*  subcmd 0x02 — tabla de licencias de módulos (RDI, GDB, JFlash…).  */
/*    Bytes 0-3:   56 00 28 04  (header del bloque)                    */
/*    Bytes 4-31:  0xFF                                                 */
/*    Offset 32:   "RDI\0"                                             */
/*    Offset 48:   "GDB\0"                                             */
/*    Offset 64:   "FlashDL\0"                                         */
/*    Offset 80:   "FlashBP\0"                                         */
/*    Offset 96:   "JFlash\0"                                          */
/*    Resto:       0xFF                                                 */
/*                                                                      */
/*  subcmd 0x00 — bloque de información de versión/modelo.             */
/*    Bytes 0-7:  00 01 FF FF 01 00 00 00  (header del bloque)         */
/*    Bytes 8-67: 0xFF                                                  */
/*    Offset 68:  "J-Link V9.3 Plus\0"                                 */
/*    Resto:      0xFF                                                  */
/* ------------------------------------------------------------------ */
void jlink_cmd_idsegger_body(uint8_t subcmd, uint8_t *tx_buf, uint16_t *tx_len) {
    memset(tx_buf, 0xFF, IDSEGGER_BODY_LEN);

    if (subcmd == 0x02) {
        /* Header */
        tx_buf[0] = 0x56; tx_buf[1] = 0x00;
        tx_buf[2] = 0x28; tx_buf[3] = 0x04;

        /* Tabla de módulos con licencia */
        memcpy(&tx_buf[32], "RDI",     3); tx_buf[35]  = 0x00;
        memcpy(&tx_buf[48], "GDB",     3); tx_buf[51]  = 0x00;
        memcpy(&tx_buf[64], "FlashDL", 7); tx_buf[71]  = 0x00;
        memcpy(&tx_buf[80], "FlashBP", 7); tx_buf[87]  = 0x00;
        memcpy(&tx_buf[96], "JFlash",  6); tx_buf[102] = 0x00;
    } else {
        /* subcmd 0x00 — bloque de versión/modelo */
        tx_buf[0] = 0x00; tx_buf[1] = 0x01;
        tx_buf[2] = 0xFF; tx_buf[3] = 0xFF;
        tx_buf[4] = 0x01; tx_buf[5] = 0x00;
        tx_buf[6] = 0x00; tx_buf[7] = 0x00;

        memcpy(&tx_buf[68], "J-Link V9.3 Plus", 16);
        tx_buf[84] = 0x00;
    }

    *tx_len = IDSEGGER_BODY_LEN;
}

/* ------------------------------------------------------------------ */
/*  Comandos de telemetría interna (caja negra).                       */
/*  La DLL emite estos comandos y espera una respuesta de tamaño fijo. */
/*  Si no respondemos, el endpoint USB se desincroniza.                */
/*                                                                      */
/*  Valores capturados del clon chino (CSV líneas 343-402):            */
/*    0x0E sub=0x0B → 01 00 00 00  (4 bytes)                          */
/*    0x0E sub=otro → FE FF FF FF  (4 bytes)                           */
/*    0x0D          → 10 47 04 00  (4 bytes)                           */
/*    0x09          → 92 bytes con estructura específica               */
/* ------------------------------------------------------------------ */
void jlink_cmd_unknown_0e(const uint8_t *rx_buf, uint16_t rx_len,
                          uint8_t *tx_buf, uint16_t *tx_len) {
    /* Sub-comando 0x0B → 01 00 00 00; sub-comando 0x05 → 00 00 00 00;
     * resto → FE FF FF FF */
    if (rx_len >= 2 && rx_buf[1] == 0x0B) {
        tx_buf[0] = 0x01; tx_buf[1] = 0x00;
        tx_buf[2] = 0x00; tx_buf[3] = 0x00;
    } else if (rx_len >= 2 && rx_buf[1] == 0x05) {
        tx_buf[0] = 0x00; tx_buf[1] = 0x00;
        tx_buf[2] = 0x00; tx_buf[3] = 0x00;
    } else {
        tx_buf[0] = 0xFE; tx_buf[1] = 0xFF;
        tx_buf[2] = 0xFF; tx_buf[3] = 0xFF;
    }
    *tx_len = 4;
}

void jlink_cmd_unknown_0d(uint8_t *tx_buf, uint16_t *tx_len) {
    tx_buf[0] = 0x10; tx_buf[1] = 0x47;
    tx_buf[2] = 0x04; tx_buf[3] = 0x00;
    *tx_len = 4;
}

/*
 * CMD 0x09 — respuesta de 92 bytes capturada del clon chino.
 *
 * Estructura (CSV línea 394-402):
 *   [0..7]   03 00 05 00 10 00 04 00  (cabecera fija)
 *   [8..11]  echo de rx_buf[2..5]     (eco de los bytes de entrada)
 *   [12..17] 00 00 00 00 00 00
 *   [18..19] 03 00
 *   [20..23] A2 24 07 00
 *   [24..87] 00 × 64
 *   [88..91] D0 0C 00 00              (voltaje ~3280 mV)
 */
void jlink_cmd_unknown_09(const uint8_t *rx_buf, uint16_t rx_len,
                          uint8_t *tx_buf, uint16_t *tx_len) {
    memset(tx_buf, 0, 92);

    tx_buf[0] = 0x03; tx_buf[1] = 0x00;
    tx_buf[2] = 0x05; tx_buf[3] = 0x00;
    tx_buf[4] = 0x10; tx_buf[5] = 0x00;
    tx_buf[6] = 0x04; tx_buf[7] = 0x00;

    if (rx_len >= 6) {
        tx_buf[8]  = rx_buf[2];
        tx_buf[9]  = rx_buf[3];
        tx_buf[10] = rx_buf[4];
        tx_buf[11] = rx_buf[5];
    }

    tx_buf[18] = 0x03; tx_buf[19] = 0x00;
    tx_buf[20] = 0xA2; tx_buf[21] = 0x24;
    tx_buf[22] = 0x07; tx_buf[23] = 0x00;

    tx_buf[88] = 0xD0; tx_buf[89] = 0x0C;
    tx_buf[90] = 0x00; tx_buf[91] = 0x00;

    *tx_len = 92;
}
