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

extern HANDLE   g_hCOM;           /* INVALID_HANDLE_VALUE si cerrado */
extern int      g_is_open;
extern uint32_t g_serial;         /* número de serie guardado en OpenEx */
extern uint32_t g_speed_khz;      /* frecuencia JTAG en kHz (default 4000) */
extern uint32_t g_reset_delay_ms; /* duración del reset hard en ms (default 10) */
extern int      g_reset_type;     /* 0=NORMAL (único soportado) */
extern int      g_jtag_ir_len;    /* IR length total de la cadena */
extern int      g_jtag_dev_pos;   /* posición del dispositivo en la cadena */

/* Cadena scan (guardada por SetDeviceList) */
extern JLINKARM_JTAG_DEVICE_CONF g_dev_list[32];
extern int                        g_dev_count;

/* Logging */
extern JLINKARM_LOG_FUNC *g_warn_cb;
extern JLINKARM_LOG_FUNC *g_error_cb;
extern FILE               *g_log_fp;
