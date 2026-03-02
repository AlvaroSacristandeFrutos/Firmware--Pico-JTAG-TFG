#pragma once

/* =================================================================
 * jlink_protocol.h — Definiciones del protocolo J-Link
 *
 * IDs de comando EMU_CMD_*, bits de capabilities y constantes USB
 * del protocolo que usa la DLL de Segger para comunicarse con el probe.
 * ================================================================= */

#include <stdint.h>

/* ---- IDs de comando EMU_CMD ---- */
#define EMU_CMD_VERSION             0x01
#define EMU_CMD_SET_SPEED           0x05
#define EMU_CMD_GET_STATE           0x07
#define EMU_CMD_SET_KS_POWER        0x08
#define EMU_CMD_GET_SPEEDS          0xC0
#define EMU_CMD_GET_HW_INFO         0xC1
#define EMU_CMD_GET_COUNTERS        0xC2
#define EMU_CMD_SELECT_IF           0xC7
#define EMU_CMD_HW_CLOCK           0xC8
#define EMU_CMD_HW_TMS0            0xC9
#define EMU_CMD_HW_TMS1            0xCA
#define EMU_CMD_HW_DATA0           0xCB
#define EMU_CMD_HW_DATA1           0xCC
#define EMU_CMD_HW_JTAG            0xCD
#define EMU_CMD_HW_JTAG2           0xCE
#define EMU_CMD_HW_JTAG3           0xCF
#define EMU_CMD_HW_RELEASE_TRST    0xD0
#define EMU_CMD_HW_SET_TRST        0xD1
#define EMU_CMD_GET_MAX_MEM_BLOCK   0xD4
#define EMU_CMD_HW_RESET0          0xDC
#define EMU_CMD_HW_RESET1          0xDD
#define EMU_CMD_HW_TRST0           0xDE
#define EMU_CMD_HW_TRST1           0xDF
#define EMU_CMD_GET_CAPS            0xE8
#define EMU_CMD_GET_CPU_CAPS        0xE9
#define EMU_CMD_EXEC_CPU_CMD        0xEA
#define EMU_CMD_GET_CAPS_EX         0xED
#define EMU_CMD_IDSEGGER            0x16   /* handshake de autenticación SEGGER */
#define EMU_CMD_UNKNOWN_09          0x09   /* telemetría interna no documentada */
#define EMU_CMD_UNKNOWN_0D          0x0D   /* telemetría interna no documentada */
#define EMU_CMD_UNKNOWN_0E          0x0E   /* telemetría interna no documentada */
#define EMU_CMD_GET_HW_VERSION      0xF0
#define EMU_CMD_WRITE_DCC           0xF1
#define EMU_CMD_READ_CONFIG         0xF2
#define EMU_CMD_WRITE_CONFIG        0xF3

/* ---- Bits de capabilities (respuesta a EMU_CMD_GET_CAPS) ---- */
#define EMU_CAP_RESERVED_1          (1u << 0)
#define EMU_CAP_GET_HW_VERSION      (1u << 1)
#define EMU_CAP_WRITE_DCC           (1u << 2)
#define EMU_CAP_ADAPTIVE_CLOCKING   (1u << 3)
#define EMU_CAP_READ_CONFIG         (1u << 4)
#define EMU_CAP_WRITE_CONFIG        (1u << 5)
#define EMU_CAP_TRACE               (1u << 6)
#define EMU_CAP_WRITE_MEM           (1u << 7)
#define EMU_CAP_READ_MEM            (1u << 8)
#define EMU_CAP_GET_SPEEDS          (1u << 9)
#define EMU_CAP_EXEC_CODE           (1u << 10)
#define EMU_CAP_GET_MAX_BLOCK_SIZE  (1u << 11)
#define EMU_CAP_GET_HW_INFO         (1u << 12)
#define EMU_CAP_SET_KS_POWER        (1u << 13)
#define EMU_CAP_RESET_STOP_TIMED    (1u << 14)
#define EMU_CAP_GET_CPU_CAPS        (1u << 16)
#define EMU_CAP_SELECT_IF           (1u << 17)
#define EMU_CAP_RW_MEM_ARM79        (1u << 18)
#define EMU_CAP_GET_COUNTERS        (1u << 19)
#define EMU_CAP_SWD                 (1u << 20)
#define EMU_CAP_GET_CAPS_EX         (1u << 21)

/* Capabilities que este probe anuncia como implementadas.
 * Valor 0xB9FF7BBF capturado del clon chino J-Link V9 funcionando con jlink.sys V8.90. */
#define JLINK_CAPS  0xB9FF7BBFu

/* ---- Versión de hardware ---- */
/* Formato: MMmmrr00 donde MM=mayor, mm=menor, rr=revisión.
 * V9.70 = 97000. Los clones V9.0 (90000) son bloqueados por jlink.sys moderno. */
#define JLINK_HW_VERSION    97000u

/* ---- Velocidades de reloj JTAG ---- */
#define JLINK_BASE_FREQ     48000000u   /* frecuencia base del divisor PIO */
#define JLINK_MIN_DIV       4u          /* divisor mínimo (frecuencia máxima = 12 MHz) */
#define JLINK_DEFAULT_SPEED 4000u       /* velocidad por defecto en kHz */

/* ---- Tipos de interfaz de debug ---- */
#define JLINK_IF_JTAG       0
#define JLINK_IF_SWD        1

/* ---- Identificadores USB ---- */
/* VID/PID de pid.codes (open hardware) para cuando usemos nuestro propio driver.
 * En modo de compatibilidad con Segger, usamos 0x1366:0x0101 en los descriptores. */
#define JLINK_USB_VID       0x1209u
#define JLINK_USB_PID       0xD0DAu

/* ---- Endpoints USB ---- */
#define JLINK_EP_OUT        0x01    /* host → probe (bulk OUT, EP1) */
#define JLINK_EP_IN         0x81    /* probe → host (interrupt IN, EP1 — igual que un J-Link real) */

/* ---- Tamaños de buffer ---- */
#define CMD_BUF_COUNT       4
#define CMD_BUF_SIZE        2048
#define TX_BUF_SIZE         2400    /* debe ser >= IDSEGGER_BODY_LEN (2304) */

/* Longitud del payload IDSEGGER (dos bloques de autenticación de licencias). */
#define IDSEGGER_BODY_LEN   2304

/*
 * Estructura de buffer de comando preparada para una arquitectura dual-core
 * donde Core 1 procesa comandos JTAG de forma asíncrona mientras Core 0
 * atiende el USB. Por ahora el procesamiento es síncrono en Core 0 y esta
 * estructura no se usa, pero está definida aquí para cuando se implemente.
 */
typedef struct {
    volatile uint8_t  busy;         /* 1 mientras Core 1 está procesando */
    volatile uint16_t rx_count;     /* bytes recibidos del host */
    volatile uint16_t tx_count;     /* bytes a enviar de vuelta al host */
    uint8_t rx_data[CMD_BUF_SIZE];
    uint8_t tx_data[TX_BUF_SIZE];
} cmd_buffer_t;
