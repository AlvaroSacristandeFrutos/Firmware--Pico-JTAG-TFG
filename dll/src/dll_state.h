#pragma once
/*
 * dll_state.h — Declaraciones extern del estado global de la DLL.
 *
 * Las definiciones residen en jlink_api.c (variables estáticas al módulo).
 * Todos los demás módulos incluyen este header para acceder al estado
 * sin romper la regla de una única definición.
 */

#include <windows.h>
#include <stdio.h>
#include "jlink_types.h"
#include "jtag_tap_track.h"

extern HANDLE   g_hCOM;           /* INVALID_HANDLE_VALUE si cerrado */
extern HANDLE   g_hUART;          /* COM del puente UART (MI_02), INVALID_HANDLE_VALUE si no disponible */
extern int      g_is_open;
extern uint32_t g_serial;         /* número de serie guardado en OpenEx */
extern uint32_t g_speed_khz;      /* frecuencia JTAG en kHz (default 4000) */
extern uint32_t g_reset_delay_ms; /* duración del reset hard en ms (default 10) */
extern int      g_reset_type;     /* 0=NORMAL (único soportado) */
extern int      g_jtag_ir_len;    /* IR length total de la cadena */
extern int      g_jtag_dev_pos;   /* posición del dispositivo en la cadena */

/* Cadena scan (guardada por SetDeviceList / jtag_scan_chain) */
extern JLINKARM_JTAG_DEVICE_CONF g_dev_list[32];
extern int                        g_dev_count;
extern uint32_t                   g_total_ir_len;  /* suma de IRLen de todos los dispositivos */

/* Estado TAP rastreado por la DLL (se actualiza en cada StoreRaw/StoreGetRaw) */
extern tap_state_t g_tap_state;

/* Logging */
extern JLINKARM_LOG_FUNC *g_warn_cb;
extern JLINKARM_LOG_FUNC *g_error_cb;
extern FILE               *g_log_fp;
