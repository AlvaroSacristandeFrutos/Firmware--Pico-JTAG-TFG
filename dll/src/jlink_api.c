/*
 * jlink_api.c — Implementación de las 41 funciones exportadas compatibles
 *               con JLinkARM.dll / JLink_x64.dll de SEGGER.
 *
 * La DLL actúa como reemplazo drop-in: cualquier software que cargue la DLL
 * de SEGGER puede usar el PicoAdapter sin modificaciones, simplemente
 * sustituyendo el archivo DLL.
 *
 * Protocolo interno: PicoAdapter sobre USB-CDC (puerto COM virtual).
 * VID/PID detectados: 0x2E8A / 0x000A (Raspberry Pi Pico).
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "jlink_types.h"
#include "dll_state.h"
#include "pico_transport.h"
#include "com_detect.h"
#include "jtag_chain.h"

/* ======================================================================
 * Definiciones de variables globales (declaradas extern en dll_state.h)
 * ====================================================================== */

HANDLE   g_hCOM           = INVALID_HANDLE_VALUE;
int      g_is_open        = 0;
uint32_t g_serial         = 0u;

/* Caché de detección: evita doble scan cuando GetList se llama dos veces
 * seguidas (PicoAdapter + JLinkAdapter) en el mismo ciclo de factory scan. */
static char     s_cached_port[32]  = {0};
static DWORD    s_cache_tick       = 0u;
static uint32_t s_cache_serial     = 0u;
static int      s_cache_found      = -1;   /* -1 = inválido */
#define CACHE_TTL_MS 3000u
uint32_t g_speed_khz      = 4000u;
uint32_t g_reset_delay_ms = 10u;
int      g_reset_type     = 0;
int      g_jtag_ir_len    = 0;
int      g_jtag_dev_pos   = 0;

JLINKARM_JTAG_DEVICE_CONF g_dev_list[32];
int                        g_dev_count    = 0;
uint32_t                   g_total_ir_len = 0u;

/* Estado TAP rastreado desde la DLL — se actualiza en cada StoreRaw/StoreGetRaw */
tap_state_t g_tap_state = TAP_RESET;

JLINKARM_LOG_FUNC *g_warn_cb  = NULL;
JLINKARM_LOG_FUNC *g_error_cb = NULL;
FILE               *g_log_fp  = NULL;

/* ======================================================================
 * DllMain
 * ====================================================================== */

/* Declaración forward para JLINKARM_Close */
void __cdecl JLINKARM_Close(void);

BOOL WINAPI DllMain(HINSTANCE hInst, DWORD reason, LPVOID reserved) {
    (void)hInst; (void)reserved;

    if (reason == DLL_PROCESS_ATTACH) {
        g_hCOM           = INVALID_HANDLE_VALUE;
        g_is_open        = 0;
        g_speed_khz      = 4000u;
        g_reset_delay_ms = 10u;
        g_reset_type     = 0;
        g_dev_count      = 0;
        g_warn_cb        = NULL;
        g_error_cb       = NULL;
        g_log_fp         = NULL;
    }
    if (reason == DLL_PROCESS_DETACH && g_is_open)
        JLINKARM_Close();

    return TRUE;
}

/* ======================================================================
 * Grupo 1: Conexión y enumeración
 * ====================================================================== */

const char * __cdecl JLINKARM_OpenEx(const char *log, void *reserved) {
    (void)log; (void)reserved;

    if (g_is_open)
        return NULL;   /* ya abierto */

    char port[32];
    if (!pico_detect(port, sizeof(port)))
        return "PicoAdapter not found";

    g_hCOM = pico_port_open(port);
    if (g_hCOM == INVALID_HANDLE_VALUE)
        return "Cannot open COM port";

    /* Leer número de serie a partir del string de versión del firmware.
     * Usamos los primeros 9 dígitos del hash FNV-1a del nombre del puerto
     * como identificador único reproducible. */
    {
        uint8_t  rx[PICO_MAX_PAYLOAD];
        uint16_t rx_len = 0u;
        if (pico_transact(g_hCOM, CMD_GET_VERSION, NULL, 0u, rx, &rx_len, 500u)) {
            /* Derivar serial: hash simple del string recibido */
            uint32_t h = 2166136261u;
            for (uint16_t i = 0u; i < rx_len; i++) {
                h ^= rx[i];
                h *= 16777619u;
            }
            g_serial = h % 1000000000u;
        }
    }

    g_is_open = 1;
    return NULL;   /* NULL = éxito, igual que SEGGER */
}

