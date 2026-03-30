/*
 * Controlador USB en modo dispositivo — implementación bare-metal.
 * Adaptado de pico-examples/usb/device/dev_lowlevel/dev_lowlevel.c
 * (Copyright (c) 2020 Raspberry Pi (Trading) Ltd, licencia BSD-3-Clause)
 *
 * Cambios respecto al original:
 *   - Eliminado todo uso de printf/stdio (no hay UART disponible)
 *   - Añadida API de lectura/escritura para el endpoint bulk de vendor (EP3)
 *   - Número de serie generado desde el ID único de flash
 *   - STALL para peticiones no soportadas
 *   - Escritura en dos pasos de buffer_control (workaround RP2040-E5)
 *   - Soporte multi-paquete en EP0 para descriptores > 64 bytes
 *   - Handlers de endpoints CDC (no-op)
 */

#include "usb_device.h"
#include "usb_common.h"
#include "usb_descriptors.h"
#include "cdc/cdc_rx.h"
#include "cdc/pico_protocol.h"
#include "uart/uart_driver.h"

#include <string.h>
#include "hardware/regs/usb.h"
#include "hardware/structs/usb.h"
#include "hardware/irq.h"
#include "hardware/resets.h"
#include "pico/time.h"

#define usb_hw_set   ((usb_hw_t *)hw_set_alias_untyped(usb_hw))
#define usb_hw_clear ((usb_hw_t *)hw_clear_alias_untyped(usb_hw))

/* ---------------------------------------------------------------------- */
/*  Estado interno del controlador                                         */
/* ---------------------------------------------------------------------- */

static uint8_t  s_pending_addr = 0;   /* 0 = sin pendiente, >0 = dirección a aplicar */

static volatile bool configured = false;

/*
 * Buffer para transferencias EP0 IN multi-paquete.
 * 192 bytes: suficiente para el descriptor de configuración completo (141 bytes).
 * Para cada GET_DESCRIPTOR, copiamos los datos aquí y enviamos en trozos
 * de 64 bytes desde ep0_in_handler hasta agotar ep0_pending_total.
 */
static uint8_t  ep0_pending_buf[192];
static uint16_t ep0_pending_total = 0;   /* bytes totales a enviar */
static uint16_t ep0_pending_sent  = 0;   /* bytes ya enviados */

/* Estado del endpoint CDC Data IN (EP3 IN, 0x83) */
static volatile bool s_cdc_tx_busy = false;

/* Estado del endpoint UART CDC Data IN (EP4 IN, 0x84) */
static volatile bool s_uart_cdc_tx_busy = false;

/* Indica que el siguiente paquete EP0 OUT contiene datos SET_LINE_CODING */
static uint8_t s_set_line_coding_pending = 0xFFu;  /* 0xFF = idle; 0/2 = iface index */

/* ---------------------------------------------------------------------- */
/*  Funciones auxiliares                                                   */
/* ---------------------------------------------------------------------- */

static inline uint32_t usb_buffer_offset(volatile uint8_t *buf) {
    return (uint32_t)buf ^ (uint32_t)usb_dpram;
}

static inline bool ep_is_tx(struct usb_endpoint_configuration *ep) {
    return ep->descriptor->bEndpointAddress & USB_DIR_IN;
}

/* ---------------------------------------------------------------------- */
/*  Configuración de endpoints                                             */
/* ---------------------------------------------------------------------- */

struct usb_endpoint_configuration *usb_get_endpoint_configuration(uint8_t addr) {
    struct usb_endpoint_configuration *endpoints = dev_config.endpoints;
    for (int i = 0; i < USB_EP_COUNT; i++) {
        if (endpoints[i].descriptor &&
            endpoints[i].descriptor->bEndpointAddress == addr) {
            return &endpoints[i];
        }
    }
    return NULL;
}

static void usb_setup_endpoint(const struct usb_endpoint_configuration *ep) {
    if (!ep->endpoint_control) return;
    uint32_t dpram_offset = usb_buffer_offset(ep->data_buffer);
    uint32_t reg = EP_CTRL_ENABLE_BITS
                 | EP_CTRL_INTERRUPT_PER_BUFFER
                 | (ep->descriptor->bmAttributes << EP_CTRL_BUFFER_TYPE_LSB)
                 | dpram_offset;
    *ep->endpoint_control = reg;
}

