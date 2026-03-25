/*
 * pico_protocol.c — Parser de protocolo serie PicoAdapter sobre USB-CDC.
 *
 * Formato de trama PC → Pico:
 *   [0xA5][CMD][len_lo][len_hi][payload: len bytes][CRC8]
 *
 * Formato de trama Pico → PC:
 *   [0xA5][RESP][len_lo][len_hi][payload: len bytes][CRC8]
 *
 * CRC8: polinomio 0x07, semilla 0x00, calculado sobre todos los bytes
 * anteriores al byte de CRC (incluyendo 0xA5, CMD y bytes de longitud).
 */

#include "pico_protocol.h"
#include "usb_device.h"      /* cdc_send() */
#include "jtag_pio.h"        /* jtag_pio_write_read(), jtag_pio_write(), jtag_set_freq(), jtag_get_freq() */
#include "jtag_tap.h"        /* jtag_tap_reset(), jtag_tap_set_tms() */
#include "adc.h"             /* adc_read_vref_mv() */
#include "board_config.h"    /* PIN_RST, PIN_TRST */
#include "led.h"             /* led_red_set() */
#include "uart/uart_driver.h" /* uart_driver_set_baud(), uart_driver_send(), uart_driver_recv() */

#include "hardware/structs/sio.h"
#include "pico/time.h"    /* busy_wait_us_32() */

#include <stdint.h>
#include <stddef.h>
#include <string.h>

/* ---------------------------------------------------------------------- */
/*  Constantes de protocolo                                                */
/* ---------------------------------------------------------------------- */

#define PROTO_START     0xA5u
#define CMD_PING        0x01u
#define CMD_RESET_TAP   0x02u
#define CMD_SET_CLOCK   0x03u
#define CMD_RESET_HARD  0x04u
#define CMD_SET_TRST    0x05u
#define CMD_READ_VREF   0x06u
#define CMD_GET_VERSION 0x07u
#define CMD_GET_STATUS  0x08u
#define CMD_SET_LED     0x09u
#define CMD_WRITE_TMS       0x10u
#define CMD_SHIFT_DATA      0x11u
#define CMD_GET_HW_VERSION  0x12u   /* devuelve u32 versión hardware */
#define CMD_GET_CLOCK       0x13u   /* devuelve u32 frecuencia actual en kHz */
#define CMD_SELECT_IF       0x14u   /* selecciona interfaz: 0=JTAG (único soportado) */

#define FIRMWARE_VERSION "PicoAdapter v1.0"
/* Versión hardware: 1.0.0 en formato SEGGER (Major<<16 | Minor<<8 | Patch) */
#define HW_VERSION_U32   0x00010000u
#define RESP_OK         0x80u
#define RESP_DATA       0x81u
#define RESP_ERROR      0x82u

#define MAX_PAYLOAD     4096u

/* ---------------------------------------------------------------------- */
/*  Máquina de estados                                                     */
/* ---------------------------------------------------------------------- */

typedef enum {
    ST_WAIT_START,
    ST_WAIT_CMD,
    ST_WAIT_LEN_LO,
    ST_WAIT_LEN_HI,
    ST_RECV_PAYLOAD,
    ST_RECV_CRC,
} parser_state_t;

static parser_state_t s_state   = ST_WAIT_START;
static uint8_t        s_cmd     = 0;
static uint16_t       s_len     = 0;
static uint16_t       s_recv    = 0;
static uint8_t        s_crc_acc = 0;   /* CRC acumulado sobre bytes ya procesados */
static uint8_t        s_payload[MAX_PAYLOAD];

/* Buffer para construir la trama de respuesta completa */
static uint8_t        s_tx_frame[4u + MAX_PAYLOAD + 1u]; /* hdr(4)+datos(MAX)+CRC(1) */

/* Buffer TDO para CMD_SHIFT_DATA */
static uint8_t        s_tdo_buf[MAX_PAYLOAD];

/* ---------------------------------------------------------------------- */
/*  CRC8 (polinomio 0x07, semilla 0x00)                                   */
/* ---------------------------------------------------------------------- */

/*
 * Tabla precomputada para CRC-8/SMBUS (polinomio 0x07).
 * Reside en Flash (.rodata) — 256 bytes.
 * Sustituye el bucle interno de 8 iteraciones por un único acceso indexado:
 *   crc_nuevo = tabla[crc_actual ^ byte_entrada]
 */
