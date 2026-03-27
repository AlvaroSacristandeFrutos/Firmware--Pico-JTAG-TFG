/*
 * com_detect.c — Detección automática del PicoAdapter.
 *
 * Estrategia A: SetupDI (no requiere admin) — enumera los puertos
 *   GUID_DEVCLASS_PORTS por VID/PID y obtiene el nombre COM del registro.
 * Estrategia B: Scan de los COM que existen (QueryDosDevice) + handshake.
 *   Solo se usa si A falla. Se usa overlapped I/O para que el timeout
 *   se respete aunque el driver del puerto no lo haga.
 */

#include "com_detect.h"
#include "pico_transport.h"

#include <windows.h>
#include <setupapi.h>
#include <string.h>
#include <stdio.h>

#pragma comment(lib, "setupapi.lib")

/* ---------------------------------------------------------------------- */
/*  Handshake                                                              */
/* ---------------------------------------------------------------------- */

static bool do_handshake_t(HANDLE h, uint32_t timeout_ms) {
    uint8_t  resp;
    uint8_t  payload[PICO_MAX_PAYLOAD];
    uint16_t len;

    if (!pico_send(h, CMD_PING, NULL, 0))                return false;
    if (!pico_recv(h, &resp, payload, &len, timeout_ms)) return false;
    if (resp != RESP_OK)                                 return false;

    if (!pico_send(h, CMD_GET_VERSION, NULL, 0))         return false;
    if (!pico_recv(h, &resp, payload, &len, timeout_ms)) return false;
    if (resp != RESP_DATA || len < 11u)                  return false;
    if (strncmp((char *)payload, "PicoAdapter", 11) != 0) return false;

    return true;
}

static bool do_handshake(HANDLE h) {
    return do_handshake_t(h, 50u);
}

/* forward declaration */
static bool try_port_overlapped(const char *port_name,
                                char *out_port, size_t out_size);

/* ---------------------------------------------------------------------- */
/*  Estrategia A — SetupDI (sin permisos de admin)                        */
/* ---------------------------------------------------------------------- */

static bool find_port_by_hwid(const char *hwid_prefix,
                               char *out_port, size_t out_size) {
    static const GUID GUID_PORTS =
        {0x4D36E978,0xE325,0x11CE,{0xBF,0xC1,0x08,0x00,0x2B,0xE1,0x03,0x18}};

    HDEVINFO hdi = SetupDiGetClassDevsA(&GUID_PORTS, NULL, NULL,
                                         DIGCF_PRESENT);
    if (hdi == INVALID_HANDLE_VALUE) return false;

    bool found = false;
    SP_DEVINFO_DATA did;
    did.cbSize = sizeof(did);

    for (DWORD i = 0; SetupDiEnumDeviceInfo(hdi, i, &did); i++) {
        char hwid[512];
        if (!SetupDiGetDeviceRegistryPropertyA(hdi, &did,
                SPDRP_HARDWAREID, NULL,
                (PBYTE)hwid, sizeof(hwid) - 1, NULL))
            continue;
        hwid[sizeof(hwid) - 1] = '\0';

        bool match = false;
        for (char *p = hwid; *p; p += strlen(p) + 1) {
            if (_strnicmp(p, hwid_prefix, strlen(hwid_prefix)) == 0) {
                match = true;
                break;
            }
        }
        if (!match) continue;

        HKEY hKey = SetupDiOpenDevRegKey(hdi, &did,
                        DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_READ);
        if (hKey == INVALID_HANDLE_VALUE) continue;

        char   port_name[32];
        DWORD  port_len = sizeof(port_name);
        DWORD  type;
        LSTATUS st = RegQueryValueExA(hKey, "PortName", NULL, &type,
                                      (BYTE *)port_name, &port_len);
        RegCloseKey(hKey);

        if (st != ERROR_SUCCESS || type != REG_SZ) continue;

        if (try_port_overlapped(port_name, out_port, out_size)) {
            found = true;
            break;
        }
    }

    SetupDiDestroyDeviceInfoList(hdi);
    return found;
}

/* ---------------------------------------------------------------------- */
/*  Estrategia B — Scan con overlapped I/O (timeout garantizado)          */
/* ---------------------------------------------------------------------- */

