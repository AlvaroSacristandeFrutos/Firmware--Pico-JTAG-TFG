#pragma once

#include <stdbool.h>

/* Initialize LED pin (GP25) — call after gpio_init_all(). */
void led_init(void);

/* Set LED on or off. */
void led_set(bool on);

/* Toggle LED state. */
void led_toggle(void);