const char * __cdecl JLINKARM_Open(void) {
    return JLINKARM_OpenEx(NULL, NULL);
}

void __cdecl JLINKARM_Close(void) {
    if (g_hCOM != INVALID_HANDLE_VALUE) {
        pico_port_close(g_hCOM);
        g_hCOM = INVALID_HANDLE_VALUE;
    }
    g_is_open = 0;
}

int __cdecl JLINKARM_IsOpen(void) {
    return g_is_open;
}

uint32_t __cdecl JLINKARM_GetSN(void) {
    return g_serial;
}

uint32_t __cdecl JLINKARM_EMU_GetList(uint32_t mask,
                                       JLINKARM_EMU_INFO *pInfo,
                                       uint32_t max) {
    (void)mask;

    char     port[32];
    uint32_t serial;
    int      found;

    if (g_is_open) {
        /* Conexión ya abierta — dispositivo disponible */
        found  = 1;
        serial = g_serial;
        strncpy(port, s_cached_port, sizeof(port));
    } else {
        /* Usar caché si es reciente */
        DWORD now = GetTickCount();
        if (s_cache_found >= 0 && (now - s_cache_tick) < CACHE_TTL_MS) {
            found  = s_cache_found;
            serial = s_cache_serial;
            strncpy(port, s_cached_port, sizeof(port));
        } else {
            /* Detección real */
            found = pico_detect(port, sizeof(port)) ? 1 : 0;

            /* Si encontramos el puerto, leer el S/N via GET_VERSION */
            if (found && s_cache_serial == 0u) {
                HANDLE h = pico_port_open(port);
                if (h != INVALID_HANDLE_VALUE) {
                    uint8_t  rx[256];
                    uint16_t rx_len = 0u;
                    if (pico_transact(h, CMD_GET_VERSION, NULL, 0u,
                                      rx, &rx_len, 500u)) {
                        uint32_t hv = 2166136261u;
                        for (uint16_t i = 0u; i < rx_len; i++) {
                            hv ^= rx[i];
                            hv *= 16777619u;
                        }
                        serial = hv % 1000000000u;
                    } else {
                        serial = 0u;
                    }
                    pico_port_close(h);
                } else {
                    serial = 0u;
                }
            } else {
                serial = s_cache_serial;
            }

            /* Actualizar caché */
            s_cache_found  = found;
            s_cache_serial = serial;
            s_cache_tick   = now;
            if (found) strncpy(s_cached_port, port, sizeof(s_cached_port));
        }
    }

    if (!found)
        return 0u;

    if (max == 0u || pInfo == NULL)
        return 1u;   /* solo count */

    /* Rellenar el primer slot */
    memset(&pInfo[0], 0, sizeof(JLINKARM_EMU_INFO));
    pInfo[0].SerialNumber = serial ? serial : 1u;
    pInfo[0].Connection   = 0u;   /* USB */
    strncpy(pInfo[0].acProduct, "PicoAdapter", sizeof(pInfo[0].acProduct) - 1);
    strncpy(pInfo[0].acFWString, "PicoAdapter v1.0", sizeof(pInfo[0].acFWString) - 1);

    /* Propagar el serial a g_serial si aún no está definido */
    if (g_serial == 0u) g_serial = serial;

    return 1u;
}

int __cdecl JLINKARM_EMU_SelectByUSBSN(uint32_t sn) {
    g_serial = sn;
    return 0;
}

int __cdecl JLINKARM_EMU_GetNumConnections(void) {
    return (int)JLINKARM_EMU_GetList(0u, NULL, 0u);
}

/* ======================================================================
 * Grupo 2: Interfaz y velocidad
 * ====================================================================== */