static void usb_setup_endpoints(void) {
    const struct usb_endpoint_configuration *endpoints = dev_config.endpoints;
    for (int i = 0; i < USB_EP_COUNT; i++) {
        if (endpoints[i].descriptor && endpoints[i].handler) {
            usb_setup_endpoint(&endpoints[i]);
        }
    }
}

/* ---------------------------------------------------------------------- */
/*  Inicio de transferencias                                               */
/* ---------------------------------------------------------------------- */

void usb_start_transfer(struct usb_endpoint_configuration *ep,
                        uint8_t *buf, uint16_t len) {
    uint32_t val = len | USB_BUF_CTRL_AVAIL;

    if (ep_is_tx(ep)) {
        if (len > 0) {
            memcpy((void *)ep->data_buffer, buf, len);
        }
        val |= USB_BUF_CTRL_FULL;
    }

    val |= ep->next_pid ? USB_BUF_CTRL_DATA1_PID : USB_BUF_CTRL_DATA0_PID;
    ep->next_pid ^= 1u;

    /* Workaround RP2040-E5 */
    *ep->buffer_control = val & ~USB_BUF_CTRL_AVAIL;
    busy_wait_at_least_cycles(12);
    *ep->buffer_control = val;
}

static void usb_stall_ep0_in(void);

/* ---------------------------------------------------------------------- */
/*  Transferencias EP0 IN multi-paquete                                   */
/* ---------------------------------------------------------------------- */

/*
 * Inicia una transferencia EP0 IN, posiblemente mayor que 64 bytes.
 * Copia los datos en ep0_pending_buf y envía el primer trozo de 64 bytes.
 * ep0_in_handler continúa enviando trozos hasta completar la transferencia.
 */
static void usb_ep0_in_begin(const uint8_t *data, uint16_t total, uint16_t wLength) {
    if (total > wLength)             total = wLength;
    if (total > sizeof(ep0_pending_buf)) total = sizeof(ep0_pending_buf);

    if (total > 0) memcpy(ep0_pending_buf, data, total);
    ep0_pending_total = total;
    ep0_pending_sent  = 0;

    uint16_t first = (total > 64) ? 64 : total;
    struct usb_endpoint_configuration *ep = usb_get_endpoint_configuration(EP0_IN_ADDR);
    ep->next_pid = 1;
    usb_start_transfer(ep, ep0_pending_buf, first);
    ep0_pending_sent = first;
}

/* ---------------------------------------------------------------------- */
/*  Gestión de descriptores (respuestas a GET_DESCRIPTOR en EP0)          */
/* ---------------------------------------------------------------------- */

static void usb_handle_device_descriptor(volatile struct usb_setup_packet *pkt) {
    usb_ep0_in_begin((const uint8_t *)dev_config.device_descriptor,
                     sizeof(struct usb_device_descriptor),
                     pkt->wLength);
}

static void usb_handle_config_descriptor(volatile struct usb_setup_packet *pkt) {
    usb_ep0_in_begin(dev_config.config_desc_full,
                     dev_config.config_desc_len,
                     pkt->wLength);
}

#define DESCRIPTOR_STRING_COUNT 3

static void usb_handle_string_descriptor(volatile struct usb_setup_packet *pkt) {
    uint8_t i = pkt->wValue & 0xff;
    uint8_t tmp[64];
    uint8_t len = 0;

    if (i == 0) {
        len = 4;
        memcpy(tmp, dev_config.lang_descriptor, len);
    } else if (i <= DESCRIPTOR_STRING_COUNT) {
        len = usb_prepare_string_descriptor(
                  dev_config.descriptor_strings[i - 1], tmp);
    } else {
        usb_stall_ep0_in();
        return;
    }

    usb_ep0_in_begin(tmp, len, pkt->wLength);
}

