#pragma once
/*
 * jtag_tap_track.h — Rastreador de estado de la FSM TAP IEEE 1149.1 (lado DLL).
 *
 * La DLL construye secuencias TMS+TDI y las envía al firmware en crudo.
 * Para saber en qué estado TAP estamos (Shift-DR, Shift-IR, etc.) y poder
 * aplicar bypass padding multi-dispositivo de forma transparente, necesitamos
 * simular la misma FSM que ejecuta el hardware del target.
 *
 * Uso:
 *   tap_track_reset();                    // tras reset hardware / Open
 *   tap_track_tms(pTMS, numBits);        // tras cada StoreRaw / StoreGetRaw
 *   if (tap_track_state() == TAP_SHIFT_DR) { ... }
 */

#include <stdint.h>

/* Los 16 estados del TAP IEEE 1149.1 */
typedef enum {
    TAP_RESET     = 0,
    TAP_IDLE      = 1,
    TAP_SELECT_DR = 2,
    TAP_CAPTURE_DR= 3,
    TAP_SHIFT_DR  = 4,
    TAP_EXIT1_DR  = 5,
    TAP_PAUSE_DR  = 6,
    TAP_EXIT2_DR  = 7,
    TAP_UPDATE_DR = 8,
    TAP_SELECT_IR = 9,
    TAP_CAPTURE_IR= 10,
    TAP_SHIFT_IR  = 11,
    TAP_EXIT1_IR  = 12,
    TAP_PAUSE_IR  = 13,
    TAP_EXIT2_IR  = 14,
    TAP_UPDATE_IR = 15,
} tap_state_t;

/* Pone el tracker en Test-Logic-Reset (estado tras reset hardware o 5×TMS=1). */
void        tap_track_reset(void);

/*
 * Avanza el estado procesando numBits bits de la secuencia TMS.
 * El bit 0 del byte pTMS[0] es el primer bit TMS enviado (LSB-first).
 */
void        tap_track_tms(const uint8_t *pTMS, uint32_t numBits);

/* Devuelve el estado TAP actual. */
tap_state_t tap_track_state(void);
