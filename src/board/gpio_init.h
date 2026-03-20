#pragma once

/* Saca IO_BANK0 y PADS_BANK0 del reset y configura los pines de los LEDs
 * (GP14, GP15, GP25) como salidas SIO inicializadas a LOW.
 * El resto de pines los configura cada módulo en su propio init. */
void gpio_init_all(void);