/* ---------------------------------------------------------------------- */
/*  Gestión de paquetes SETUP                                              */
/* ---------------------------------------------------------------------- */

static void usb_acknowledge_out_request(void) {
    ep0_pending_total = 0;
    ep0_pending_sent  = 0;
    struct usb_endpoint_configuration *ep = usb_get_endpoint_configuration(EP0_IN_ADDR);
    ep->next_pid = 1;
    usb_start_transfer(ep, NULL, 0);
}

static void usb_stall_ep0_in(void) {
    ep0_pending_total = 0;
    ep0_pending_sent  = 0;
    usb_hw_set->ep_stall_arm = USB_EP_STALL_ARM_EP0_IN_BITS;
    struct usb_endpoint_configuration *ep = usb_get_endpoint_configuration(EP0_IN_ADDR);
    *ep->buffer_control = USB_BUF_CTRL_STALL;
}

static void usb_set_device_address(volatile struct usb_setup_packet *pkt) {
    s_pending_addr = (uint8_t)(pkt->wValue & 0xff);
    usb_acknowledge_out_request();
}

static void usb_reset_endpoint_pids(void) {
    struct usb_endpoint_configuration *endpoints = dev_config.endpoints;
    for (int i = 0; i < USB_EP_COUNT; i++) {
        if (endpoints[i].descriptor)
            endpoints[i].next_pid = 0;
    }
}

static void usb_set_device_configuration(volatile struct usb_setup_packet *pkt) {
    (void)pkt;
    usb_acknowledge_out_request();
    configured = true;

    usb_reset_endpoint_pids();
    s_cdc_tx_busy = false;

    /* Armar EP3 OUT (CDC data): el host puede enviar comandos del protocolo */
    usb_start_transfer(usb_get_endpoint_configuration(CDC_EP_DATA_OUT), NULL, 64);

    /* Armar EP4 OUT (UART data): listo para recibir bytes del host */
    usb_start_transfer(usb_get_endpoint_configuration(UART_EP_DATA_OUT), NULL, 64);
}

static void usb_handle_clear_feature(volatile struct usb_setup_packet *pkt) {
    uint8_t recipient = pkt->bmRequestType & USB_REQ_TYPE_RECIPIENT_MASK;

    if (recipient == USB_REQ_TYPE_RECIPIENT_ENDPOINT &&
        pkt->wValue == USB_FEAT_ENDPOINT_HALT) {
        uint8_t ep_addr = (uint8_t)(pkt->wIndex & 0xFF);
        struct usb_endpoint_configuration *ep = usb_get_endpoint_configuration(ep_addr);
        if (ep) {
            ep->next_pid = 0;
            if (!(ep_addr & USB_DIR_IN)) {
                if (ep_addr == CDC_EP_DATA_OUT) {
                    /* CDC data OUT — re-armar para absorber datos */
                    usb_start_transfer(ep, NULL, 64);
                } else if (ep_addr == UART_EP_DATA_OUT) {
                    /* UART data OUT — re-armar para recibir bytes del host */
                    usb_start_transfer(ep, NULL, 64);
                }
            }
        }
    }
    usb_acknowledge_out_request();
}

