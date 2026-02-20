#include "clock_init.h"
#include "board_config.h"

#include <stdint.h>
#include "hardware/structs/xosc.h"
#include "hardware/structs/pll.h"
#include "hardware/structs/clocks.h"
#include "hardware/structs/resets.h"
#include "hardware/regs/xosc.h"
#include "hardware/regs/pll.h"
#include "hardware/regs/clocks.h"
#include "hardware/regs/resets.h"

/* ---------------------------------------------------------------------- */
/*  Funciones auxiliares                                                   */
/* ---------------------------------------------------------------------- */

/* Pone uno o varios periféricos en reset. */
static void reset_block(uint32_t bits) {
    hw_set_bits_raw(&resets_hw->reset, bits);
}

/* Saca uno o varios periféricos de reset y espera hasta que estén listos. */
static void unreset_block_wait(uint32_t bits) {
    hw_clear_bits_raw(&resets_hw->reset, bits);
    while ((resets_hw->reset_done & bits) != bits)
        ;
}

/* Arranca el oscilador de cristal de 12 MHz. */
static void xosc_start(void) {
    xosc_hw->ctrl = XOSC_CTRL_FREQ_RANGE_VALUE_1_15MHZ;
    xosc_hw->startup = 47;  /* ~1 ms de estabilización a 12 MHz / 256 */
    hw_set_bits_raw(&xosc_hw->ctrl,
                    XOSC_CTRL_ENABLE_VALUE_ENABLE << XOSC_CTRL_ENABLE_LSB);
    while (!(xosc_hw->status & XOSC_STATUS_STABLE_BITS))
        ;
}

/*
 * Configura y arranca un PLL.
 * Frecuencia de salida = (XOSC_HZ / refdiv) * fbdiv / post1 / post2
 *
 * El proceso es: reset → programar divisores → encender VCO → esperar lock
 * → encender post-divisores. Separar el encendido del VCO y los
 * post-divisores es un requisito del datasheet del RP2040 (§2.18.2).
 */
static void pll_start(pll_hw_t *pll, uint32_t rst_bit,
                      uint32_t refdiv, uint32_t fbdiv,
                      uint32_t post1, uint32_t post2) {
    reset_block(rst_bit);
    unreset_block_wait(rst_bit);

    pll->cs = refdiv;
    pll->fbdiv_int = fbdiv;

    /*
     * Los bits del registro PWR son "power down" (1 = apagado).
     * Encendemos el núcleo del PLL y el VCO (PD=0, VCOPD=0),
     * pero mantenemos los post-divisores apagados hasta que el VCO esté estable.
     */
    pll->pwr = PLL_PWR_DSMPD_BITS | PLL_PWR_POSTDIVPD_BITS;

    while (!(pll->cs & PLL_CS_LOCK_BITS))
        ;

    /* Una vez que el VCO está estable, configurar y encender los post-divisores */
    pll->prim = (post1 << PLL_PRIM_POSTDIV1_LSB) |
                (post2 << PLL_PRIM_POSTDIV2_LSB);
    hw_clear_bits_raw(&pll->pwr, PLL_PWR_POSTDIVPD_BITS);
}

/* ---------------------------------------------------------------------- */
/*  Inicialización de relojes                                              */
/* ---------------------------------------------------------------------- */

void clock_init(void) {
    /* 1. Arrancar el oscilador de cristal de 12 MHz */
    xosc_start();

    /* 2. Cambiar CLK_REF de ROSC a XOSC.
     *    SRC = 2 corresponde a xosc_clksrc (datasheet RP2040 §2.15.7). */
    clocks_hw->clk[clk_ref].ctrl = 2u;
    while (!(clocks_hw->clk[clk_ref].selected & (1u << 2)))
        ;

    /* 3. Mover CLK_SYS a CLK_REF (mux sin glitch, SRC=0) antes de
     *    reconfigurar PLL_SYS para evitar un glitch en el reloj del sistema. */
    hw_clear_bits_raw(&clocks_hw->clk[clk_sys].ctrl,
                      CLOCKS_CLK_SYS_CTRL_SRC_BITS);
    while (!(clocks_hw->clk[clk_sys].selected & 1u))
        ;

    /* 4. PLL_SYS: 12 MHz × 125 / 6 / 2 = 125 MHz */
    pll_start(pll_sys_hw, RESETS_RESET_PLL_SYS_BITS, 1, 125, 6, 2);

    /* 5. PLL_USB: 12 MHz × 100 / 5 / 5 = 48 MHz */
    pll_start(pll_usb_hw, RESETS_RESET_PLL_USB_BITS, 1, 100, 5, 5);

    /* 6. CLK_SYS = PLL_SYS a través del mux auxiliar.
     *    AUXSRC = 0 (clksrc_pll_sys), luego SRC = 1 (clksrc_clk_sys_aux). */
    clocks_hw->clk[clk_sys].div  = 1u << 8;  /* divisor entero = 1 */
    clocks_hw->clk[clk_sys].ctrl = 0;        /* AUXSRC → PLL_SYS */
    hw_set_bits_raw(&clocks_hw->clk[clk_sys].ctrl,
                    CLOCKS_CLK_SYS_CTRL_SRC_BITS);
    while (!(clocks_hw->clk[clk_sys].selected & 2u))
        ;

    /* 7. CLK_USB = 48 MHz desde PLL_USB */
    clocks_hw->clk[clk_usb].div  = 1u << 8;
    clocks_hw->clk[clk_usb].ctrl = CLOCKS_CLK_USB_CTRL_ENABLE_BITS;

    /* 8. CLK_PERI = CLK_SYS (para UART, SPI, etc.) */
    clocks_hw->clk[clk_peri].ctrl = CLOCKS_CLK_PERI_CTRL_ENABLE_BITS;

    /* 9. CLK_ADC = 48 MHz desde PLL_USB */
    clocks_hw->clk[clk_adc].div  = 1u << 8;
    clocks_hw->clk[clk_adc].ctrl = CLOCKS_CLK_ADC_CTRL_ENABLE_BITS;
}
