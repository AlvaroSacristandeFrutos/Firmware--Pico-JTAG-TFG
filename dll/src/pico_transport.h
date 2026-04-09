#pragma once
/*
 * pico_transport.h — Capa de comunicación con el PicoAdapter sobre COM port.
 *
 * Formato de trama host → Pico:
 *   [0xA5][CMD][len_lo][len_hi][payload: len bytes][CRC8]
 *
 * Formato de trama Pico → host:
 *   [0xA5][RESP][len_lo][len_hi][payload: len bytes][CRC8]
 *
 * CRC8: polinomio 0x07, semilla 0x00, calculado sobre todos los bytes
 * anteriores al byte de CRC.
 */

#include <windows.h>
#include <stdint.h>
#include <stdbool.h>

/* ---- Comandos del firmware (CMD_*) ---- */
#define CMD_PING            0x01u
#define CMD_RESET_TAP       0x02u
#define CMD_SET_CLOCK       0x03u
#define CMD_RESET_HARD      0x04u
#define CMD_SET_TRST        0x05u
#define CMD_READ_VREF       0x06u
#define CMD_GET_VERSION     0x07u
#define CMD_GET_STATUS      0x08u
#define CMD_SET_LED         0x09u
#define CMD_WRITE_TMS       0x10u
#define CMD_SHIFT_DATA      0x11u
#define CMD_GET_HW_VERSION  0x12u
#define CMD_GET_CLOCK       0x13u
#define CMD_SELECT_IF       0x14u
/* CMD_UART_SET_BAUD (0x20), CMD_UART_SEND (0x21) y CMD_UART_RECV (0x22) eliminados.
 * El baudrate se cambia vía SetCommState sobre el COM MI_02 (g_hUART),
 * que dispara SET_LINE_CODING al firmware. Los datos se envían/reciben
 * directamente sobre ese COM transparente sin protocolo PicoAdapter. */

/* ---- Tipos de respuesta ---- */
#define RESP_OK             0x80u
#define RESP_DATA           0x81u
#define RESP_ERROR          0x82u

/* ---- Tamaño máximo de payload (igual que el firmware) ---- */
#define PICO_MAX_PAYLOAD    4096u

/*
 * Abre el puerto serie especificado (p.ej. "COM7") a 115200 8N1.
 * Devuelve INVALID_HANDLE_VALUE en caso de error.
 */
HANDLE pico_port_open(const char *port_name);

/*
 * Igual que pico_port_open pero con timeouts de lectura inmediatos
 * (ReadFile no bloquea: devuelve 0 bytes si el buffer está vacío).
 * Usado para el puente UART transparente (MI_02).
 */
HANDLE pico_port_open_uart(const char *port_name);

/*
 * Cierra el puerto.
 */
void pico_port_close(HANDLE h);

/*
 * Envía un comando al Pico.
 * payload puede ser NULL si len == 0.
 */
bool pico_send(HANDLE h, uint8_t cmd, const uint8_t *payload, uint16_t len);

/*
 * Recibe una respuesta del Pico.
 *   out_resp    ← byte de tipo de respuesta (RESP_OK / RESP_DATA / RESP_ERROR)
 *   out_payload ← buffer de al menos PICO_MAX_PAYLOAD bytes
 *   out_len     ← longitud del payload recibido
 *   timeout_ms  ← tiempo máximo de espera para el primer byte
 */
bool pico_recv(HANDLE h,
               uint8_t  *out_resp,
               uint8_t  *out_payload,
               uint16_t *out_len,
               uint32_t  timeout_ms);

/*
 * Transacción completa: pico_send() + pico_recv() en una sola llamada.
 *   rx y rx_len pueden ser NULL si no se espera payload de respuesta.
 */
bool pico_transact(HANDLE h,
                   uint8_t        cmd,
                   const uint8_t *tx,      uint16_t  tx_len,
                   uint8_t       *rx,      uint16_t *rx_len,
                   uint32_t       timeout_ms);
