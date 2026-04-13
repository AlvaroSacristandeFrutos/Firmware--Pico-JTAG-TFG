/*
 * pico_transport.c — Capa de comunicación serie con el PicoAdapter.
 */

#include "pico_transport.h"
#include <string.h>
#include <stdio.h>

/* ---------------------------------------------------------------------- */
/*  CRC8 (polinomio 0x07, semilla 0x00) — mismo algoritmo que el firmware */
/* ---------------------------------------------------------------------- */

static uint8_t crc8_update(uint8_t crc, uint8_t b) {
    crc ^= b;
    for (int i = 0; i < 8; i++)
        crc = (crc & 0x80u) ? (uint8_t)((crc << 1u) ^ 0x07u) : (uint8_t)(crc << 1u);
    return crc;
}

static uint8_t crc8_buf(const uint8_t *buf, size_t n) {
    uint8_t crc = 0x00u;
    while (n--)
        crc = crc8_update(crc, *buf++);
    return crc;
}

/* ---------------------------------------------------------------------- */
/*  Apertura / cierre de puerto                                            */
/* ---------------------------------------------------------------------- */

HANDLE pico_port_open(const char *port_name) {
    /* Los puertos > COM9 requieren el prefijo \\.\ */
    char path[64];
    _snprintf_s(path, sizeof(path), _TRUNCATE, "\\\\.\\%s", port_name);

    HANDLE h = CreateFileA(path,
                           GENERIC_READ | GENERIC_WRITE,
                           0, NULL,
                           OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL,
                           NULL);
    if (h == INVALID_HANDLE_VALUE)
        return INVALID_HANDLE_VALUE;

    /* Configurar 115200 8N1, sin control de flujo por hardware */
    DCB dcb;
    memset(&dcb, 0, sizeof(dcb));
    dcb.DCBlength = sizeof(DCB);
    if (!GetCommState(h, &dcb)) {
        CloseHandle(h);
        return INVALID_HANDLE_VALUE;
    }
    dcb.BaudRate       = CBR_115200;
    dcb.ByteSize       = 8;
    dcb.Parity         = NOPARITY;
    dcb.StopBits       = ONESTOPBIT;
    dcb.fBinary        = TRUE;
    dcb.fParity        = FALSE;
    dcb.fOutxCtsFlow   = FALSE;
    dcb.fOutxDsrFlow   = FALSE;
    dcb.fDtrControl    = DTR_CONTROL_ENABLE;
    dcb.fRtsControl    = RTS_CONTROL_ENABLE;
    dcb.fOutX          = FALSE;
    dcb.fInX           = FALSE;
    dcb.fErrorChar     = FALSE;
    dcb.fNull          = FALSE;
    dcb.fAbortOnError  = FALSE;
    if (!SetCommState(h, &dcb)) {
        CloseHandle(h);
        return INVALID_HANDLE_VALUE;
    }

    /* Timeouts: 100 ms para RX, 500 ms para TX */
    COMMTIMEOUTS ct;
    memset(&ct, 0, sizeof(ct));
    ct.ReadTotalTimeoutConstant  = 100u;
    ct.WriteTotalTimeoutConstant = 500u;
    SetCommTimeouts(h, &ct);

    /* Vaciar cualquier dato residual del puerto */
    PurgeComm(h, PURGE_RXCLEAR | PURGE_TXCLEAR);

    return h;
}

HANDLE pico_port_open_uart(const char *port_name) {
    char path[64];
    _snprintf_s(path, sizeof(path), _TRUNCATE, "\\\\.\\%s", port_name);

    HANDLE h = CreateFileA(path,
                           GENERIC_READ | GENERIC_WRITE,
                           0, NULL,
                           OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL,
                           NULL);
    if (h == INVALID_HANDLE_VALUE)
        return INVALID_HANDLE_VALUE;

    DCB dcb;
    memset(&dcb, 0, sizeof(dcb));
    dcb.DCBlength = sizeof(DCB);
    if (!GetCommState(h, &dcb)) {
        CloseHandle(h);
        return INVALID_HANDLE_VALUE;
    }
    dcb.BaudRate       = CBR_115200;
    dcb.ByteSize       = 8;
    dcb.Parity         = NOPARITY;
    dcb.StopBits       = ONESTOPBIT;
    dcb.fBinary        = TRUE;
    dcb.fParity        = FALSE;
    dcb.fOutxCtsFlow   = FALSE;
    dcb.fOutxDsrFlow   = FALSE;
    dcb.fDtrControl    = DTR_CONTROL_ENABLE;
    dcb.fRtsControl    = RTS_CONTROL_ENABLE;
    dcb.fOutX          = FALSE;
    dcb.fInX           = FALSE;
    dcb.fErrorChar     = FALSE;
    dcb.fNull          = FALSE;
    dcb.fAbortOnError  = FALSE;
    if (!SetCommState(h, &dcb)) {
        CloseHandle(h);
        return INVALID_HANDLE_VALUE;
    }

    /* Lectura no bloqueante: ReadFile devuelve inmediatamente con los
     * bytes disponibles (0 si vacío). Escritura con timeout 500 ms. */
    COMMTIMEOUTS ct;
    memset(&ct, 0, sizeof(ct));
    ct.ReadIntervalTimeout        = MAXDWORD;
    ct.ReadTotalTimeoutMultiplier = 0u;
    ct.ReadTotalTimeoutConstant   = 0u;
    ct.WriteTotalTimeoutConstant  = 500u;
    SetCommTimeouts(h, &ct);

    PurgeComm(h, PURGE_RXCLEAR | PURGE_TXCLEAR);

    return h;
}

