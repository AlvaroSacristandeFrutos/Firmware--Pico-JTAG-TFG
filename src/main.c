/*
 * Fase B — Protocolo J-Link sobre WinUSB
 *
 * El firmware procesa comandos J-Link reales pero sigue usando el VID/PID
 * propio (0x1209:0xD0DB) con WinUSB, sin depender del driver jlink.sys.
 * El script tools/test_jlink.mjs envía la secuencia de inicialización que
 * usaría JLink_x64.dll y verifica que cada respuesta es correcta.
 *
 * Objetivo: confirmar que el protocolo J-Link está correctamente implementado
 * antes de volver a intentar la integración con el driver de Segger.
 */
 
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "board/board_config.h"
#include "board/gpio_init.h"
#include "util/led.h"
#include "util/adc.h"
#include "usb/usb_device.h"
#include "jlink/jlink_handler.h"
#include "jlink/jlink_protocol.h"
#include "jlink/jlink_caps.h"
#include "jtag/jtag_pio.h"

#include "hardware/regs/addressmap.h"
#define TIMER_TIMELR    (*(volatile uint32_t *)(TIMER_BASE + 0x0Cu))

static uint32_t time_us(void) {
    return TIMER_TIMELR;
}

static void delay_ms(uint32_t ms) {
    uint32_t start = time_us();
    while ((time_us() - start) < ms * 1000u)
        ;
}

static void led_blink_n(uint32_t n) {
    for (uint32_t i = 0; i < n; i++) {
        led_set(true);  delay_ms(200);
        led_set(false); delay_ms(200);
    }
}

/*
 * Diagnóstico de inactividad — muestra el ÚLTIMO BYTE de comando recibido
 * como dos grupos de destellos, para identificar exactamente qué envía
 * jlink.sys antes de detenerse, sin necesidad de UART:
 *
 *   Grupo 1 (LENTO, 600 ms/destello): nibble alto (bits 7-4)
 *   Pausa de 1.5 s
 *   Grupo 2 (RÁPIDO, 300 ms/destello): nibble bajo (bits 3-0)
 *   Pausa de 3 s → repetir
 *
 * Si el nibble vale 0, se muestran 16 destellos (0 destellos sería invisible).
 *
 * Ejemplos:
 *   0xF2 (READ_CONFIG)  → 15 lentos  | 2 rápidos
 *   0xF3 (WRITE_CONFIG) → 15 lentos  | 3 rápidos
 *   0xCF (HW_JTAG3)     → 12 lentos  | 15 rápidos
 *   0xDC (HW_RESET0)    → 13 lentos  | 12 rápidos
 *   0x07 (GET_STATE)    → 16 lentos  | 7 rápidos  (nibble alto = 0 → 16)
 *   0x00 (dato config)  → 16 lentos  | 16 rápidos (no es un comando real)
 */
static void led_diagnostic_tick(uint32_t cmd_count, uint8_t last_byte) {
    (void)cmd_count;

    uint32_t hi = (last_byte >> 4) & 0xF; if (!hi) hi = 16;
    uint32_t lo =  last_byte       & 0xF; if (!lo) lo = 16;

    const uint32_t slow = 600000u;   /* 300 ms ON + 300 ms OFF */
    const uint32_t fast = 300000u;   /* 150 ms ON + 150 ms OFF */
    const uint32_t gap1 = 1500000u;  /* pausa entre nibbles */
    const uint32_t gap2 = 3000000u;  /* pausa final antes de repetir */

    uint32_t p1    = hi * slow;
    uint32_t p2    = lo * fast;
    uint32_t total = p1 + gap1 + p2 + gap2;
    uint32_t pos   = time_us() % total;

    if (pos < p1)
        led_set((pos % slow) < 300000u);
    else if (pos < p1 + gap1)
        led_set(false);
    else if (pos < p1 + gap1 + p2)
        led_set(((pos - p1 - gap1) % fast) < 150000u);
    else
        led_set(false);
}

/* Buffers para un ciclo de comando/respuesta (protocolo J-Link es síncrono) */
static uint8_t rx_data[CMD_BUF_SIZE];
static uint8_t tx_data[TX_BUF_SIZE];

/* --------------------------------------------------------------------------
 * Punto de entrada (Core 0)
 * -------------------------------------------------------------------------- */