static void usb_handle_setup_packet(void) {
    volatile struct usb_setup_packet *pkt =
        (volatile struct usb_setup_packet *)&usb_dpram->setup_packet;
    uint8_t req_direction = pkt->bmRequestType;
    uint8_t req = pkt->bRequest;

    usb_get_endpoint_configuration(EP0_IN_ADDR)->next_pid = 1u;

    if (!(req_direction & USB_DIR_IN)) {
        if (req == USB_REQUEST_SET_ADDRESS) {
            usb_set_device_address(pkt);
        } else if (req == USB_REQUEST_SET_CONFIGURATION) {
            usb_set_device_configuration(pkt);
        } else if (req == USB_REQUEST_CLEAR_FEATURE) {
            usb_handle_clear_feature(pkt);
        } else if (req == 0x20 /* SET_LINE_CODING */ && pkt->wLength >= 7) {
            /* Armar EP0 OUT para recibir los 7 bytes; aplicar baud sólo si wIndex==2 */
            s_set_line_coding_pending = (uint8_t)(pkt->wIndex & 0xFFu);
            struct usb_endpoint_configuration *ep0out =
                usb_get_endpoint_configuration(EP0_OUT_ADDR);
            ep0out->next_pid = 1;   /* DATA1: primera DATA tras SETUP (USB spec 8.5.3) */
            usb_start_transfer(ep0out, NULL, 7);
        } else if (req == 0x22 /* SET_CONTROL_LINE_STATE */ &&
                   (pkt->wIndex & 0xFF) == 2) {
            /* bit 0 = DTR. Cuando el host cierra el puerto (DTR=0) resetear
             * completamente EP4 IN/OUT para que el driver termine limpio y
             * la próxima apertura arranque sin estado residual. */
            if (!(pkt->wValue & 0x01u)) {
                s_uart_cdc_tx_busy = false;
                struct usb_endpoint_configuration *ep_uart_in =
                    usb_get_endpoint_configuration(UART_EP_DATA_IN);
                if (ep_uart_in) {
                    *ep_uart_in->buffer_control = 0;
                    ep_uart_in->next_pid = 0;
                }
                /* Re-armar EP4 OUT para que el host pueda cerrar limpiamente */
                struct usb_endpoint_configuration *ep_uart_out =
                    usb_get_endpoint_configuration(UART_EP_DATA_OUT);
                if (ep_uart_out) {
                    ep_uart_out->next_pid = 0;
                    usb_start_transfer(ep_uart_out, NULL, 64);
                }
            }
            usb_acknowledge_out_request();
        } else {
            usb_acknowledge_out_request();
        }
    } else {
        if (req == USB_REQUEST_GET_DESCRIPTOR) {
            uint16_t descriptor_type = pkt->wValue >> 8;
            switch (descriptor_type) {
            case USB_DT_DEVICE:
                usb_handle_device_descriptor(pkt);
                break;
            case USB_DT_CONFIG:
                usb_handle_config_descriptor(pkt);
                break;
            case USB_DT_STRING:
                usb_handle_string_descriptor(pkt);
                break;
            default:
                usb_stall_ep0_in();
                break;
            }
        } else if (req == 0x21 /* GET_LINE_CODING */) {
            /* Responder para ambas interfaces; wIndex!=2 devuelve 115200 8N1 fijo */
            uint32_t baud = ((pkt->wIndex & 0xFFu) == 2u)
                            ? uart_driver_get_baud() : 115200u;
            uint8_t lc[7];
            lc[0] = (uint8_t)(baud);
            lc[1] = (uint8_t)(baud >> 8);
            lc[2] = (uint8_t)(baud >> 16);
            lc[3] = (uint8_t)(baud >> 24);
            lc[4] = 0;   /* 1 stop bit */
            lc[5] = 0;   /* sin paridad */
            lc[6] = 8;   /* 8 bits de datos */
            usb_ep0_in_begin(lc, 7, pkt->wLength);
        } else {
            usb_stall_ep0_in();
        }
    }
}

/* ---------------------------------------------------------------------- */
/*  Gestión del buffer status                                              */
/* ---------------------------------------------------------------------- */

static void usb_handle_ep_buff_done(struct usb_endpoint_configuration *ep) {
    uint32_t buffer_control = *ep->buffer_control;
    uint16_t len = buffer_control & USB_BUF_CTRL_LEN_MASK;
    ep->handler((uint8_t *)ep->data_buffer, len);
}

static void usb_handle_buff_done(uint ep_num, bool in) {
    uint8_t ep_addr = ep_num | (in ? USB_DIR_IN : 0);
    for (uint i = 0; i < USB_EP_COUNT; i++) {
        struct usb_endpoint_configuration *ep = &dev_config.endpoints[i];
        if (ep->descriptor && ep->handler) {
            if (ep->descriptor->bEndpointAddress == ep_addr) {
                usb_handle_ep_buff_done(ep);
                return;
            }
        }
    }
}