static const uint8_t k_crc8_table[256] = {
    0x00, 0x07, 0x0E, 0x09, 0x1C, 0x1B, 0x12, 0x15,
    0x38, 0x3F, 0x36, 0x31, 0x24, 0x23, 0x2A, 0x2D,
    0x70, 0x77, 0x7E, 0x79, 0x6C, 0x6B, 0x62, 0x65,
    0x48, 0x4F, 0x46, 0x41, 0x54, 0x53, 0x5A, 0x5D,
    0xE0, 0xE7, 0xEE, 0xE9, 0xFC, 0xFB, 0xF2, 0xF5,
    0xD8, 0xDF, 0xD6, 0xD1, 0xC4, 0xC3, 0xCA, 0xCD,
    0x90, 0x97, 0x9E, 0x99, 0x8C, 0x8B, 0x82, 0x85,
    0xA8, 0xAF, 0xA6, 0xA1, 0xB4, 0xB3, 0xBA, 0xBD,
    0xC7, 0xC0, 0xC9, 0xCE, 0xDB, 0xDC, 0xD5, 0xD2,
    0xFF, 0xF8, 0xF1, 0xF6, 0xE3, 0xE4, 0xED, 0xEA,
    0xB7, 0xB0, 0xB9, 0xBE, 0xAB, 0xAC, 0xA5, 0xA2,
    0x8F, 0x88, 0x81, 0x86, 0x93, 0x94, 0x9D, 0x9A,
    0x27, 0x20, 0x29, 0x2E, 0x3B, 0x3C, 0x35, 0x32,
    0x1F, 0x18, 0x11, 0x16, 0x03, 0x04, 0x0D, 0x0A,
    0x57, 0x50, 0x59, 0x5E, 0x4B, 0x4C, 0x45, 0x42,
    0x6F, 0x68, 0x61, 0x66, 0x73, 0x74, 0x7D, 0x7A,
    0x89, 0x8E, 0x87, 0x80, 0x95, 0x92, 0x9B, 0x9C,
    0xB1, 0xB6, 0xBF, 0xB8, 0xAD, 0xAA, 0xA3, 0xA4,
    0xF9, 0xFE, 0xF7, 0xF0, 0xE5, 0xE2, 0xEB, 0xEC,
    0xC1, 0xC6, 0xCF, 0xC8, 0xDD, 0xDA, 0xD3, 0xD4,
    0x69, 0x6E, 0x67, 0x60, 0x75, 0x72, 0x7B, 0x7C,
    0x51, 0x56, 0x5F, 0x58, 0x4D, 0x4A, 0x43, 0x44,
    0x19, 0x1E, 0x17, 0x10, 0x05, 0x02, 0x0B, 0x0C,
    0x21, 0x26, 0x2F, 0x28, 0x3D, 0x3A, 0x33, 0x34,
    0x4E, 0x49, 0x40, 0x47, 0x52, 0x55, 0x5C, 0x5B,
    0x76, 0x71, 0x78, 0x7F, 0x6A, 0x6D, 0x64, 0x63,
    0x3E, 0x39, 0x30, 0x37, 0x22, 0x25, 0x2C, 0x2B,
    0x06, 0x01, 0x08, 0x0F, 0x1A, 0x1D, 0x14, 0x13,
    0xAE, 0xA9, 0xA0, 0xA7, 0xB2, 0xB5, 0xBC, 0xBB,
    0x96, 0x91, 0x98, 0x9F, 0x8A, 0x8D, 0x84, 0x83,
    0xDE, 0xD9, 0xD0, 0xD7, 0xC2, 0xC5, 0xCC, 0xCB,
    0xE6, 0xE1, 0xE8, 0xEF, 0xFA, 0xFD, 0xF4, 0xF3,
};

static inline uint8_t crc8_byte(uint8_t crc, uint8_t byte) {
    return k_crc8_table[crc ^ byte];
}

static uint8_t crc8_block(const uint8_t *data, size_t len) {
    uint8_t crc = 0x00u;
    for (size_t i = 0; i < len; i++)
        crc = k_crc8_table[crc ^ data[i]];
    return crc;
}

