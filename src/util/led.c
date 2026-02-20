#include "led.h"
#include "board_config.h"

#include "hardware/structs/sio.h"

#define LED_MASK (1u << PIN_LED)

void led_init(void) {
    /* GP25 already configured as SIO output by gpio_init_all().
     * Ensure it starts off. */
    sio_hw->gpio_clr = LED_MASK;
}

void led_set(bool on) {
    if (on)
        sio_hw->gpio_set = LED_MASK;
    else
        sio_hw->gpio_clr = LED_MASK;
}

void led_toggle(void) {
    sio_hw->gpio_togl = LED_MASK;
}