int __cdecl JLINKARM_TIF_Select(int iface) {
    if (!g_is_open) return -1;
    uint8_t  p   = (uint8_t)iface;
    bool     ok  = pico_transact(g_hCOM, CMD_SELECT_IF, &p, 1u,
                                 NULL, NULL, 500u);
    return ok ? 0 : -1;
}

void __cdecl JLINKARM_TIF_GetAvailable(uint32_t *pMask) {
    if (pMask) *pMask = 0x01u;   /* bit 0 = JTAG */
}

void __cdecl JLINKARM_SetSpeed(uint32_t khz) {
    if (!g_is_open) return;
    uint32_t hz = khz * 1000u;
    uint8_t  p[4] = {
        (uint8_t)( hz        & 0xFFu),
        (uint8_t)((hz >>  8) & 0xFFu),
        (uint8_t)((hz >> 16) & 0xFFu),
        (uint8_t)((hz >> 24) & 0xFFu)
    };
    pico_transact(g_hCOM, CMD_SET_CLOCK, p, 4u, NULL, NULL, 500u);
    g_speed_khz = khz;
}

uint32_t __cdecl JLINKARM_GetSpeed(void) {
    if (!g_is_open) return g_speed_khz;
    uint8_t  rx[4];
    uint16_t rx_len = 0u;
    if (!pico_transact(g_hCOM, CMD_GET_CLOCK, NULL, 0u, rx, &rx_len, 500u)
        || rx_len < 4u)
        return g_speed_khz;
    return (uint32_t)rx[0]
         | ((uint32_t)rx[1] << 8u)
         | ((uint32_t)rx[2] << 16u)
         | ((uint32_t)rx[3] << 24u);
}

/* ======================================================================
 * Grupo 3: Reset y control de señales
 * ====================================================================== */

void __cdecl JLINKARM_Reset(void) {
    if (!g_is_open) return;
    pico_transact(g_hCOM, CMD_RESET_TAP, NULL, 0u, NULL, NULL, 500u);
    /* CMD_RESET_TAP envía 5×TMS=1 + 1×TMS=0 → TAP queda en RTI */
    tap_track_reset();
    g_tap_state = TAP_IDLE;
}

void __cdecl JLINKARM_ResetTarget(void) {
    if (!g_is_open) return;
    uint8_t p[2] = {
        (uint8_t)(g_reset_delay_ms & 0xFFu),
        (uint8_t)(g_reset_delay_ms >> 8u)
    };
    pico_transact(g_hCOM, CMD_RESET_HARD, p, 2u, NULL, NULL, 2000u);
}

void __cdecl JLINKARM_SetTRST(int level) {
    if (!g_is_open) return;
    uint8_t p = level ? 1u : 0u;
    pico_transact(g_hCOM, CMD_SET_TRST, &p, 1u, NULL, NULL, 500u);
}

int __cdecl JLINKARM_SetResetType(int type) {
    g_reset_type = type;
    return 0;
}

void __cdecl JLINKARM_SetResetDelay(int ms) {
    g_reset_delay_ms = (uint32_t)(ms > 0 ? ms : 0);
}

/* ======================================================================
 * Grupo 4: JTAG raw — núcleo
 * ====================================================================== */

int __cdecl JLINKARM_JTAG_StoreRaw(const uint8_t *pTDI,
                                    const uint8_t *pTMS,
                                    uint32_t       numBits) {
    if (!g_is_open) return -1;
    int ret;

    switch (classify_tms(pTMS, numBits)) {
    case TMS_ALL_ZERO:
        ret = jtag_shift_data(pTDI, NULL, numBits, false) ? 0 : -1;
        break;
    case TMS_NAV_ONLY: {
        /* Caso simple: N-1 bits TMS=0 + último bit TMS=1.
         * Usar shift_data(exit=true) para preservar TDI en todos los bits. */
        uint32_t last = numBits - 1u;
        bool exit_bit = (bool)((pTMS[last >> 3u] >> (last & 7u)) & 1u);
        ret = jtag_shift_data(pTDI, NULL, numBits, exit_bit) ? 0 : -1;
        break;
    }
    case TMS_MIXED:
    default:
        /* TMS mixto real: bitbang bit a bit para preservar TDI correctamente.
         * Más lento (1 round-trip por bit) pero necesario para mantener TDI válido
         * cuando hay cambios de TMS intercalados con datos (p.ej. EXTEST sobre IR). */
        ret = jtag_store_raw_bitbang(pTDI, NULL, pTMS, numBits) ? 0 : -1;
        break;
    }

    /* Actualizar el estado TAP rastreado */
    tap_track_tms(pTMS, numBits);
    g_tap_state = tap_track_state();
    return ret;
}