/* ---------------------------------------------------------------------- */
/*  Funciones de respuesta                                                 */
/* ---------------------------------------------------------------------- */

static void send_resp_simple(uint8_t code, bool is_error) {
    led_red_set(is_error);
    uint8_t frame[5] = {PROTO_START, code, 0x00u, 0x00u, 0x00u};
    frame[4] = crc8_block(frame, 4u);
    cdc_send(frame, 5u);
}
#define send_resp_ok()    send_resp_simple(RESP_OK,    false)
#define send_resp_error() send_resp_simple(RESP_ERROR, true)

static void send_resp_data(const uint8_t *data, uint16_t len) {
    led_red_set(false);
    s_tx_frame[0] = PROTO_START;
    s_tx_frame[1] = RESP_DATA;
    s_tx_frame[2] = (uint8_t)(len & 0xFFu);
    s_tx_frame[3] = (uint8_t)(len >> 8u);
    memcpy(&s_tx_frame[4], data, len);
    s_tx_frame[4u + len] = crc8_block(s_tx_frame, 4u + (size_t)len);
    cdc_send(s_tx_frame, (uint16_t)(5u + len));
}

/* ---------------------------------------------------------------------- */
/*  Handlers de comando                                                    */
/* ---------------------------------------------------------------------- */

static void handle_ping(void) {
    send_resp_ok();
}

static void handle_reset_tap(void) {
    jtag_tap_reset();
    send_resp_ok();
}

/* payload: uint32 LE en Hz */
static void handle_set_clock(const uint8_t *payload, uint16_t len) {
    if (len < 4u) { send_resp_error(); return; }
    uint32_t hz = (uint32_t)payload[0]
                | ((uint32_t)payload[1] << 8u)
                | ((uint32_t)payload[2] << 16u)
                | ((uint32_t)payload[3] << 24u);
    jtag_set_freq(hz / 1000u);   /* firmware trabaja en kHz */
    send_resp_ok();
}

/*
 * CMD_WRITE_TMS — payload: [numBits:u16 LE][tmsBytes: ceil(numBits/8)]
 *
 * Para cada bit i:
 *   1. Extraer el bit i del array tmsBytes y fijar TMS con jtag_tap_set_tms()
 *   2. Generar 1 pulso de TCK con jtag_pio_write() con TDI=0
 *
 * TMS es controlado por SIO (GP19); TCK/TDI lo gestiona el PIO.
 * jtag_pio_write() con un buffer de ceros y len_bits=1 genera exactamente
 * un ciclo de reloj JTAG.
 */
static void handle_write_tms(const uint8_t *payload, uint16_t len) {
    if (len < 2u) {
        send_resp_error();
        return;
    }
    /* num_bits: u16 LE — permite hasta 65535 bits de TMS */
    uint16_t num_bits = (uint16_t)payload[0] | ((uint16_t)payload[1] << 8u);
    uint16_t needed   = 2u + (num_bits + 7u) / 8u;
    if (len < needed) {
        send_resp_error();
        return;
    }
    const uint8_t *tms_bytes = payload + 2u;

    /*
     * Optimización: agrupar bits consecutivos con el mismo nivel de TMS
     * y enviarlos en una única llamada DMA en lugar de un DMA por bit.
     *
     * TMS se controla por SIO (pin fijo entre pulsos del mismo nivel);
     * TCK lo genera el PIO para el bloque completo.  Esto reduce el
     * número de transacciones DMA de N (bits) a K (transiciones TMS),
     * que en JTAG típico es O(comandos) en lugar de O(bits).
     *
     * Buffer de ceros para TDI: 64 bytes = 512 bits por llamada DMA.
     * jtag_pio_write() ya fragmenta internamente en chunks de 512 bits.
     */
    static const uint8_t k_zeros[64] = {0u};

    uint16_t i = 0u;
    while (i < num_bits) {
        bool tms_val = (bool)((tms_bytes[i >> 3u] >> (i & 7u)) & 1u);
        jtag_tap_set_tms(tms_val);

        /* Contar cuántos bits consecutivos comparten el mismo nivel TMS */
        uint16_t run = 1u;
        while ((i + run) < num_bits && run < 512u) {
            bool next = (bool)((tms_bytes[(i + run) >> 3u] >> ((i + run) & 7u)) & 1u);
            if (next != tms_val) break;
            run++;
        }

        /* Generar 'run' pulsos TCK en una sola operación DMA */
        if (!jtag_pio_write(k_zeros, (uint32_t)run)) {
            send_resp_error();
            return;
        }
        i = (uint16_t)(i + run);
    }
    send_resp_ok();
}

