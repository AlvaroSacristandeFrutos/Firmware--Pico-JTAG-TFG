#pragma once

/* Bring GPIO subsystem out of reset and configure all pin functions.
 * Only the LED pin (GP25) is fully configured in the skeleton. */
void gpio_init_all(void);