/*
 * Buffers estáticos para bypass padding en StoreGetRaw multi-dispositivo.
 * 4096 bytes = suficiente para cualquier cadena de hasta 32 dispositivos.
 */
static uint8_t s_pad_tdi[PICO_MAX_PAYLOAD];
static uint8_t s_pad_tdo[PICO_MAX_PAYLOAD];

/*
 * Copia numBits bits desde src_buf[src_off..] hacia dst_buf[dst_off..].
 * Los bits restantes de dst_buf deben estar pre-inicializados por el llamador.
 */
static void bits_copy(uint8_t *dst_buf, uint32_t dst_off,
                      const uint8_t *src_buf, uint32_t src_off,
                      uint32_t nbits) {
    for (uint32_t i = 0u; i < nbits; i++) {
        uint32_t si = src_off + i;
        uint32_t di = dst_off + i;
        uint8_t  b  = (src_buf[si >> 3u] >> (si & 7u)) & 1u;
        if (b)
            dst_buf[di >> 3u] |=  (uint8_t)(1u << (di & 7u));
        else
            dst_buf[di >> 3u] &= ~(uint8_t)(1u << (di & 7u));
    }
}

int __cdecl JLINKARM_JTAG_StoreGetRaw(const uint8_t *pTDI,
                                       uint8_t       *pTDO,
                                       const uint8_t *pTMS,
                                       uint32_t       numBits) {
    if (!g_is_open) return -1;
    if (numBits == 0u) return 0;

    int ret;
    tap_state_t cur = tap_track_state();
    bool apply_bypass = (g_dev_count > 1) &&
                        (cur == TAP_SHIFT_DR || cur == TAP_SHIFT_IR);

    if (apply_bypass) {
        /*
         * Bypass padding transparente para multi-dispositivo.
         *
         * Cadena: TDI → Dev0 → Dev1 → … → DevN-1 → TDO
         * Target en posición g_jtag_dev_pos (0-indexed desde TDI).
         *
         * Shift-DR: cada dispositivo en BYPASS aporta 1 bit de DR.
         *   pre_bits  = g_jtag_dev_pos        (devs antes, más cerca de TDI)
         *   post_bits = N - pos - 1           (devs después, más cerca de TDO)
         *
         * Shift-IR: cada dispositivo en BYPASS aporta IRLen[i] bits de IR.
         *   pre_bits  = sum(IRLen[0..pos-1])  (rellenamos con 1s = BYPASS instruction)
         *   post_bits = sum(IRLen[pos+1..N-1])
         *
         * El exit bit (TMS=1 en el último bit) se mueve al último bit de la
         * secuencia paddeada (total_bits-1), no al último bit del target.
         * Esto garantiza que el TAP no sale de Shift antes de desplazar los
         * bypass bits post-target.
         */
        int pos = g_jtag_dev_pos;
        if (pos < 0 || pos >= g_dev_count) pos = 0;

        uint32_t pre_bits  = 0u;
        uint32_t post_bits = 0u;
        bool     pad_ones  = (cur == TAP_SHIFT_IR);  /* BYPASS IR = todos 1s */

        if (cur == TAP_SHIFT_DR) {
            pre_bits  = (uint32_t)pos;
            post_bits = (uint32_t)(g_dev_count - pos - 1);
        } else {
            for (int i = 0; i < pos; i++)
                pre_bits  += (uint32_t)g_dev_list[i].IRLen;
            for (int i = pos + 1; i < g_dev_count; i++)
                post_bits += (uint32_t)g_dev_list[i].IRLen;
        }

        uint32_t total_bits  = pre_bits + numBits + post_bits;
        uint32_t total_bytes = (total_bits + 7u) / 8u;

        if (total_bytes <= (uint32_t)sizeof(s_pad_tdi)) {
            /* ---- Construir TDI padded ----
             * pre_bits  → relleno (0s para DR, 1s para IR BYPASS)
             * numBits   → datos reales del target
             * post_bits → relleno (0s para DR, 1s para IR BYPASS) */
            memset(s_pad_tdi, pad_ones ? 0xFFu : 0x00u, total_bytes);
            bits_copy(s_pad_tdi, pre_bits, pTDI, 0u, numBits);

            memset(s_pad_tdo, 0, total_bytes);

            /* ---- Determinar si la transferencia original tiene exit ----
             * Para el padded transfer: el exit se coloca en el último bit TOTAL,
             * no en el último bit del target, para desplazar también los bypass post. */
            tms_class_t cls = classify_tms(pTMS, numBits);
            bool exit_bit = false;
            if (cls == TMS_NAV_ONLY) {
                uint32_t last = numBits - 1u;
                exit_bit = (bool)((pTMS[last >> 3u] >> (last & 7u)) & 1u);
            }

            /* ---- Shift del total paddeado ---- */
            ret = jtag_shift_data(s_pad_tdi, s_pad_tdo, total_bits, exit_bit) ? 0 : -1;

            /* ---- Extraer bits del target desde TDO paddeado ---- */
            if (ret == 0 && pTDO) {
                uint32_t out_bytes = (numBits + 7u) / 8u;
                memset(pTDO, 0, out_bytes);
                bits_copy(pTDO, 0u, s_pad_tdo, pre_bits, numBits);
            }

            /* ---- Actualizar TAP tracker con TMS equivalente ----
             * El TMS padded es (total_bits-1) ceros + exit_bit. */
            static uint8_t s_zero_tms[PICO_MAX_PAYLOAD] = {0};
            tap_track_tms(s_zero_tms, exit_bit ? total_bits - 1u : total_bits);
            if (exit_bit) {
                uint8_t one = 0x01u;
                tap_track_tms(&one, 1u);
            }
            g_tap_state = tap_track_state();
            return ret;
        }
        /* Cadena demasiado larga para el buffer → caer al path normal */
    }

    /* ---- Path normal (single-device o bypass desactivado) ---- */
    switch (classify_tms(pTMS, numBits)) {
    case TMS_ALL_ZERO:
        ret = jtag_shift_data(pTDI, pTDO, numBits, false) ? 0 : -1;
        break;

    case TMS_NAV_ONLY: {
        uint32_t last    = numBits - 1u;
        bool exit_bit    = (bool)((pTMS[last >> 3u] >> (last & 7u)) & 1u);
        bool simple      = true;
        for (uint32_t i = 0u; i < last && simple; i++) {
            if ((pTMS[i >> 3u] >> (i & 7u)) & 1u) simple = false;
        }
        ret = simple
            ? (jtag_shift_data(pTDI, pTDO, numBits, exit_bit) ? 0 : -1)
            : (jtag_store_raw_bitbang(pTDI, pTDO, pTMS, numBits) ? 0 : -1);
        break;
    }

    case TMS_MIXED:
    default:
        ret = jtag_store_raw_bitbang(pTDI, pTDO, pTMS, numBits) ? 0 : -1;
        break;
    }

    tap_track_tms(pTMS, numBits);
    g_tap_state = tap_track_state();
    return ret;
}