/*
 * CMD_SHIFT_DATA — payload: [numBits:u32 LE][exitShift:u8][tdi: ceil(numBits/8)]
 *
 * exitShift: 0=permanecer en Shift, 1=salir a Exit1 (último bit con TMS=1).
 * Responde RESP_DATA con los bytes TDO capturados.
 */
static void handle_shift_data(const uint8_t *payload, uint16_t len) {
    if (len < 5u) {
        send_resp_error();
        return;
    }
    uint32_t num_bits = (uint32_t)payload[0]
                      | ((uint32_t)payload[1] << 8u)
                      | ((uint32_t)payload[2] << 16u)
                      | ((uint32_t)payload[3] << 24u);
    bool     exit_shift = (payload[4] != 0u);
    uint32_t num_bytes  = (num_bits + 7u) / 8u;

    /*
     * Validar tamaño. El límite real no es el buffer TDO (4096 bytes) sino
     * el buffer DMA de recepción en jtag_pio.c (s_rx_words[1022]), que
     * acota cada transferencia a 1022 bytes = 8176 bits. Superar ese límite
     * hace que jtag_pio_write_read() devuelva false, pero sin este check
     * el error solo se detectaría después de procesar el payload completo.
     */
    if (num_bytes > 1022u ||
        (uint32_t)len < 5u + num_bytes) {
        send_resp_error();
        return;
    }
    const uint8_t *tdi = payload + 5u;

    led_toggle();   /* parpadeo verde: inicio transferencia */
    bool ok = jtag_pio_write_read_exit(tdi, s_tdo_buf, num_bits, exit_shift);
    led_toggle();   /* parpadeo verde: fin transferencia */

    if (!ok) {
        send_resp_error();
        return;
    }
    send_resp_data(s_tdo_buf, (uint16_t)num_bytes);
}

/*
 * CMD_RESET_HARD — payload: [duration_ms: u16 LE]
 *
 * Afirma nRST (GP20) durante duration_ms milisegundos y luego lo suelta.
 * Si duration_ms == 0 se usa un valor por defecto de 10 ms.
 */
static void handle_reset_hard(const uint8_t *payload, uint16_t len) {
    uint16_t ms = (len >= 2u)
        ? (uint16_t)((uint16_t)payload[0] | ((uint16_t)payload[1] << 8u))
        : 0u;
    if (ms == 0u)    ms = 10u;
    if (ms > 1000u)  ms = 1000u;   /* cap: no superar la ventana del watchdog (2 s) */
    sio_hw->gpio_clr = (1u << PIN_RST);
    busy_wait_us_32((uint32_t)ms * 1000u);
    sio_hw->gpio_set = (1u << PIN_RST);
    send_resp_ok();
}

/*
 * CMD_SET_TRST — payload: [level: u8]
 *
 * Controla nTRST (GP21): 0 = afirmar (LOW, activo), distinto de 0 = soltar (HIGH).
 */
static void handle_set_trst(const uint8_t *payload, uint16_t len) {
    if (len < 1u) { send_resp_error(); return; }
    if (payload[0])
        sio_hw->gpio_set = (1u << PIN_TRST);
    else
        sio_hw->gpio_clr = (1u << PIN_TRST);
    send_resp_ok();
}

/*
 * CMD_READ_VREF — sin payload
 *
 * Devuelve la tensión de referencia del objetivo en mV (u16 LE)
 * leída del canal ADC0 (GP26).
 */
static void handle_read_vref(void) {
    uint16_t mv = adc_read_vref_mv();
    uint8_t data[2] = { (uint8_t)(mv & 0xFFu), (uint8_t)(mv >> 8u) };
    send_resp_data(data, 2u);
}

/*
 * CMD_GET_VERSION — sin payload
 *
 * Devuelve el string de versión del firmware (ASCII, null-terminated).
 * sizeof(FIRMWARE_VERSION) incluye el '\0' implícito del literal de cadena.
 */
