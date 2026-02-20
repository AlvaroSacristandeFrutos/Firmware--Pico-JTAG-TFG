#pragma once

/* =================================================================
 * board_config.h — Asignación de pines y constantes de sistema
 *
 * Fuente de verdad única para el hardware. Si se migra a un PCB
 * propio, solo hay que cambiar los números de pin aquí.
 * ================================================================= */

#include <stdint.h>

/* ---- Interfaz JTAG (conectada directamente a PIO0) ---- */
#define PIN_TDI         16
#define PIN_TDO         17
#define PIN_TCK         18
#define PIN_TMS         19

/* ---- Líneas de reset del objetivo (activas a nivel bajo) ---- */
#define PIN_RST         20
#define PIN_TRST        21

/* ---- Puente UART (UART0) ---- */
#define PIN_UART_TX     12
#define PIN_UART_RX     13

/* ---- LED de estado (LED integrado de la Pico, activo a nivel alto) ---- */
#define PIN_LED         25

/* ---- Lectura analógica de tensión ---- */
#define PIN_VREF_ADC    26   /* ADC0 — tensión de referencia del objetivo / 2 */
#define ADC_CHANNEL     0

/* ---- Máscaras de conveniencia para operaciones sobre varios pines ---- */
#define JTAG_PIN_MASK   ((1u << PIN_TDI) | (1u << PIN_TDO) | \
                         (1u << PIN_TCK) | (1u << PIN_TMS))
#define RST_PIN_MASK    ((1u << PIN_RST) | (1u << PIN_TRST))

/* ---- Frecuencias de reloj del sistema (Hz) ---- */
/* El SDK de Pico define estas macros en platform_defs.h.
 * Las guardamos con #ifndef para evitar advertencias de redefinición. */
#ifndef SYS_CLK_HZ
#define SYS_CLK_HZ     125000000u
#endif
#ifndef USB_CLK_HZ
#define USB_CLK_HZ      48000000u
#endif
#ifndef XOSC_HZ
#define XOSC_HZ         12000000u
#endif

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
static inline void hw_set_bits_raw(volatile uint32_t *addr, uint32_t mask) {
    *(volatile uint32_t *)((uintptr_t)addr | 0x2000u) = mask;
}

static inline void hw_clear_bits_raw(volatile uint32_t *addr, uint32_t mask) {
    *(volatile uint32_t *)((uintptr_t)addr | 0x3000u) = mask;
}

static inline void hw_xor_bits_raw(volatile uint32_t *addr, uint32_t mask) {
    *(volatile uint32_t *)((uintptr_t)addr | 0x1000u) = mask;
}