void __cdecl JLINKARM_JTAG_SyncBits(void) {
    /* no-op: las transferencias ya son síncronas */
}

uint8_t __cdecl JLINKARM_JTAG_GetU8(void) {
    if (!g_is_open) return 0u;
    return jtag_get_u8();
}

uint16_t __cdecl JLINKARM_JTAG_GetU16(void) {
    if (!g_is_open) return 0u;
    return jtag_get_u16();
}

uint32_t __cdecl JLINKARM_JTAG_GetU32(void) {
    if (!g_is_open) return 0u;
    return jtag_get_u32();
}

void __cdecl JLINKARM_JTAG_SendNBytes(int n, const uint8_t *pData) {
    if (!g_is_open || n <= 0) return;
    static const uint8_t zeros[PICO_MAX_PAYLOAD / 8 + 1];
    (void)zeros;
    jtag_shift_data(pData, NULL, (uint32_t)(n * 8), false);
}

uint32_t __cdecl JLINKARM_JTAG_GetId(void) {
    if (!g_is_open) return 0u;
    return jtag_read_idcode();
}

/* ======================================================================
 * Grupo 5: Información de hardware / firmware
 * ====================================================================== */

void __cdecl JLINKARM_GetFirmwareString(char *pBuf, int bufSize) {
    if (!g_is_open || !pBuf || bufSize <= 0) return;
    uint8_t  rx[PICO_MAX_PAYLOAD];
    uint16_t rx_len = 0u;
    if (pico_transact(g_hCOM, CMD_GET_VERSION, NULL, 0u, rx, &rx_len, 500u)
        && rx_len > 0u) {
        int copy = (rx_len < (uint16_t)(bufSize - 1)) ? rx_len : bufSize - 1;
        memcpy(pBuf, rx, copy);
        pBuf[copy] = '\0';
    } else {
        pBuf[0] = '\0';
    }
}