static void handle_get_version(void) {
    send_resp_data((const uint8_t *)FIRMWARE_VERSION,
                   (uint16_t)(sizeof(FIRMWARE_VERSION) - 1u));
}

/*
 * CMD_GET_HW_VERSION — sin payload
 *
 * Devuelve la versión hardware como u32 LE en formato SEGGER:
 *   bits[31:16] = Major, bits[15:8] = Minor, bits[7:0] = Patch
 * Valor: 0x00010000 → v1.0.0
 */
static void handle_get_hw_version(void) {
    const uint32_t v = HW_VERSION_U32;
    uint8_t data[4] = {
        (uint8_t)( v        & 0xFFu),
        (uint8_t)((v >>  8) & 0xFFu),
        (uint8_t)((v >> 16) & 0xFFu),
        (uint8_t)((v >> 24) & 0xFFu),
    };
    send_resp_data(data, 4u);
}

/*
 * CMD_GET_CLOCK — sin payload
 *
 * Devuelve la frecuencia JTAG actual en kHz como u32 LE.
 */
static void handle_get_clock(void) {
    const uint32_t khz = jtag_get_freq();
    uint8_t data[4] = {
        (uint8_t)( khz        & 0xFFu),
        (uint8_t)((khz >>  8) & 0xFFu),
        (uint8_t)((khz >> 16) & 0xFFu),
        (uint8_t)((khz >> 24) & 0xFFu),
    };
    send_resp_data(data, 4u);
}

/*
 * CMD_SELECT_IF — payload: [iface: u8]
 *
 * Selecciona la interfaz de depuración:
 *   0x00 = JTAG (soportado)
 *   0x01 = SWD  (no implementado → RESP_ERROR)
 * Responde RESP_OK si JTAG, RESP_ERROR si cualquier otro valor.
 */
static void handle_select_if(const uint8_t *payload, uint16_t len) {
    if (len < 1u) { send_resp_error(); return; }
    if (payload[0] == 0x00u) send_resp_ok();
    else send_resp_error();
}

/*
 * CMD_GET_STATUS — sin payload
 *
 * Devuelve 4 bytes:
 *   [0] flags: bit0=USB configurado, bit1=Vref>1500 mV (target presente)
 *   [1] Vref mV byte bajo
 *   [2] Vref mV byte alto
 *   [3] 0x00 reservado
 */
static void handle_get_status(void) {
    uint16_t mv = adc_read_vref_mv();
    uint8_t flags = 0x01u;                       /* bit0: USB siempre configurado aquí */
    if (mv > 1500u) flags |= 0x02u;             /* bit1: target presente */
    uint8_t data[4] = {
        flags,
        (uint8_t)(mv & 0xFFu),
        (uint8_t)(mv >> 8u),
        0x00u
    };
    send_resp_data(data, 4u);
}

/*
 * CMD_UART_SET_BAUD (0x20) — payload: [baud: u32 LE]
 * Cambia la velocidad de UART0 en tiempo de ejecución.
 */
static void handle_uart_set_baud(const uint8_t *payload, uint16_t len) {
    if (len < 4u) { send_resp_error(); return; }
    uint32_t baud = (uint32_t)payload[0]
                  | ((uint32_t)payload[1] << 8u)
                  | ((uint32_t)payload[2] << 16u)
                  | ((uint32_t)payload[3] << 24u);
    uart_driver_set_baud(baud);
    send_resp_ok();
}

/*
 * CMD_UART_SEND (0x21) — payload: [data: N bytes]
 * Envía N bytes al target por UART0 (GP12 TX).
 */
static void handle_uart_send(const uint8_t *payload, uint16_t len) {
    if (len > 0u)
        uart_driver_send(payload, len);
    send_resp_ok();
}

/*
 * CMD_UART_RECV (0x22) — sin payload
 * Devuelve hasta 512 bytes del buffer RX circular de UART0 (GP13 RX).
 * RESP_DATA con 0 bytes es válido (buffer vacío).
 */
static void handle_uart_recv(void) {
    uint16_t got = uart_driver_recv(s_payload, 512u);
    send_resp_data(s_payload, got);
}