static void usb_handle_buff_status(uint32_t buffers) {
    uint32_t remaining = buffers;
    uint32_t bit = 1u;
    for (uint i = 0; remaining && i < 5 * 2; i++) {
        if (remaining & bit) {
            usb_hw_clear->buf_status = bit;
            usb_handle_buff_done(i >> 1u, !(i & 1u));
            remaining &= ~bit;
        }
        bit <<= 1u;
    }
}

/* ---------------------------------------------------------------------- */
/*  Reset de bus                                                           */
/* ---------------------------------------------------------------------- */

static void usb_bus_reset(void) {
    s_pending_addr = 0;
    usb_hw->dev_addr_ctrl = 0;
    configured = false;
    protocol_reset();

    ep0_pending_total = 0;
    ep0_pending_sent  = 0;
    s_cdc_tx_busy     = false;

    /*
     * Limpiar explícitamente el buffer_control de EP3 IN (CDC TX).
     * Si el bus reset llega mientras hay una transferencia en progreso
     * (AVAIL=1), el registro queda con datos obsoletos en el DPRAM.
     * El host recibiría ese byte fantasma antes de que el protocolo
     * esté sincronizado. Escribir 0 elimina tanto AVAIL como FULL.
     */
    struct usb_endpoint_configuration *ep_cdc_in =
        usb_get_endpoint_configuration(CDC_EP_DATA_IN);
    if (ep_cdc_in) *ep_cdc_in->buffer_control = 0;

    struct usb_endpoint_configuration *ep_uart_in =
        usb_get_endpoint_configuration(UART_EP_DATA_IN);
    if (ep_uart_in) *ep_uart_in->buffer_control = 0;
    s_uart_cdc_tx_busy        = false;
    s_set_line_coding_pending = 0xFFu;

    for (int i = 0; i < USB_EP_COUNT; i++) {
        if (dev_config.endpoints[i].descriptor)
            dev_config.endpoints[i].next_pid = 0;
    }
}

/* ---------------------------------------------------------------------- */
/*  Handlers de endpoint                                                   */
/* ---------------------------------------------------------------------- */

void ep0_in_handler(uint8_t *buf, uint16_t len) {
    (void)buf; (void)len;

    if (s_pending_addr) {
        usb_hw->dev_addr_ctrl = s_pending_addr;
        s_pending_addr        = 0;
        ep0_pending_total     = 0;
        ep0_pending_sent      = 0;
        return;
    }

    if (ep0_pending_sent < ep0_pending_total) {
        /* Hay más datos: enviar el siguiente trozo */
        uint16_t remaining = ep0_pending_total - ep0_pending_sent;
        uint16_t chunk     = (remaining > 64) ? 64 : remaining;
        struct usb_endpoint_configuration *ep =
            usb_get_endpoint_configuration(EP0_IN_ADDR);
        usb_start_transfer(ep, &ep0_pending_buf[ep0_pending_sent], chunk);
        ep0_pending_sent += chunk;
    } else {
        /* Transferencia completa: armar EP0 OUT para la fase STATUS.
         * El STATUS ZLP del host para transferencias Control IN usa DATA1. */
        ep0_pending_total = 0;
        ep0_pending_sent  = 0;
        struct usb_endpoint_configuration *ep_out =
            usb_get_endpoint_configuration(EP0_OUT_ADDR);
        ep_out->next_pid = 1;   /* STATUS ZLP usa DATA1 (USB spec 8.5.3) */
        usb_start_transfer(ep_out, NULL, 0);
    }
}

void ep0_out_handler(uint8_t *buf, uint16_t len) {
    if (s_set_line_coding_pending != 0xFFu) {
        /* Aplicar el baudrate solo si recibimos los 7 bytes completos y es
         * la interfaz UART (wIndex==2). Si len < 7 (host malformado) ignoramos
         * los datos pero igual cerramos el control transfer con el STATUS ZLP. */
        if (len >= 7u && s_set_line_coding_pending == 2u) {
            uint32_t baud = (uint32_t)buf[0]
                          | ((uint32_t)buf[1] << 8)
                          | ((uint32_t)buf[2] << 16)
                          | ((uint32_t)buf[3] << 24);
            if (baud > 0) uart_driver_set_baud(baud);
        }
        s_set_line_coding_pending = 0xFFu;
        /* STATUS ZLP — siempre, independientemente de si len >= 7 */
        struct usb_endpoint_configuration *ep =
            usb_get_endpoint_configuration(EP0_IN_ADDR);
        ep->next_pid = 1;
        usb_start_transfer(ep, NULL, 0);
    }
}

