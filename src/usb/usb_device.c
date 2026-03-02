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

static bool     should_set_address = false;
static uint8_t  dev_addr = 0;

static volatile bool configured = false;
static volatile bool enumerated = false;

/*
 * Buffer para transferencias EP0 IN multi-paquete.
 * 128 bytes: suficiente para el descriptor de configuración completo (98 bytes).
 * Para cada GET_DESCRIPTOR, copiamos los datos aquí y enviamos en trozos
 * de 64 bytes desde ep0_in_handler hasta agotar ep0_pending_total.
 */
static uint8_t  ep0_pending_buf[128];
static uint16_t ep0_pending_total = 0;   /* bytes totales a enviar */
static uint16_t ep0_pending_sent  = 0;   /* bytes ya enviados */

/* Estado del endpoint bulk de vendor (EP3) */
static volatile bool     vendor_rx_ready = false;
static volatile uint16_t vendor_rx_len   = 0;
static uint8_t           vendor_rx_buf[64];
static volatile bool     vendor_tx_busy  = false;

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
    for (int i = 0; i < USB_NUM_ENDPOINTS; i++) {
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
    for (int i = 0; i < USB_NUM_ENDPOINTS; i++) {
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
    dev_addr = (uint8_t)(pkt->wValue & 0xff);
    should_set_address = true;
    usb_acknowledge_out_request();
}

static void usb_reset_endpoint_pids(void) {
    struct usb_endpoint_configuration *endpoints = dev_config.endpoints;
    for (int i = 0; i < USB_NUM_ENDPOINTS; i++) {
        if (endpoints[i].descriptor)
            endpoints[i].next_pid = 0;
    }
}

static void usb_set_device_configuration(volatile struct usb_setup_packet *pkt) {
    (void)pkt;
    usb_acknowledge_out_request();
    configured = true;

    usb_reset_endpoint_pids();
    vendor_rx_ready = false;
    vendor_tx_busy  = false;

    /* Armar EP3 OUT (J-Link): el host puede enviar el primer comando */
    usb_start_transfer(usb_get_endpoint_configuration(EP1_OUT_ADDR), NULL, 64);

    /* Armar EP1 OUT (CDC data): listo para absorber datos del host */
    usb_start_transfer(usb_get_endpoint_configuration(CDC_EP_DATA_OUT), NULL, 64);
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
                if (ep_addr == EP1_OUT_ADDR) {
                    /* J-Link OUT */
                    vendor_rx_ready = false;
                    usb_start_transfer(ep, NULL, 64);
                } else if (ep_addr == CDC_EP_DATA_OUT) {
                    /* CDC data OUT — re-armar para absorber datos */
                    usb_start_transfer(ep, NULL, 64);
                }
            } else {
                if (ep_addr == EP2_IN_ADDR) {
                    vendor_tx_busy = false;
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

    /* Petición de vendor para el descriptor MS OS 2.0 */
    if ((req_direction & USB_DIR_IN) &&
        (req_direction & USB_REQ_TYPE_TYPE_MASK) == USB_REQ_TYPE_TYPE_VENDOR &&
        req == MS_OS_20_VENDOR_CODE &&
        pkt->wIndex == 0x07) {
        usb_ep0_in_begin(ms_os_20_descriptor_set,
                         ms_os_20_descriptor_set_len,
                         pkt->wLength);
        return;
    }

    if (!(req_direction & USB_DIR_IN)) {
        if (req == USB_REQUEST_SET_ADDRESS) {
            usb_set_device_address(pkt);
        } else if (req == USB_REQUEST_SET_CONFIGURATION) {
            usb_set_device_configuration(pkt);
        } else if (req == USB_REQUEST_CLEAR_FEATURE) {
            usb_handle_clear_feature(pkt);
        } else {
            /* Peticiones de clase CDC (SET_LINE_CODING, SET_CONTROL_LINE_STATE…)
             * y cualquier otro OUT desconocido → ACK con ZLP */
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
            case USB_DT_BOS:
                usb_ep0_in_begin(bos_descriptor, bos_descriptor_len, pkt->wLength);
                break;
            default:
                usb_stall_ep0_in();
                break;
            }
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
    for (uint i = 0; i < USB_NUM_ENDPOINTS; i++) {
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
    for (uint i = 0; remaining && i < USB_NUM_ENDPOINTS * 2; i++) {
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

static volatile uint32_t bus_reset_count = 0;

static void usb_bus_reset(void) {
    bus_reset_count++;
    dev_addr = 0;
    should_set_address = false;
    usb_hw->dev_addr_ctrl = 0;
    configured = false;

    ep0_pending_total = 0;
    ep0_pending_sent  = 0;
    vendor_rx_ready   = false;
    vendor_tx_busy    = false;

    for (int i = 0; i < USB_NUM_ENDPOINTS; i++) {
        if (dev_config.endpoints[i].descriptor)
            dev_config.endpoints[i].next_pid = 0;
    }
}

uint32_t usb_get_bus_reset_count(void) {
    return bus_reset_count;
}

/* ---------------------------------------------------------------------- */
/*  Handlers de endpoint                                                   */
/* ---------------------------------------------------------------------- */

void ep0_in_handler(uint8_t *buf, uint16_t len) {
    (void)buf; (void)len;

    if (should_set_address) {
        usb_hw->dev_addr_ctrl = dev_addr;
        should_set_address    = false;
        enumerated            = true;
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
        /* Transferencia completa: armar EP0 OUT para la fase STATUS */
        ep0_pending_total = 0;
        ep0_pending_sent  = 0;
        usb_start_transfer(usb_get_endpoint_configuration(EP0_OUT_ADDR), NULL, 0);
    }
}

void ep0_out_handler(uint8_t *buf, uint16_t len) {
    (void)buf; (void)len;
}

/* EP3 OUT (0x03): el host envía un comando J-Link */
void ep1_out_handler(uint8_t *buf, uint16_t len) {
    if (len > 64) len = 64;
    memcpy(vendor_rx_buf, buf, len);
    vendor_rx_len   = len;
    vendor_rx_ready = true;
}

/* EP3 IN (0x83): la respuesta J-Link se ha enviado al host */
void ep2_in_handler(uint8_t *buf, uint16_t len) {
    (void)buf; (void)len;
    vendor_tx_busy = false;
}

/* EP1 IN (0x81): CDC notificaciones — nunca se arma, handler por completitud */
void cdc_notify_in_handler(uint8_t *buf, uint16_t len) {
    (void)buf; (void)len;
}

/* EP1 OUT (0x01): datos CDC del host — absorber y re-armar */
void cdc_data_out_handler(uint8_t *buf, uint16_t len) {
    (void)buf; (void)len;
    /* Descartar los datos y re-armar para que el host no se bloquee */
    struct usb_endpoint_configuration *ep =
        usb_get_endpoint_configuration(CDC_EP_DATA_OUT);
    if (ep) usb_start_transfer(ep, NULL, 64);
}

/* EP2 IN (0x82): datos CDC al host — nunca se arma, handler por completitud */
void cdc_data_in_handler(uint8_t *buf, uint16_t len) {
    (void)buf; (void)len;
}

/* ---------------------------------------------------------------------- */
/*  Rutina de servicio de interrupción USB                                 */
/* ---------------------------------------------------------------------- */

static void isr_usbctrl(void) {
    uint32_t status = usb_hw->ints;

    if (status & USB_INTS_SETUP_REQ_BITS) {
        usb_hw_clear->sie_status = USB_SIE_STATUS_SETUP_REC_BITS;
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

void usb_device_task(void) {
    /* Todos los eventos USB se gestionan en isr_usbctrl() */
}

bool usb_is_configured(void) {
    return configured;
}

bool usb_is_enumerated(void) {
    return enumerated;
}

uint16_t usb_vendor_read(uint8_t *buf, uint16_t max_len) {
    if (!vendor_rx_ready) return 0;

    uint16_t len = vendor_rx_len;
    if (len > max_len) len = max_len;
    memcpy(buf, vendor_rx_buf, len);

    vendor_rx_ready = false;
    vendor_rx_len   = 0;

    return len;
}

void usb_vendor_arm_rx(void) {
    usb_start_transfer(usb_get_endpoint_configuration(EP1_OUT_ADDR), NULL, 64);
}

bool usb_vendor_write(const uint8_t *data, uint16_t len) {
    if (vendor_tx_busy) {
        struct usb_endpoint_configuration *ep =
            usb_get_endpoint_configuration(EP2_IN_ADDR);
        uint32_t bc = *ep->buffer_control;
        if (!(bc & USB_BUF_CTRL_AVAIL)) {
            vendor_tx_busy = false;
        } else {
            return false;
        }
    }
    if (len > 64) len = 64;

    vendor_tx_busy = true;
    usb_start_transfer(usb_get_endpoint_configuration(EP2_IN_ADDR),
                       (uint8_t *)data, len);
    return true;
}

bool usb_vendor_tx_busy(void) {
    if (!vendor_tx_busy) return false;

    struct usb_endpoint_configuration *ep =
        usb_get_endpoint_configuration(EP2_IN_ADDR);
    uint32_t bc = *ep->buffer_control;
    if (!(bc & USB_BUF_CTRL_AVAIL)) {
        vendor_tx_busy = false;
        return false;
    }
    return true;
}
