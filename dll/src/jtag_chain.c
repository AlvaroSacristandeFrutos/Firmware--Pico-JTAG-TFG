/*
 * jtag_chain.c — Operaciones JTAG de alto nivel sobre pico_transact().
 */

#include "jtag_chain.h"
#include "pico_transport.h"
#include "dll_state.h"

#include <stdlib.h>
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

    /* Si hay bits TMS=1, usamos NAV_ONLY (TDI no importa para la DLL
     * en navegación pura).  El clasificador más riguroso requeriría
     * comparar bit a bit TDI vs TMS; para nuestro caso de uso (boundary
     * scan JTAG estándar) NAV_ONLY es correcto casi siempre. */
    return TMS_NAV_ONLY;
}

/* ---------------------------------------------------------------------- */
/*  Primitivas de transferencia                                            */
/* ---------------------------------------------------------------------- */

bool jtag_shift_data(const uint8_t *pTDI, uint8_t *pTDO,
                     uint32_t numBits, bool exit) {
    if (numBits == 0u)
        return true;

    uint32_t num_bytes  = (numBits + 7u) / 8u;
    uint16_t payload_len = (uint16_t)(5u + num_bytes);

    /* Payload: [numBits:u32 LE][exit:u8][TDI bytes] */
    uint8_t *payload = (uint8_t *)malloc(payload_len);
    if (!payload)
        return false;

    payload[0] = (uint8_t)( numBits        & 0xFFu);
    payload[1] = (uint8_t)((numBits >>  8) & 0xFFu);
    payload[2] = (uint8_t)((numBits >> 16) & 0xFFu);
    payload[3] = (uint8_t)((numBits >> 24) & 0xFFu);
    payload[4] = exit ? 1u : 0u;
    memcpy(payload + 5u, pTDI, num_bytes);

    uint8_t  rx_buf[PICO_MAX_PAYLOAD];
    uint16_t rx_len = 0u;
    bool ok = pico_transact(g_hCOM, CMD_SHIFT_DATA,
                            payload, payload_len,
                            pTDO ? rx_buf : NULL, &rx_len,
                            2000u);
    free(payload);

    if (ok && pTDO && rx_len == (uint16_t)num_bytes)
        memcpy(pTDO, rx_buf, num_bytes);

    return ok;
}

bool jtag_write_tms(const uint8_t *pTMS, uint32_t numBits) {
    if (numBits == 0u)
        return true;

    uint32_t num_bytes  = (numBits + 7u) / 8u;
    uint16_t payload_len = (uint16_t)(2u + num_bytes);

    /* Payload: [numBits:u16 LE][TMS bytes] */
    uint8_t *payload = (uint8_t *)malloc(payload_len);
    if (!payload)
        return false;

    payload[0] = (uint8_t)(numBits & 0xFFu);
    payload[1] = (uint8_t)(numBits >> 8u);
    memcpy(payload + 2u, pTMS, num_bytes);

    bool ok = pico_transact(g_hCOM, CMD_WRITE_TMS,
                            payload, payload_len,
                            NULL, NULL, 2000u);
    free(payload);
    return ok;
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

    /* Shift-DR: capturar 32 bits con TDI=0xFF, TMS=0 (no exit) */
    uint8_t tdi[4] = {0xFFu, 0xFFu, 0xFFu, 0xFFu};
    uint8_t tdo[4] = {0};
    jtag_shift_data(tdi, tdo, 32u, false);

    return (uint32_t)tdo[0]
         | ((uint32_t)tdo[1] << 8u)
         | ((uint32_t)tdo[2] << 16u)
         | ((uint32_t)tdo[3] << 24u);
}

int jtag_scan_chain(JLINKARM_JTAG_IDCODE_INFO *pInfo, int maxDev) {
    /* Reset TAP */
    uint8_t tms_rst[1] = {0x1Fu};
    jtag_write_tms(tms_rst, 5u);
    uint8_t tms_rti[1] = {0x00u};
    jtag_write_tms(tms_rti, 1u);

    int count = 0;
    uint8_t tdi[4] = {0xFFu, 0xFFu, 0xFFu, 0xFFu};

    while (count < maxDev) {
        uint8_t tdo[4] = {0};
        jtag_shift_data(tdi, tdo, 32u, false);

        uint32_t id = (uint32_t)tdo[0]
                    | ((uint32_t)tdo[1] << 8u)
                    | ((uint32_t)tdo[2] << 16u)
                    | ((uint32_t)tdo[3] << 24u);

        /* 0xFFFFFFFF indica BYPASS (no hay más dispositivos) */
        if (id == 0xFFFFFFFFu)
            break;

        if (pInfo) {
            pInfo[count].Id    = id;
            pInfo[count].IRLen = 0u;   /* no determinado aquí */
        }
        count++;
    }
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