/* EP1 IN (0x81): CDC notify — nunca se arma, handler por completitud */
void cdc_notify_in_handler(uint8_t *buf, uint16_t len) {
    (void)buf; (void)len;
}

/* EP3 OUT (0x03): datos CDC del host — almacenar en buffer y re-armar */
void cdc_data_out_handler(uint8_t *buf, uint16_t len) {
    /* Clamp defensivo: el buffer de EP3 OUT en DPRAM tiene 64 bytes.
     * usb_handle_ep_buff_done extrae len del buffer_control (hasta 1023 bits);
     * ante un bitflip de hardware o errata, len podría superar 64 y causar
     * un OOB read en DPRAM al iterar en cdc_rx_push. Mismo patrón que
     * uart_data_out_handler. */
    if (len > 64u) len = 64u;
    cdc_rx_push(buf, len);
    struct usb_endpoint_configuration *ep =
        usb_get_endpoint_configuration(CDC_EP_DATA_OUT);
    if (ep) usb_start_transfer(ep, NULL, 64);
}

/* EP3 IN (0x83): TX CDC completado — marcar como libre */
void cdc_data_in_handler(uint8_t *buf, uint16_t len) {
    (void)buf; (void)len;
    s_cdc_tx_busy = false;
}

/* EP4 IN (0x84): TX UART CDC completado — marcar como libre */
void uart_data_in_handler(uint8_t *buf, uint16_t len) {
    (void)buf; (void)len;
    s_uart_cdc_tx_busy = false;
}

/* Envía datos al host por EP3 IN en chunks de 64 bytes.
 * Bloquea (spin) hasta que cada chunk sea aceptado por el hardware.
 * Las interrupciones USB siguen activas, por lo que s_cdc_tx_busy
 * será borrado por cdc_data_in_handler desde la ISR. */
void cdc_send(const uint8_t *data, uint16_t len) {
    uint16_t off = 0;
    struct usb_endpoint_configuration *ep =
        usb_get_endpoint_configuration(CDC_EP_DATA_IN);
    if (!ep) return;

    while (off < len) {
        /* Esperar a que la transferencia anterior haya completado.
         * Timeout de 50 ms: si el host se desconecta o el bus se resetea
         * mientras esperamos, s_cdc_tx_busy nunca se borra desde la ISR y
         * sin timeout el firmware se congela indefinidamente. */
        uint32_t deadline = time_us_32() + 50000u;
        while (s_cdc_tx_busy) {
            if ((int32_t)(time_us_32() - deadline) >= 0) {
                s_cdc_tx_busy = false;   /* liberar flag y abortar envío */
                return;
            }
        }

        /* Verificar que el USB sigue configurado antes de armar la transferencia.
         * Si llegó un bus reset mientras esperábamos en el spin, configured pasa a
         * false y EP3 IN buffer_control ya fue limpiado por usb_bus_reset(). Armar
         * la transferencia en ese estado dejaría AVAIL=1 en el buffer, causando que
         * el host reciba un paquete fantasma con datos obsoletos al reconectar. */
        if (!configured) return;

        uint16_t chunk = len - off;
        if (chunk > 64u) chunk = 64u;

        s_cdc_tx_busy = true;
        usb_start_transfer(ep, (uint8_t *)(data + off), chunk);
        off += chunk;
    }
}

/* Envía datos al host por EP4 IN en chunks de 64 bytes.
 * Bloquea (spin) hasta que cada chunk sea aceptado por el hardware.
 * Las interrupciones USB siguen activas, por lo que s_uart_cdc_tx_busy
 * será borrado por uart_data_in_handler desde la ISR. */
