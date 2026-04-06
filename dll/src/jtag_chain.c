/*
 * jtag_chain.c — Operaciones JTAG de alto nivel sobre pico_transact().
 */

/* Límite superior para la detección de IR total en jtag_scan_chain().
 * 32 dispositivos × 128 bits IR máx = 4096. El loop termina antes si
 * la cadena es más corta; este valor actúa como guardia de seguridad. */
#define JTAG_MAX_IR_TOTAL_BITS  4096u

#include "jtag_chain.h"
#include "pico_transport.h"
#include "dll_state.h"

#include <string.h>

/* ---------------------------------------------------------------------- */
/*  Clasificador TMS                                                       */
/* ---------------------------------------------------------------------- */

tms_class_t classify_tms(const uint8_t *pTMS, uint32_t numBits) {
    if (numBits == 0u)
        return TMS_ALL_ZERO;

    uint32_t num_bytes = (numBits + 7u) / 8u;
    uint32_t all_zeros = 1u;

    for (uint32_t i = 0u; i < num_bytes; i++) {
        uint8_t mask = (i == num_bytes - 1u && (numBits & 7u))
                       ? (uint8_t)((1u << (numBits & 7u)) - 1u)
                       : 0xFFu;
        if (pTMS[i] & mask) {
            all_zeros = 0u;
            break;
        }
    }

    if (all_zeros)
        return TMS_ALL_ZERO;

    /* Si hay bits TMS=1, verificar si es el caso simple: todos los bits anteriores
     * al último son TMS=0 y solo el último es TMS=1.  Este patrón es el más habitual
     * (desplazamiento DR/IR con salida al final) y se puede despachar con un único
     * CMD_SHIFT_DATA(exit=true) preservando TDI.
     * Cualquier otro patrón (TMS mixto) requiere procesamiento bit a bit. */
    uint32_t last = numBits - 1u;
    bool last_tms = (bool)((pTMS[last >> 3u] >> (last & 7u)) & 1u);
    if (last_tms) {
        bool simple = true;
        for (uint32_t i = 0u; i < last && simple; i++) {
            if ((pTMS[i >> 3u] >> (i & 7u)) & 1u)
                simple = false;
        }
        if (simple)
            return TMS_NAV_ONLY;   /* N-1 ceros + último uno: usa shift_data(exit=true) */
    }
    return TMS_MIXED;
}

/* ---------------------------------------------------------------------- */
/*  Primitivas de transferencia                                            */
/* ---------------------------------------------------------------------- */

/* Buffers estáticos reutilizables: evitan malloc/free por cada transferencia.
 * PICO_MAX_PAYLOAD = 4096. Header máximo = 5 bytes (shift_data). */
static uint8_t s_tx_buf[5u + PICO_MAX_PAYLOAD];
static uint8_t s_rx_buf[PICO_MAX_PAYLOAD];

bool jtag_shift_data(const uint8_t *pTDI, uint8_t *pTDO,
                     uint32_t numBits, bool exit) {
    if (numBits == 0u)
        return true;

    uint32_t num_bytes   = (numBits + 7u) / 8u;
    if (num_bytes > PICO_MAX_PAYLOAD) return false;
    uint16_t payload_len = (uint16_t)(5u + num_bytes);

    /* Payload en buffer estático: [numBits:u32 LE][exit:u8][TDI bytes] */
    s_tx_buf[0] = (uint8_t)( numBits        & 0xFFu);
    s_tx_buf[1] = (uint8_t)((numBits >>  8) & 0xFFu);
    s_tx_buf[2] = (uint8_t)((numBits >> 16) & 0xFFu);
    s_tx_buf[3] = (uint8_t)((numBits >> 24) & 0xFFu);
    s_tx_buf[4] = exit ? 1u : 0u;
    memcpy(s_tx_buf + 5u, pTDI, num_bytes);

    uint16_t rx_len = 0u;
    bool ok = pico_transact(g_hCOM, CMD_SHIFT_DATA,
                            s_tx_buf, payload_len,
                            pTDO ? s_rx_buf : NULL, &rx_len,
                            2000u);

    if (ok && pTDO && rx_len == (uint16_t)num_bytes)
        memcpy(pTDO, s_rx_buf, num_bytes);

    return ok;
}

bool jtag_write_tms(const uint8_t *pTMS, uint32_t numBits) {
    if (numBits == 0u)
        return true;

    uint32_t num_bytes   = (numBits + 7u) / 8u;
    if (num_bytes > PICO_MAX_PAYLOAD) return false;
    uint16_t payload_len = (uint16_t)(2u + num_bytes);

    /* Payload en buffer estático: [numBits:u16 LE][TMS bytes] */
    s_tx_buf[0] = (uint8_t)(numBits & 0xFFu);
    s_tx_buf[1] = (uint8_t)(numBits >> 8u);
    memcpy(s_tx_buf + 2u, pTMS, num_bytes);

    return pico_transact(g_hCOM, CMD_WRITE_TMS,
                         s_tx_buf, payload_len,
                         NULL, NULL, 2000u);
}