uint32_t __cdecl JLINKARM_GetHWVersion(void) {
    if (!g_is_open) return 0u;
    uint8_t  rx[4];
    uint16_t rx_len = 0u;
    if (!pico_transact(g_hCOM, CMD_GET_HW_VERSION, NULL, 0u, rx, &rx_len, 500u)
        || rx_len < 4u)
        return 0u;
    return (uint32_t)rx[0]
         | ((uint32_t)rx[1] << 8u)
         | ((uint32_t)rx[2] << 16u)
         | ((uint32_t)rx[3] << 24u);
}

int __cdecl JLINKARM_EMU_HasCapEx(int cap) {
    return (cap == 0) ? 1 : 0;   /* cap 0 = JTAG básico */
}

void __cdecl JLINKARM_GetFeatureString(char *pBuf) {
    if (pBuf) pBuf[0] = '\0';   /* sin licencias SEGGER */
}

void __cdecl JLINKARM_GetOEMString(char *pBuf) {
    if (pBuf) strncpy_s(pBuf, 32, "PicoAdapter", _TRUNCATE);
}

/* ======================================================================
 * Grupo 6: Tensión y estado del target
 * ====================================================================== */

int __cdecl JLINKARM_GetVRefMV(void) {
    if (!g_is_open) return 0;
    uint8_t  rx[2];
    uint16_t rx_len = 0u;
    if (!pico_transact(g_hCOM, CMD_READ_VREF, NULL, 0u, rx, &rx_len, 500u)
        || rx_len < 2u)
        return 0;
    return (int)((uint16_t)rx[0] | ((uint16_t)rx[1] << 8u));
}

void __cdecl JLINKARM_GetStatus(JLINKARM_HW_STATUS *p) {
    if (!p) return;
    memset(p, 0, sizeof(*p));
    if (!g_is_open) return;

    uint8_t  rx[4];
    uint16_t rx_len = 0u;
    if (!pico_transact(g_hCOM, CMD_GET_STATUS, NULL, 0u, rx, &rx_len, 500u)
        || rx_len < 3u)
        return;

    /* Byte 0 = flags, bytes 1-2 = Vref mV (LE) */
    p->VTarget = (uint16_t)rx[1] | ((uint16_t)rx[2] << 8u);
}

/* ======================================================================
 * Grupo 7: Cadena de scan / configuración JTAG
 * ====================================================================== */

int __cdecl JLINKARM_JTAG_SetDeviceList(const JLINKARM_JTAG_DEVICE_CONF *pList,
                                         int n) {
    if (!pList || n <= 0) return -1;
    int count = (n > 32) ? 32 : n;
    memcpy(g_dev_list, pList, (size_t)count * sizeof(JLINKARM_JTAG_DEVICE_CONF));
    g_dev_count = count;
    /* Calcular longitud IR total de la cadena */
    g_total_ir_len = 0u;
    for (int i = 0; i < count; i++)
        g_total_ir_len += (uint32_t)g_dev_list[i].IRLen;
    return 0;
}

