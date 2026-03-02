#include "led.h"
#include "board_config.h"

#include "hardware/structs/sio.h"

#define LED_MASK (1u << PIN_LED)

void led_init(void) {
    /* PIN_LED (GP14) ya configurado como SIO output en gpio_init_all(). */
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
