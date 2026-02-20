#pragma once

/* Initialize XOSC, PLLs, and clock muxes.
 * After return: clk_sys = 125 MHz, clk_usb = 48 MHz. */
void clock_init(void);