void uart_cdc_send(const uint8_t *data, uint16_t len) {
    uint16_t off = 0;
    struct usb_endpoint_configuration *ep =
        usb_get_endpoint_configuration(UART_EP_DATA_IN);
    if (!ep) return;

    while (off < len) {
        uint32_t deadline = time_us_32() + 50000u;
        while (s_uart_cdc_tx_busy) {
            if ((int32_t)(time_us_32() - deadline) >= 0) {
                s_uart_cdc_tx_busy = false;
                return;
            }
        }

        /* Misma guardia que cdc_send: abortar si el bus se resetó mientras
         * esperábamos, para no dejar AVAIL=1 en EP4 IN con datos obsoletos. */
        if (!configured) return;

        uint16_t chunk = len - off;
        if (chunk > 64u) chunk = 64u;

        s_uart_cdc_tx_busy = true;
        usb_start_transfer(ep, (uint8_t *)(data + off), chunk);
        off += chunk;
    }
}

/* ---------------------------------------------------------------------- */
/*  Rutina de servicio de interrupción USB                                 */
/* ---------------------------------------------------------------------- */

static void isr_usbctrl(void) {
    uint32_t status = usb_hw->ints;

    if (status & USB_INTS_SETUP_REQ_BITS) {
        usb_hw_clear->sie_status = USB_SIE_STATUS_SETUP_REC_BITS;
        /* Limpiar cualquier BUFF_STATUS residual de EP0 antes de procesar el
         * nuevo SETUP packet. Cuando el hardware cancela una transferencia EP0
         * pendiente (OUT armado para 0 bytes desde ep0_in_handler) al recibir
         * SETUP, setea buf_status[EP0_OUT]. Si no limpiamos ese bit aquí,
         * usb_handle_buff_status lo procesará DESPUÉS de que usb_handle_setup_packet
         * haya armado EP0 OUT para 7 bytes (DATA phase del nuevo SET_LINE_CODING),
         * lo que lleva a ep0_out_handler(old_buf, 7) con datos obsoletos — falsa
         * completitud del DATA phase, STATUS ZLP prematuro y pérdida del comando. */
        usb_hw_clear->buf_status = 0x3u;   /* EP0 IN (bit 0) y EP0 OUT (bit 1) */
        usb_handle_setup_packet();
    }

    if (status & USB_INTS_BUFF_STATUS_BITS) {
        usb_handle_buff_status(usb_hw->buf_status);
    }

    if (status & USB_INTS_BUS_RESET_BITS) {
        usb_hw_clear->sie_status = USB_SIE_STATUS_BUS_RESET_BITS;
        usb_bus_reset();
    }
}

/* ---------------------------------------------------------------------- */
/*  API pública                                                            */
/* ---------------------------------------------------------------------- */

void usb_device_init(void) {
    usb_descriptors_init();

    reset_block(RESETS_RESET_USBCTRL_BITS);
    unreset_block_wait(RESETS_RESET_USBCTRL_BITS);

    memset(usb_dpram, 0, sizeof(*usb_dpram));

    usb_hw->muxing = USB_USB_MUXING_TO_PHY_BITS
                   | USB_USB_MUXING_SOFTCON_BITS;

    usb_hw->pwr = USB_USB_PWR_VBUS_DETECT_BITS
                | USB_USB_PWR_VBUS_DETECT_OVERRIDE_EN_BITS;

    usb_hw->main_ctrl = USB_MAIN_CTRL_CONTROLLER_EN_BITS;

    usb_hw->sie_ctrl = USB_SIE_CTRL_EP0_INT_1BUF_BITS;

    usb_hw->inte = USB_INTS_BUFF_STATUS_BITS
                 | USB_INTS_BUS_RESET_BITS
                 | USB_INTS_SETUP_REQ_BITS;

    usb_setup_endpoints();

    irq_set_exclusive_handler(USBCTRL_IRQ, isr_usbctrl);
    irq_set_enabled(USBCTRL_IRQ, true);

    usb_hw_set->sie_ctrl = USB_SIE_CTRL_PULLUP_EN_BITS;
}

bool usb_is_configured(void) {
    return configured;
}

