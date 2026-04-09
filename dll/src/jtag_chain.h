#pragma once
/*
 * jtag_chain.h — Operaciones JTAG de alto nivel construidas sobre pico_transact().
 *
 * Estas funciones implementan la lógica de cadena JTAG que las funciones
 * JLINKARM_JTAG_* requieren.  No son traducciones 1:1 de un comando firmware;
 * se encargan de clasificar TMS/TDI y elegir el comando óptimo.
 */

#include <stdint.h>
#include <stdbool.h>
#include "jlink_types.h"

/* ---- Clasificación de vectores TMS ---- */
typedef enum {
    TMS_ALL_ZERO,   /* Todos los bits de TMS son 0 → CMD_SHIFT_DATA (exit=false) */
    TMS_NAV_ONLY,   /* N-1 bits TMS=0 + último bit TMS=1 → CMD_SHIFT_DATA (exit=true) */
    TMS_MIXED,      /* Cualquier otro patrón de TMS → bitbang bit a bit */
} tms_class_t;

/*
 * Clasifica el vector TMS de numBits bits.
 *
 * TMS_ALL_ZERO  → TMS[0..N-1] todos 0.
 * TMS_NAV_ONLY  → exactamente los N-1 primeros bits son 0 y el último es 1.
 *                 Este patrón cubre el caso más habitual: desplazamiento de datos
 *                 seguido de transición a Exit1-DR/IR en el último bit.
 * TMS_MIXED     → cualquier otro patrón (TMS=1 en posiciones intermedias) →
 *                 requiere bitbang bit a bit para mantener TDI correcto.
 */
tms_class_t classify_tms(const uint8_t *pTMS, uint32_t numBits);

/*
 * Envía CMD_SHIFT_DATA al firmware.
 *   pTDI     — bits a desplazar (LSB first, ceil(numBits/8) bytes)
 *   pTDO     — buffer de salida; NULL si no interesa TDO
 *   numBits  — número de bits a transferir
 *   exit     — si true, TMS sube en el último bit (Exit1-DR/IR)
 */
bool jtag_shift_data(const uint8_t *pTDI, uint8_t *pTDO,
                     uint32_t numBits, bool exit);

/*
 * Envía CMD_WRITE_TMS al firmware.
 *   pTMS    — bits de TMS (LSB first, ceil(numBits/8) bytes)
 *   numBits — número de bits de TMS a generar
 */
bool jtag_write_tms(const uint8_t *pTMS, uint32_t numBits);

/*
 * Modo bit-a-bit para TMS_MIXED: alterna CMD_WRITE_TMS(1 bit) + CMD_SHIFT_DATA(1 bit).
 * pTDO puede ser NULL.
 */
bool jtag_store_raw_bitbang(const uint8_t *pTDI, uint8_t *pTDO,
                             const uint8_t *pTMS, uint32_t numBits);

/*
 * Lee el IDCODE del primer dispositivo de la cadena:
 *   Reset TAP → shift 32 bits con TDI=0xFF, TMS=0 → devuelve word TDO.
 */
uint32_t jtag_read_idcode(void);

/*
 * Escanea la cadena JTAG:
 *   Reset TAP → shift 32 bits sucesivos hasta leer 0xFFFFFFFF (BYPASS).
 *   Rellena pInfo[i].Id con cada IDCODE.
 *   Devuelve el número de dispositivos encontrados.
 */
int jtag_scan_chain(JLINKARM_JTAG_IDCODE_INFO *pInfo, int maxDev);

/* Lectura de N bits desde la DR actual (TMS=0, TDI=0xFF) */
uint8_t  jtag_get_u8(void);
uint16_t jtag_get_u16(void);
uint32_t jtag_get_u32(void);