bool jtag_store_raw_bitbang(const uint8_t *pTDI, uint8_t *pTDO,
                             const uint8_t *pTMS, uint32_t numBits) {
    if (pTDO)
        memset(pTDO, 0, (numBits + 7u) / 8u);

    for (uint32_t i = 0u; i < numBits; i++) {
        bool tms = (bool)((pTMS[i >> 3u] >> (i & 7u)) & 1u);
        bool tdi = (bool)((pTDI[i >> 3u] >> (i & 7u)) & 1u);

        /* Generar el bit de TMS */
        uint8_t tms_byte = tms ? 1u : 0u;
        if (!jtag_write_tms(&tms_byte, 1u))
            return false;

        /* Desplazar 1 bit TDI, capturar TDO */
        uint8_t tdi_byte = tdi ? 1u : 0u;
        uint8_t tdo_byte = 0u;
        if (!jtag_shift_data(&tdi_byte, pTDO ? &tdo_byte : NULL, 1u, false))
            return false;

        if (pTDO && (tdo_byte & 1u))
            pTDO[i >> 3u] |= (uint8_t)(1u << (i & 7u));
    }
    return true;
}

/* ---------------------------------------------------------------------- */
/*  Lectura de IDCODE y escaneo de cadena                                 */
/* ---------------------------------------------------------------------- */

uint32_t jtag_read_idcode(void) {
    /* Reset TAP → Test-Logic-Reset → Run-Test/Idle */
    uint8_t tms_rst[1] = {0x1Fu};  /* 5 bits TMS=1 */
    jtag_write_tms(tms_rst, 5u);
    uint8_t tms_rti[1] = {0x00u};
    jtag_write_tms(tms_rti, 1u);

    /* RTI → Shift-DR: TMS = 1,0,0  (Select-DR-Scan → Capture-DR → Shift-DR) */
    uint8_t tms_shdr[1] = {0x01u};  /* 3 bits: bit0=1, bit1=0, bit2=0 */
    jtag_write_tms(tms_shdr, 3u);

    /* Desplazar 32 bits con TMS=1 en el último bit → Exit1-DR */
    uint8_t tdi[4] = {0xFFu, 0xFFu, 0xFFu, 0xFFu};
    uint8_t tdo[4] = {0};
    jtag_shift_data(tdi, tdo, 32u, true);

    /* Exit1-DR → Update-DR (TMS=1) → RTI (TMS=0): 2 bits 1,0 */
    uint8_t tms_upd_rti[1] = {0x01u};
    jtag_write_tms(tms_upd_rti, 2u);

    return (uint32_t)tdo[0]
         | ((uint32_t)tdo[1] << 8u)
         | ((uint32_t)tdo[2] << 16u)
         | ((uint32_t)tdo[3] << 24u);
}

