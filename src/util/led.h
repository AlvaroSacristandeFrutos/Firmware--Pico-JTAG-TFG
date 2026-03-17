#pragma once

#include <stdbool.h>

/* LED verde (GP14) */
void led_init(void);
void led_set(bool on);
void led_toggle(void);

/* LED rojo (GP15) — indicador de error de protocolo */
void led_red_set(bool on);
void led_red_toggle(void);