int main(void) {
    gpio_init_all();
    led_init();
    adc_sense_init();
    jtag_pio_init();
    usb_device_init();

    /* Esperar configuración USB — LED parpadea sin bloquear para que el
     * polling sea lo más rápido posible (USB es interrupt-driven).
     *
     * jlink.sys lee las respuestas J-Link de EP2 IN (bulk), no de EP1 IN
     * (interrupt). EP1 IN sólo debe existir en el descriptor para que
     * jlink.sys entre en modo de comandos bulk; no necesita datos.
     */
    bool did_enumerate = false;
    {
        uint32_t enum_start  = time_us();
        uint32_t last_blink  = enum_start;
        bool     blink_state = false;
        while ((time_us() - enum_start) < 15000000u) {
            if (usb_is_configured()) {
                did_enumerate = true;
                break;
            }
            /* Parpadeo ~4 Hz sin delay bloqueante */
            if ((time_us() - last_blink) >= 250000u) {
                blink_state = !blink_state;
                led_set(blink_state);
                last_blink += 250000u;
            }
        }
    }

    if (!did_enumerate) {
        while (1) {
            uint32_t resets = usb_get_bus_reset_count();
            led_blink_n(resets > 0 ? resets : 1);
            led_set(true);  delay_ms(2000);
            led_set(false); delay_ms(1000);
        }
    }

    led_set(true);  /* enumeración OK */

    uint32_t cmd_count           = 0;
    uint8_t  first_cmd_byte      = 0;
    uint8_t  last_cmd_byte       = 0;
    uint32_t bus_resets_at_start = usb_get_bus_reset_count();
    uint32_t last_activity_us    = time_us();

    /*
     * Estado para acumulación multi-paquete de WRITE_CONFIG (0xF3).
     * El comando completo mide 1 (cmd) + 256 (datos) = 257 bytes → 5 paquetes USB.
     * write_cfg_active se activa al recibir 0xF3 y desactiva cuando acumulamos 256 bytes.
     */
    static uint8_t  write_cfg_buf[256];
    static uint16_t write_cfg_accum  = 0;
    static bool     write_cfg_active = false;

    /*
     * Estado para acumulación multi-paquete de HW_JTAG3 (0xCF).
     * Formato: cmd(1) + dummy(1) + numBits(2 LE) + TMS[N] + TDI[N].
     * Total = 4 + 2*ceil(numBits/8) bytes, variable — puede superar los 64 bytes
     * de un paquete USB FS. main.c acumula en jtag3_buf hasta tener jtag3_total bytes,
     * luego despacha el comando completo a jlink_handle_command().
     */
    static uint8_t  jtag3_buf[CMD_BUF_SIZE];
    static uint32_t jtag3_total  = 0;
    static uint16_t jtag3_accum  = 0;
    static bool     jtag3_active = false;

    /* --------------------------------------------------------------------------
     * Bucle principal de comandos J-Link
     * -------------------------------------------------------------------------- */
    while (1) {
        usb_device_task();

        uint8_t pkt[64];
        uint16_t len = usb_vendor_read(pkt, sizeof(pkt));

        if (len > 0) {
            last_activity_us = time_us();
            cmd_count++;
            if (cmd_count == 1) first_cmd_byte = pkt[0];

            if (jtag3_active) {
                /*
                 * Paquetes de datos de HW_JTAG3: acumular en jtag3_buf
                 * hasta completar jtag3_total bytes, luego despachar.
                 */
                last_cmd_byte = EMU_CMD_HW_JTAG3;

                uint16_t space = (uint16_t)(jtag3_total - jtag3_accum);
                if (len < space) space = len;
                memcpy(jtag3_buf + jtag3_accum, pkt, space);
                jtag3_accum += space;

                if (jtag3_accum >= (uint16_t)jtag3_total) {
                    /* Comando completo — despachar */
                    uint16_t tx_len = 0;
                    jlink_handle_command(jtag3_buf, (uint16_t)jtag3_total,
                                        tx_data, &tx_len);
                    jtag3_active = false;
                    jtag3_total  = 0;
                    jtag3_accum  = 0;

                    if (tx_len > 0) {
                        uint16_t offset = 0;
                        while (offset < tx_len) {
                            uint16_t chunk = tx_len - offset;
                            if (chunk > 64) chunk = 64;
                            if (usb_vendor_write(&tx_data[offset], chunk))
                                offset += chunk;
                            usb_device_task();
                        }
                        uint32_t wait_start = time_us();
                        while ((time_us() - wait_start) < 50000u) {
                            if (!usb_vendor_tx_busy()) break;
                            usb_device_task();
                        }
                    }
                    usb_vendor_arm_rx();
                } else {
                    usb_vendor_arm_rx();
                }

            } else if (write_cfg_active) {
                /*
                 * Paquetes de datos de WRITE_CONFIG: acumular en write_cfg_buf.
                 * No se genera respuesta; simplemente seguimos leyendo.
                 */
                last_cmd_byte = 0xF3; /* seguimos en contexto WRITE_CONFIG */

                uint16_t space = 256 - write_cfg_accum;
                uint16_t copy  = (len < space) ? (uint16_t)len : space;
                memcpy(write_cfg_buf + write_cfg_accum, pkt, copy);
                write_cfg_accum += copy;

                if (write_cfg_accum >= 256) {
                    jlink_config_update(write_cfg_buf, 256);
                    write_cfg_active = false;
                    write_cfg_accum  = 0;
                }

                usb_vendor_arm_rx();
            } else {
                last_cmd_byte = pkt[0];
                memcpy(rx_data, pkt, len);

                /*
                 * HW_JTAG3 puede ocupar múltiples paquetes USB si TMS+TDI
                 * no caben en los 60 bytes restantes del primer paquete.
                 * Si el payload completo (4 + 2*N bytes) no ha llegado aún,
                 * iniciamos la acumulación y esperamos paquetes adicionales.
                 */
                if (pkt[0] == EMU_CMD_HW_JTAG3 && len >= 4) {
                    uint32_t nb       = (uint32_t)pkt[2] | ((uint32_t)pkt[3] << 8);
                    uint32_t nb_bytes = (nb + 7u) / 8u;
                    uint32_t total    = 4u + 2u * nb_bytes;
                    if (total > (uint32_t)len && total <= CMD_BUF_SIZE) {
                        memcpy(jtag3_buf, pkt, len);
                        jtag3_accum  = (uint16_t)len;
                        jtag3_total  = total;
                        jtag3_active = true;
                        usb_vendor_arm_rx();
                        continue; /* esperar paquetes restantes */
                    }
                }

                uint16_t tx_len = 0;
                jlink_handle_command(rx_data, len, tx_data, &tx_len);

                /*
                 * Si el comando fue WRITE_CONFIG, los datos llegan en paquetes
                 * subsiguientes.  El primer paquete ya contiene len-1 bytes de datos
                 * (después del byte de comando).
                 */
                if (pkt[0] == EMU_CMD_WRITE_CONFIG) {
                    write_cfg_active = true;
                    write_cfg_accum  = 0;
                    if (len > 1) {
                        uint16_t data_in_first = len - 1;
                        if (data_in_first > 256) data_in_first = 256;
                        memcpy(write_cfg_buf, pkt + 1, data_in_first);
                        write_cfg_accum = data_in_first;
                        if (write_cfg_accum >= 256) {
                            jlink_config_update(write_cfg_buf, 256);
                            write_cfg_active = false;
                            write_cfg_accum  = 0;
                        }
                    }
                }

                if (tx_len > 0) {
                    /* Enviar en trozos de 64 bytes (máximo paquete bulk) */
                    uint16_t offset = 0;
                    while (offset < tx_len) {
                        uint16_t chunk = tx_len - offset;
                        if (chunk > 64) chunk = 64;
                        if (usb_vendor_write(&tx_data[offset], chunk))
                            offset += chunk;
                        usb_device_task();
                    }

                    /* Esperar hasta 50 ms a que jlink.sys lea la respuesta.
                     * USB FS bulk: el host consume los datos en <1 ms; 50 ms
                     * es suficiente margen sin bloquear el bucle de comandos. */
                    {
                        uint32_t wait_start = time_us();
                        while ((time_us() - wait_start) < 50000u) {
                            if (!usb_vendor_tx_busy()) break;
                            usb_device_task();
                        }
                    }

                    /*
                     * EMU_CMD_VERSION requiere una segunda transferencia:
                     * tras los 2 bytes de longitud, jlink.sys emite un nuevo
                     * URB de lectura para VERSION_BODY_LEN (112) bytes.
                     * Enviamos el body inmediatamente, antes de armar RX.
                     */
                    if (rx_data[0] == EMU_CMD_VERSION) {
                        uint16_t body_len = 0;
                        jlink_cmd_version_body(tx_data, &body_len);
                        uint16_t off2 = 0;
                        while (off2 < body_len) {
                            uint16_t chunk = body_len - off2;
                            if (chunk > 64) chunk = 64;
                            if (usb_vendor_write(&tx_data[off2], chunk))
                                off2 += chunk;
                            usb_device_task();
                        }
                        uint32_t w2 = time_us();
                        while ((time_us() - w2) < 50000u) {
                            if (!usb_vendor_tx_busy()) break;
                            usb_device_task();
                        }
                    }

                    /*
                     * EMU_CMD_IDSEGGER también requiere una segunda transferencia:
                     * tras los 4 bytes de cabecera [00 09 00 00], jlink.sys emite
                     * un nuevo URB de lectura para 2304 bytes de volcado de licencias.
                     * El sub-comando (rx_data[1]) determina qué bloque enviar:
                     *   0x02 → tabla de módulos (RDI, GDB, JFlash, …)
                     *   0x00 → bloque de versión/modelo
                     */
                    if (rx_data[0] == EMU_CMD_IDSEGGER) {
                        uint16_t body_len = 0;
                        jlink_cmd_idsegger_body(rx_data[1], tx_data, &body_len);
                        uint16_t off3 = 0;
                        while (off3 < body_len) {
                            uint16_t chunk = body_len - off3;
                            if (chunk > 64) chunk = 64;
                            if (usb_vendor_write(&tx_data[off3], chunk))
                                off3 += chunk;
                            usb_device_task();
                        }
                        uint32_t w3 = time_us();
                        while ((time_us() - w3) < 50000u) {
                            if (!usb_vendor_tx_busy()) break;
                            usb_device_task();
                        }
                    }
                }

                usb_vendor_arm_rx();
            }
        }

        /* Diagnóstico no bloqueante: tras 5 s sin comandos, muestra
         * cmd_count como N destellos para identificar el último paso
         * del saludo J-Link que se procesó. */
        if (cmd_count > 0 && (time_us() - last_activity_us) > 5000000u) {
            led_diagnostic_tick(cmd_count, last_cmd_byte);
        }
    }
}
