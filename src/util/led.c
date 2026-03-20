#include "led.h"
#include "board_config.h"

#include "hardware/structs/sio.h"

#define LED_MASK      (1u << PIN_LED)
#define LED_RED_MASK  (1u << PIN_LED_RED)
#define LED_OB_MASK   (1u << PIN_LED_ONBOARD)

void led_set(bool on) {
    if (on) sio_hw->gpio_set = LED_MASK;
    else    sio_hw->gpio_clr = LED_MASK;
}

void led_toggle(void) {
    sio_hw->gpio_togl = LED_MASK;
}

void led_red_set(bool on) {
    if (on) sio_hw->gpio_set = LED_RED_MASK;
    else    sio_hw->gpio_clr = LED_RED_MASK;
}

void led_red_toggle(void) {
    sio_hw->gpio_togl = LED_RED_MASK;
}

void led_onboard_toggle(void) {
    sio_hw->gpio_togl = LED_OB_MASK;
}