static bool try_port_overlapped(const char *port_name,
                                char *out_port, size_t out_size) {
    char path[64];
    _snprintf_s(path, sizeof(path), _TRUNCATE, "\\\\.\\%s", port_name);

    HANDLE h = CreateFileA(path,
                           GENERIC_READ | GENERIC_WRITE,
                           0, NULL, OPEN_EXISTING,
                           FILE_FLAG_OVERLAPPED,
                           NULL);
    if (h == INVALID_HANDLE_VALUE) return false;

    /* NO llamar a GetCommState/SetCommState: envían SET_LINE_CODING y
     * SET_CONTROL_LINE_STATE al firmware CDC que bloquean 30 s si el
     * dispositivo no responde al primer intento (comportamiento del
     * driver usbser.sys de Windows con CDC ACM).
     * Para detección basta con purgar y enviar PING directamente. */
    COMMTIMEOUTS ct = {0};
    SetCommTimeouts(h, &ct);
    PurgeComm(h, PURGE_RXCLEAR | PURGE_TXCLEAR);

    /* Construir paquete PING: [0xA5][CMD_PING][0x00][0x00][CRC8] */
    uint8_t ping[5];
    ping[0] = 0xA5u;
    ping[1] = CMD_PING;
    ping[2] = 0x00u;
    ping[3] = 0x00u;
    uint8_t crc = 0u;
    for (int i = 0; i < 4; i++) {
        crc ^= ping[i];
        for (int b = 0; b < 8; b++)
            crc = (crc & 0x80u) ? (uint8_t)((crc << 1u) ^ 0x07u)
                                 : (uint8_t)(crc << 1u);
    }
    ping[4] = crc;

    OVERLAPPED ov;
    memset(&ov, 0, sizeof(ov));
    ov.hEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
    if (!ov.hEvent) { CloseHandle(h); return false; }

    bool ok = false;
    DWORD written = 0;
    if (WriteFile(h, ping, sizeof(ping), &written, &ov) ||
        GetLastError() == ERROR_IO_PENDING) {
        WaitForSingleObject(ov.hEvent, 100u);
        GetOverlappedResult(h, &ov, &written, FALSE);
    }

    if (written == sizeof(ping)) {
        ResetEvent(ov.hEvent);
        uint8_t resp[5];
        DWORD got = 0;
        if (ReadFile(h, resp, sizeof(resp), &got, &ov) ||
            GetLastError() == ERROR_IO_PENDING) {
            if (WaitForSingleObject(ov.hEvent, 50u) == WAIT_OBJECT_0) {
                GetOverlappedResult(h, &ov, &got, FALSE);
                if (got >= 2 && resp[0] == 0xA5u && resp[1] == 0x80u) {
                    _snprintf_s(out_port, out_size, _TRUNCATE, "%s", port_name);
                    ok = true;
                }
            } else {
                CancelIo(h);
            }
        }
    }

    CloseHandle(ov.hEvent);
    CloseHandle(h);
    return ok;
}

static bool detect_by_scan(char *out_port, size_t out_size) {
    char devices[65536];
    if (QueryDosDeviceA(NULL, devices, sizeof(devices)) == 0) return false;

    for (char *p = devices; *p; p += strlen(p) + 1) {
        if (_strnicmp(p, "COM", 3) != 0) continue;
        bool digits = true;
        for (char *d = p + 3; *d; d++)
            if (*d < '0' || *d > '9') { digits = false; break; }
        if (!digits || *(p + 3) == '\0') continue;

        if (try_port_overlapped(p, out_port, out_size))
            return true;
    }
    return false;
}

/* ---------------------------------------------------------------------- */
/*  API pública                                                            */
/* ---------------------------------------------------------------------- */

bool pico_detect(char *out_port, size_t out_size) {
    if (find_port_by_hwid("USB\\VID_1366&PID_1024&MI_00", out_port, out_size))
        return true;
    if (find_port_by_hwid("USB\\VID_2E8A&PID_000A&MI_00", out_port, out_size))
        return true;
    return detect_by_scan(out_port, out_size);
}