void pico_port_close(HANDLE h) {
    if (h != INVALID_HANDLE_VALUE) {
        PurgeComm(h, PURGE_RXABORT | PURGE_TXABORT | PURGE_RXCLEAR | PURGE_TXCLEAR);
        CloseHandle(h);
    }
}

/* ---------------------------------------------------------------------- */
/*  Envío de trama                                                         */
/* ---------------------------------------------------------------------- */

bool pico_send(HANDLE h, uint8_t cmd, const uint8_t *payload, uint16_t len) {
    /* Construir la trama completa en un único buffer y enviarla en un solo
     * WriteFile.  Esto garantiza que header + payload + CRC llegan al firmware
     * en el mismo token USB (o en la menor cantidad posible de paquetes CDC),
     * evitando fragmentaciones por el driver usbser.sys de Windows.
     *
     * Tamaño máximo: 4 (hdr) + PICO_MAX_PAYLOAD (4096) + 1 (CRC) = 4101 bytes.
     */
    if (len > PICO_MAX_PAYLOAD) return false;   /* guardia: evita overflow del buffer */
    static uint8_t s_tx_frame[4u + PICO_MAX_PAYLOAD + 1u];

    s_tx_frame[0] = 0xA5u;
    s_tx_frame[1] = cmd;
    s_tx_frame[2] = (uint8_t)(len & 0xFFu);
    s_tx_frame[3] = (uint8_t)(len >> 8u);

    if (len > 0u && payload)
        memcpy(s_tx_frame + 4u, payload, len);

    uint8_t crc = crc8_buf(s_tx_frame, 4u + len);
    s_tx_frame[4u + len] = crc;

    DWORD written = 0;
    uint32_t total = 4u + (uint32_t)len + 1u;
    if (!WriteFile(h, s_tx_frame, total, &written, NULL) || written != total)
        return false;

    return true;
}

/* ---------------------------------------------------------------------- */
/*  Recepción de trama                                                     */
/* ---------------------------------------------------------------------- */

bool pico_recv(HANDLE h,
               uint8_t  *out_resp,
               uint8_t  *out_payload,
               uint16_t *out_len,
               uint32_t  timeout_ms) {
    /* Configurar timeout total para la cabecera */
    COMMTIMEOUTS ct;
    memset(&ct, 0, sizeof(ct));
    ct.ReadTotalTimeoutConstant  = timeout_ms;
    ct.WriteTotalTimeoutConstant = 500u;
    SetCommTimeouts(h, &ct);

    DWORD got;

    /* Leer cabecera: [0xA5][RESP][len_lo][len_hi] */
    uint8_t hdr[4];
    if (!ReadFile(h, hdr, 4u, &got, NULL) || got != 4u)
        return false;
    if (hdr[0] != 0xA5u)
        return false;

    *out_resp = hdr[1];
    uint16_t len = (uint16_t)hdr[2] | ((uint16_t)hdr[3] << 8u);

    /* Usar timeout más corto para el resto de la trama (payload + CRC).
     * Si se produjera un error aquí, restauramos el timeout original antes de
     * retornar para que la siguiente llamada a pico_recv no herede el 500 ms. */
    ct.ReadTotalTimeoutConstant = 500u;
    SetCommTimeouts(h, &ct);

    /* Leer payload */
    if (len > 0u) {
        if (len > PICO_MAX_PAYLOAD || !out_payload) {
            ct.ReadTotalTimeoutConstant = timeout_ms;
            SetCommTimeouts(h, &ct);
            return false;
        }
        if (!ReadFile(h, out_payload, len, &got, NULL) || got != (DWORD)len) {
            ct.ReadTotalTimeoutConstant = timeout_ms;
            SetCommTimeouts(h, &ct);
            return false;
        }
    }

    /* Leer CRC */
    uint8_t crc_byte;
    if (!ReadFile(h, &crc_byte, 1u, &got, NULL) || got != 1u) {
        ct.ReadTotalTimeoutConstant = timeout_ms;
        SetCommTimeouts(h, &ct);
        return false;
    }

    /* Verificar CRC sobre hdr[0..3] + payload */
    uint8_t expected = crc8_buf(hdr, 4u);
    for (uint16_t i = 0; i < len; i++)
        expected = crc8_update(expected, out_payload[i]);

    if (crc_byte != expected)
        return false;

    *out_len = len;
    return true;
}

/* ---------------------------------------------------------------------- */
/*  Transacción completa                                                   */
/* ---------------------------------------------------------------------- */

static uint8_t s_rx_buf[PICO_MAX_PAYLOAD];

bool pico_transact(HANDLE h,
                   uint8_t        cmd,
                   const uint8_t *tx,      uint16_t  tx_len,
                   uint8_t       *rx,      uint16_t *rx_len,
                   uint32_t       timeout_ms) {
    if (!pico_send(h, cmd, tx, tx_len))
        return false;

    uint8_t  resp;
    uint16_t len = 0;
    if (!pico_recv(h, &resp, s_rx_buf, &len, timeout_ms))
        return false;

    if (resp != RESP_OK && resp != RESP_DATA)
        return false;

    if (rx && rx_len) {
        memcpy(rx, s_rx_buf, len);
        *rx_len = len;
    }

    return true;
}