/*
 * CMD_SET_LED — payload: [mask: u8]
 *   bit0 = LED verde (GP14): 1=encendido, 0=apagado
 *   bit1 = LED rojo  (GP15): 1=encendido, 0=apagado
 */
static void handle_set_led(const uint8_t *payload, uint16_t len) {
    if (len < 1u) { send_resp_error(); return; }
    led_set((payload[0] & 0x01u) != 0u);
    led_red_set((payload[0] & 0x02u) != 0u);
    send_resp_ok();
}

/* ---------------------------------------------------------------------- */
/*  Dispatcher                                                             */
/* ---------------------------------------------------------------------- */

static void dispatch(void) {
    switch (s_cmd) {
    case CMD_PING:
        handle_ping();
        break;
    case CMD_RESET_TAP:
        handle_reset_tap();
        break;
    case CMD_SET_CLOCK:
        handle_set_clock(s_payload, s_len);
        break;
    case CMD_RESET_HARD:
        handle_reset_hard(s_payload, s_len);
        break;
    case CMD_SET_TRST:
        handle_set_trst(s_payload, s_len);
        break;
    case CMD_READ_VREF:
        handle_read_vref();
        break;
    case CMD_GET_VERSION:
        handle_get_version();
        break;
    case CMD_GET_STATUS:
        handle_get_status();
        break;
    case CMD_SET_LED:
        handle_set_led(s_payload, s_len);
        break;
    case CMD_WRITE_TMS:
        handle_write_tms(s_payload, s_len);
        break;
    case CMD_SHIFT_DATA:
        handle_shift_data(s_payload, s_len);
        break;
    case CMD_GET_HW_VERSION:
        handle_get_hw_version();
        break;
    case CMD_GET_CLOCK:
        handle_get_clock();
        break;
    case CMD_SELECT_IF:
        handle_select_if(s_payload, s_len);
        break;
    case CMD_UART_SET_BAUD:
        handle_uart_set_baud(s_payload, s_len);
        break;
    case CMD_UART_SEND:
        handle_uart_send(s_payload, s_len);
        break;
    case CMD_UART_RECV:
        handle_uart_recv();
        break;
    default:
        send_resp_error();
        break;
    }
}

/* ---------------------------------------------------------------------- */
/*  Parser (máquina de estados byte a byte)                               */
/* ---------------------------------------------------------------------- */

void protocol_feed(uint8_t byte) {
    switch (s_state) {

    case ST_WAIT_START:
        if (byte == PROTO_START) {
            s_crc_acc = crc8_byte(0x00u, byte);
            s_state   = ST_WAIT_CMD;
        }
        break;

    case ST_WAIT_CMD:
        s_cmd     = byte;
        s_crc_acc = crc8_byte(s_crc_acc, byte);
        s_state   = ST_WAIT_LEN_LO;
        break;

    case ST_WAIT_LEN_LO:
        s_len     = byte;
        s_crc_acc = crc8_byte(s_crc_acc, byte);
        s_state   = ST_WAIT_LEN_HI;
        break;

    case ST_WAIT_LEN_HI:
        s_len    |= (uint16_t)((uint16_t)byte << 8u);
        s_crc_acc = crc8_byte(s_crc_acc, byte);
        if (s_len > (uint16_t)MAX_PAYLOAD) {
            send_resp_error();       /* fallo rápido: payload demasiado grande */
            s_state = ST_WAIT_START;
            break;
        }
        s_recv  = 0u;
        s_state = (s_len == 0u) ? ST_RECV_CRC : ST_RECV_PAYLOAD;
        break;

    case ST_RECV_PAYLOAD:
        /* Almacenar solo si cabe en el buffer; siempre actualizar CRC */
        if (s_recv < (uint16_t)MAX_PAYLOAD)
            s_payload[s_recv] = byte;
        s_crc_acc = crc8_byte(s_crc_acc, byte);
        s_recv++;
        if (s_recv >= s_len)
            s_state = ST_RECV_CRC;
        break;

    case ST_RECV_CRC:
        if (byte == s_crc_acc)
            dispatch();
        /* Si CRC incorrecto: descartar silenciosamente (s_len ya validado en ST_WAIT_LEN_HI) */
        s_state = ST_WAIT_START;
        break;
    }
}
