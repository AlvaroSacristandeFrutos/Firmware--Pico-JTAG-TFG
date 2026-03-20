#pragma once

/* =================================================================
 * board_config.h — Asignación de pines y constantes de sistema
 *
 * Fuente de verdad única para el hardware. Si se migra a un PCB
 * propio, solo hay que cambiar los números de pin aquí.
 * ================================================================= */

#include <stdint.h>

/* ---- Interfaz JTAG (conectada directamente a PIO0) ---- */
#define PIN_TDI         16   /* GP16 — TDI_OUT al nivel-shifter */
#define PIN_TDO         17   /* GP17 — TDO_IN desde el nivel-shifter */
#define PIN_TCK         18   /* GP18 — TCK_OUT al nivel-shifter */
#define PIN_TMS         19   /* GP19 — TMS_OUT al nivel-shifter */

/* ---- Líneas de reset del objetivo (activas a nivel bajo) ---- */
#define PIN_RST         20   /* GP20 — nRST del objetivo */
#define PIN_TRST        21   /* GP21 — nTRST del objetivo */

/* ---- Control del nivel-shifter ---- */
#define PIN_CTRL_OE     22   /* GP22 — /OE del level-shifter (activo LOW = habilitado) */

/* ---- Read-back de señales de salida (tras el nivel-shifter) ---- */
#define PIN_TDI_RD       6   /* GP6  — lectura real de TDI (para GET_STATE) */
#define PIN_TCK_RD       7   /* GP7  — lectura real de TCK */
#define PIN_TMS_RD       8   /* GP8  — lectura real de TMS */

/* ---- Puente UART (UART0) ---- */
#define PIN_UART_TX     12
#define PIN_UART_RX     13

/* ---- LEDs externos del PCB (activos a nivel alto) ---- */
#define PIN_LED         14   /* GP14 — LED verde (actividad JTAG / USB) */
#define PIN_LED_RED     15   /* GP15 — LED rojo  (error de protocolo) */
#define PIN_LED_ONBOARD 25   /* GP25 — LED onboard de la Pico (actividad UART) */

/* ---- Lectura analógica de tensión ---- */
#define PIN_VREF_ADC    26   /* GP26 / ADC0 — tensión de referencia del objetivo / 2 */
#define ADC_CHANNEL     0

/* ---- Acceso atómico a registros del RP2040 ---- */
/*
 * El RP2040 expone tres alias de cada registro periférico en las 4 KB
 * siguientes a su dirección base. Escribir en estos alias realiza la
 * operación (set/clear/xor) de forma atómica sin necesidad de
 * deshabilitar interrupciones ni hacer read-modify-write.
 *
 * Alias:
 *   addr | 0x1000 → XOR
 *   addr | 0x2000 → SET
 *   addr | 0x3000 → CLEAR
 */
static inline void hw_clear_bits_raw(volatile uint32_t *addr, uint32_t mask) {
    *(volatile uint32_t *)((uintptr_t)addr | 0x3000u) = mask;
}