int __cdecl JLINKARM_JTAG_GetDeviceInfo(int devIdx,
                                         JLINKARM_JTAG_DEVICE_CONF *p) {
    if (!p || devIdx < 0 || devIdx >= g_dev_count) return -1;
    *p = g_dev_list[devIdx];
    return 0;
}

int __cdecl JLINKARM_JTAG_GetIdChain(JLINKARM_JTAG_IDCODE_INFO *pInfo,
                                      int maxDev) {
    if (!g_is_open) return 0;
    return jtag_scan_chain(pInfo, maxDev);
}

int __cdecl JLINKARM_ConfigJTAG(int irLen, int devPos) {
    g_jtag_ir_len = irLen;
    g_jtag_dev_pos = devPos;
    return 0;
}

/* ======================================================================
 * Grupo 8: Logging y callbacks
 * ====================================================================== */

void __cdecl JLINKARM_SetLogFile(const char *sFilename) {
    if (g_log_fp) { fclose(g_log_fp); g_log_fp = NULL; }
    if (sFilename && sFilename[0])
        fopen_s(&g_log_fp, sFilename, "a");
}

void __cdecl JLINKARM_SetWarnOutHandler(JLINKARM_LOG_FUNC *cb) {
    g_warn_cb = cb;
}

void __cdecl JLINKARM_SetErrorOutHandler(JLINKARM_LOG_FUNC *cb) {
    g_error_cb = cb;
}

/* ======================================================================
 * Funciones propias PicoAdapter (prefijo JLINK_PICO_)
 * ====================================================================== */

void __cdecl JLINK_PICO_SetLED(int mask) {
    if (!g_is_open) return;
    uint8_t p = (uint8_t)(mask & 0x03);
    pico_transact(g_hCOM, CMD_SET_LED, &p, 1u, NULL, NULL, 500u);
}

void __cdecl JLINK_PICO_GetVersion(char *pBuf, int bufSize) {
    JLINKARM_GetFirmwareString(pBuf, bufSize);
}

/* ======================================================================
 * UART del target (GP12=TX, GP13=RX)
 * ====================================================================== */

/*
 * PICO_UART_SetBaud — cambia la velocidad de la UART del target.
 * Llamar antes de enviar/recibir si el target no usa 115200.
 */
void __cdecl PICO_UART_SetBaud(uint32_t baud) {
    if (!g_is_open) return;
    uint8_t pl[4] = {
        (uint8_t)( baud        & 0xFFu),
        (uint8_t)((baud >>  8) & 0xFFu),
        (uint8_t)((baud >> 16) & 0xFFu),
        (uint8_t)((baud >> 24) & 0xFFu)
    };
    uint8_t  resp;
    uint8_t  buf[4];
    uint16_t rlen;
    if (pico_send(g_hCOM, CMD_UART_SET_BAUD, pl, 4u))
        pico_recv(g_hCOM, &resp, buf, &rlen, 200u);
}

/*
 * PICO_UART_Send — envía data[0..len-1] al target por UART (GP12 TX).
 * Devuelve len si OK, -1 si error.
 */
int __cdecl PICO_UART_Send(const uint8_t *data, uint16_t len) {
    if (!g_is_open || !data || len == 0u) return -1;
    if (!pico_send(g_hCOM, CMD_UART_SEND, data, len)) return -1;
    uint8_t  resp;
    uint8_t  tmp[4];
    uint16_t rlen;
    if (!pico_recv(g_hCOM, &resp, tmp, &rlen, 500u)) return -1;
    return (resp == RESP_OK) ? (int)len : -1;
}

/*
 * PICO_UART_Recv — lee bytes pendientes del buffer RX UART (GP13 RX).
 * Copia hasta max_len bytes en buf.
 * Devuelve el número de bytes copiados (puede ser 0), o -1 si error.
 */
int __cdecl PICO_UART_Recv(uint8_t *buf, uint16_t max_len) {
    if (!g_is_open || !buf) return -1;
    if (!pico_send(g_hCOM, CMD_UART_RECV, NULL, 0u)) return -1;
    uint8_t  resp;
    uint16_t rlen = 0u;
    if (!pico_recv(g_hCOM, &resp, buf, &rlen, 200u)) return -1;
    if (rlen > max_len) rlen = max_len;
    return (int)rlen;
}
