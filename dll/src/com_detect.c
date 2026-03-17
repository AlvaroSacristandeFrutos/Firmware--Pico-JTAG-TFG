/*
 * com_detect.c — Detección automática del PicoAdapter.
 */

#include "com_detect.h"
#include "pico_transport.h"

#include <windows.h>
#include <string.h>
#include <stdio.h>

/* ---------------------------------------------------------------------- */
/*  Handshake                                                              */
/* ---------------------------------------------------------------------- */

/*
 * Verifica que el dispositivo en el puerto h es un PicoAdapter.
 * Envía CMD_PING y CMD_GET_VERSION; comprueba la respuesta.
 */
/* timeout_ms: usar 15 en scan (detección rápida), 500 en conexión real */
static bool do_handshake_t(HANDLE h, uint32_t timeout_ms) {
    uint8_t  resp;
    uint8_t  payload[PICO_MAX_PAYLOAD];
    uint16_t len;

    if (!pico_send(h, CMD_PING, NULL, 0))           return false;
    if (!pico_recv(h, &resp, payload, &len, timeout_ms)) return false;
    if (resp != RESP_OK)                             return false;

    if (!pico_send(h, CMD_GET_VERSION, NULL, 0))    return false;
    if (!pico_recv(h, &resp, payload, &len, timeout_ms)) return false;
    if (resp != RESP_DATA || len < 11u)             return false;
    if (strncmp((char *)payload, "PicoAdapter", 11) != 0) return false;

    return true;
}

static bool do_handshake(HANDLE h) {
    return do_handshake_t(h, 15u);   /* detección: 15 ms basta */
}


/* ---------------------------------------------------------------------- */
/*  Estrategia B — Solo puertos COM que existen (QueryDosDevice)          */
/* ---------------------------------------------------------------------- */

static bool detect_by_scan(char *out_port, size_t out_size) {
    /* QueryDosDevice(NULL) devuelve la lista de todos los dispositivos DOS.
     * Filtramos los que se llaman "COMn" — son los únicos puertos serie
     * realmente presentes, sin iterar sobre 64 posibles COM inexistentes. */
    char devices[65536];
    DWORD ret = QueryDosDeviceA(NULL, devices, sizeof(devices));
    if (ret == 0) return false;

    for (char *p = devices; *p; p += strlen(p) + 1) {
        /* Comprobar que empieza por "COM" y el resto son dígitos */
        if (_strnicmp(p, "COM", 3) != 0) continue;
        bool digits = true;
        for (char *d = p + 3; *d; d++) {
            if (*d < '0' || *d > '9') { digits = false; break; }
        }
        if (!digits || *(p + 3) == '\0') continue;

        HANDLE h = pico_port_open(p);
        if (h == INVALID_HANDLE_VALUE) continue;

        bool ok = do_handshake(h);
        pico_port_close(h);

        if (ok) {
            _snprintf_s(out_port, out_size, _TRUNCATE, "%s", p);
            return true;
        }
    }
    return false;
}

/* ---------------------------------------------------------------------- */
/*  API pública                                                            */
/* ---------------------------------------------------------------------- */

bool pico_detect(char *out_port, size_t out_size) {
    /* Intentar primero con el VID/PID real del firmware (SEGGER composite) */
    const char *keys[] = {
        "SYSTEM\\CurrentControlSet\\Enum\\USB\\VID_1366&PID_1024&MI_00",
        "SYSTEM\\CurrentControlSet\\Enum\\USB\\VID_2E8A&PID_000A",
        NULL
    };
    for (int k = 0; keys[k] != NULL; k++) {
        /* Reutilizar lógica de detect_by_vid_pid con clave variable */
        HKEY hBase;
        if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, keys[k], 0,
                          KEY_READ | KEY_ENUMERATE_SUB_KEYS, &hBase) != ERROR_SUCCESS)
            continue;

        bool found = false;
        char inst_name[256];
        DWORD name_len;
        for (DWORD idx = 0; !found; idx++) {
            name_len = sizeof(inst_name);
            if (RegEnumKeyExA(hBase, idx, inst_name, &name_len,
                              NULL, NULL, NULL, NULL) != ERROR_SUCCESS)
                break;

            char sub_path[512];
            _snprintf_s(sub_path, sizeof(sub_path), _TRUNCATE,
                        "%s\\%s\\Device Parameters", keys[k], inst_name);

            HKEY hSub;
            if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, sub_path, 0,
                              KEY_READ, &hSub) != ERROR_SUCCESS)
                continue;

            char  port_name[32];
            DWORD port_len = sizeof(port_name);
            DWORD type;
            LSTATUS st = RegQueryValueExA(hSub, "PortName", NULL, &type,
                                          (BYTE *)port_name, &port_len);
            RegCloseKey(hSub);

            if (st != ERROR_SUCCESS || type != REG_SZ)
                continue;

            HANDLE h = pico_port_open(port_name);
            if (h == INVALID_HANDLE_VALUE)
                continue;

            if (do_handshake(h)) {
                pico_port_close(h);
                _snprintf_s(out_port, out_size, _TRUNCATE, "%s", port_name);
                found = true;
            } else {
                pico_port_close(h);
            }
        }
        RegCloseKey(hBase);
        if (found) return true;
    }
    return detect_by_scan(out_port, out_size);
}