int jtag_scan_chain(JLINKARM_JTAG_IDCODE_INFO *pInfo, int maxDev) {
    /* Reset TAP → Test-Logic-Reset → Run-Test/Idle */
    uint8_t tms_rst[1] = {0x1Fu};
    jtag_write_tms(tms_rst, 5u);
    uint8_t tms_rti[1] = {0x00u};
    jtag_write_tms(tms_rti, 1u);

    /* RTI → Shift-DR: TMS = 1,0,0  (Select-DR-Scan → Capture-DR → Shift-DR) */
    uint8_t tms_shdr[1] = {0x01u};  /* 3 bits: bit0=1, bit1=0, bit2=0 */
    jtag_write_tms(tms_shdr, 3u);

    int count = 0;
    uint8_t tdi[4] = {0xFFu, 0xFFu, 0xFFu, 0xFFu};

    while (count < maxDev) {
        uint8_t tdo[4] = {0};
        jtag_shift_data(tdi, tdo, 32u, false);   /* TAP permanece en Shift-DR */

        uint32_t id = (uint32_t)tdo[0]
                    | ((uint32_t)tdo[1] << 8u)
                    | ((uint32_t)tdo[2] << 16u)
                    | ((uint32_t)tdo[3] << 24u);

        /* Fin de cadena: IEEE 1149.1 §12.1 exige bit 0 = 1 en todo IDCODE válido.
         * 0x00000000 = dispositivo en BYPASS (sin IDCODE).
         * 0xFFFFFFFF = TDO flotando (no hay más dispositivos en la cadena). */
        if (id == 0xFFFFFFFFu || (id & 1u) == 0u)
            break;

        if (pInfo) {
            pInfo[count].Id    = id;
            pInfo[count].IRLen = 0u;   /* longitud IR no determinada aquí */
        }
        count++;
    }

    /* Salir de Shift-DR: TMS=1 (Exit1-DR) → TMS=1 (Update-DR) → TMS=0 (RTI) */
    uint8_t tms_exit[1] = {0x03u};   /* 3 bits: bit0=1, bit1=1, bit2=0 */
    jtag_write_tms(tms_exit, 3u);

    if (count == 0)
        return 0;

    /*
     * Detección de longitud IR total (IEEE 1149.1 §10.3).
     *
     * Algoritmo:
     *   1. Navegar RTI → Shift-IR.
     *   2. Precargar todos los registros IR con BYPASS (todos 1s).
     *      Máximo posible: 32 dispositivos × 32 bits IR = 1024 bits → 128 bytes.
     *   3. Desplazar un 0 seguido de 1s; contar ciclos hasta que el 0 aparece en TDO.
     *      Ese conteo = longitud IR total de la cadena.
     *   4. Salir Exit1-IR → Update-IR → RTI.
     *
     * Si hay 1 solo dispositivo: IRLen = total_ir_len.
     * Si hay N dispositivos y g_dev_list está configurado (SetDeviceList):
     *   cada g_dev_list[i].IRLen ya es correcto → solo actualizamos g_total_ir_len.
     * Si hay N dispositivos sin config: guardamos total_ir_len en g_total_ir_len,
     *   dejamos pInfo[i].IRLen = 0 (desconocido individualmente).
     */

    /* RTI → Shift-IR: TMS = 1,1,0,0 (Select-DR → Select-IR → Capture-IR → Shift-IR) */
    uint8_t tms_shir[1] = {0x03u};   /* 4 bits: bit0=1, bit1=1, bit2=0, bit3=0 */
    jtag_write_tms(tms_shir, 4u);

    /* Precargar con 1s (máximo 128 bytes = 1024 bits, más que suficiente) */
    static uint8_t ones[128];
    memset(ones, 0xFFu, sizeof(ones));
    jtag_shift_data(ones, NULL, 1024u, false);

    /* Desplazar un 0 y luego 1s; capturar hasta que el 0 aparece en TDO */
    uint32_t total_ir_len = 0u;
    bool     found_zero   = false;
    uint8_t  probe_tdi    = 0x00u;   /* primer bit = 0 (el que buscamos) */
    uint8_t  probe_tdo    = 0x00u;
    jtag_shift_data(&probe_tdi, &probe_tdo, 1u, false);   /* shift el 0 */

    for (uint32_t k = 0u; k < JTAG_MAX_IR_TOTAL_BITS; k++) {
        uint8_t one_tdi = 0x01u;
        uint8_t one_tdo = 0x00u;
        jtag_shift_data(&one_tdi, &one_tdo, 1u, false);
        total_ir_len++;
        if ((one_tdo & 1u) == 0u) {  /* el 0 que metimos aparece en TDO */
            found_zero = true;
            break;
        }
    }

    /* Salir Exit1-IR → Update-IR → RTI */
    uint8_t tms_exit_ir[1] = {0x03u};  /* 3 bits: bit0=1, bit1=1, bit2=0 */
    jtag_write_tms(tms_exit_ir, 3u);

    /* Si el 0 nunca apareció: cadena JTAG rota o sin dispositivos */
    if (!found_zero)
        return 0;

    /* Actualizar resultados */
    g_total_ir_len = total_ir_len;

    if (count == 1 && pInfo) {
        /* Un único dispositivo: su IRLen es el total */
        pInfo[0].IRLen = total_ir_len;
    } else if (g_dev_count == count) {
        /* SetDeviceList fue llamado con la configuración correcta:
         * recalcular g_total_ir_len desde los valores almacenados. */
        g_total_ir_len = 0u;
        for (int i = 0; i < g_dev_count; i++)
            g_total_ir_len += (uint32_t)g_dev_list[i].IRLen;
        /* Copiar IRLen al resultado si disponible */
        if (pInfo) {
            for (int i = 0; i < count && i < g_dev_count; i++)
                pInfo[i].IRLen = g_dev_list[i].IRLen;
        }
    }
    /* Si count > 1 y g_dev_count != count: IRLen individuales desconocidos (quedan 0). */

    return count;
}

/* ---------------------------------------------------------------------- */
/*  GetU8 / GetU16 / GetU32                                               */
/* ---------------------------------------------------------------------- */

uint8_t jtag_get_u8(void) {
    uint8_t tdi[1] = {0xFFu};
    uint8_t tdo[1] = {0};
    jtag_shift_data(tdi, tdo, 8u, false);
    return tdo[0];
}

uint16_t jtag_get_u16(void) {
    uint8_t tdi[2] = {0xFFu, 0xFFu};
    uint8_t tdo[2] = {0};
    jtag_shift_data(tdi, tdo, 16u, false);
    return (uint16_t)tdo[0] | ((uint16_t)tdo[1] << 8u);
}

uint32_t jtag_get_u32(void) {
    uint8_t tdi[4] = {0xFFu, 0xFFu, 0xFFu, 0xFFu};
    uint8_t tdo[4] = {0};
    jtag_shift_data(tdi, tdo, 32u, false);
    return (uint32_t)tdo[0]
         | ((uint32_t)tdo[1] << 8u)
         | ((uint32_t)tdo[2] << 16u)
         | ((uint32_t)tdo[3] << 24u);
}
