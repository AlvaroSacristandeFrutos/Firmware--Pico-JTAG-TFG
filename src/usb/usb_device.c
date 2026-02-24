/*
 * Controlador USB en modo dispositivo — implementación bare-metal.
 * Adaptado de pico-examples/usb/device/dev_lowlevel/dev_lowlevel.c
 * (Copyright (c) 2020 Raspberry Pi (Trading) Ltd, licencia BSD-3-Clause)
 *
 * Cambios respecto al original:
 *   - Eliminado todo uso de printf/stdio (no hay UART disponible)
 *   - Añadida API de lectura/escritura para el endpoint bulk de vendor (EP1)
 *   - Número de serie generado desde el ID único de flash
 *   - STALL para peticiones no soportadas
 *   - Escritura en dos pasos de buffer_control (workaround RP2040-E5)
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

/* Alias de los registros USB para escritura atómica (set/clear por separado) */
#define usb_hw_set   ((usb_hw_t *)hw_set_alias_untyped(usb_hw))
#define usb_hw_clear ((usb_hw_t *)hw_clear_alias_untyped(usb_hw))

/* ---------------------------------------------------------------------- */
/*  Estado interno del controlador                                         */
/* ---------------------------------------------------------------------- */

/* Dirección USB — se aplica después de que EP0 IN confirme el ACK */
static bool     should_set_address = false;
static uint8_t  dev_addr = 0;

static volatile bool configured = false;
static volatile bool enumerated = false;   /* true tras aplicar SET_ADDRESS */

/* Buffer temporal para ensamblar respuestas de EP0 (descriptores, etc.) */
static uint8_t ep0_buf[64];

/* Estado del endpoint bulk de vendor (EP1) */
static volatile bool     vendor_rx_ready = false;
static volatile uint16_t vendor_rx_len   = 0;
static uint8_t           vendor_rx_buf[64];
static volatile bool     vendor_tx_busy  = false;

/* ---------------------------------------------------------------------- */
/*  Funciones auxiliares                                                   */
/* ---------------------------------------------------------------------- */

/* Convierte un puntero a la DPRAM en un offset desde la base de la DPRAM.
 * El hardware necesita offsets, no direcciones absolutas. */
static inline uint32_t usb_buffer_offset(volatile uint8_t *buf) {
    return (uint32_t)buf ^ (uint32_t)usb_dpram;
}

/* Devuelve true si el endpoint es de entrada (dispositivo → host). */
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

/* Escribe el registro de control de endpoint para un endpoint no-EP0.
 * EP0 no tiene registro de control propio — se omite si endpoint_control es NULL. */
static void usb_setup_endpoint(const struct usb_endpoint_configuration *ep) {
    if (!ep->endpoint_control) return;

    uint32_t dpram_offset = usb_buffer_offset(ep->data_buffer);
    uint32_t reg = EP_CTRL_ENABLE_BITS
                 | EP_CTRL_INTERRUPT_PER_BUFFER
                 | (ep->descriptor->bmAttributes << EP_CTRL_BUFFER_TYPE_LSB)
                 | dpram_offset;
    *ep->endpoint_control = reg;
}

/* Configura todos los endpoints que tienen descriptor y handler asignados. */
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
        /* Para transferencias de salida, copiar los datos a la DPRAM primero */
        if (len > 0) {
            memcpy((void *)ep->data_buffer, buf, len);
        }
        val |= USB_BUF_CTRL_FULL;
    }

    /* Alternar el PID (DATA0/DATA1) para control de flujo USB */
    val |= ep->next_pid ? USB_BUF_CTRL_DATA1_PID : USB_BUF_CTRL_DATA0_PID;
    ep->next_pid ^= 1u;

    /*
     * Workaround RP2040-E5: el bit AVAIL no puede escribirse en el mismo
     * ciclo que los demás campos de buffer_control. Se escribe primero todo
     * sin AVAIL, se esperan al menos 12 ciclos a que la DPRAM propague el
     * valor, y luego se activa AVAIL por separado.
     */
    *ep->buffer_control = val & ~USB_BUF_CTRL_AVAIL;
    busy_wait_at_least_cycles(12);
    *ep->buffer_control = val;
}

/* Declaración adelantada */
static void usb_stall_ep0_in(void);

/* ---------------------------------------------------------------------- */
/*  Gestión de descriptores (respuestas a GET_DESCRIPTOR en EP0)          */
/* ---------------------------------------------------------------------- */

static void usb_handle_device_descriptor(volatile struct usb_setup_packet *pkt) {
    const struct usb_device_descriptor *d = dev_config.device_descriptor;
    struct usb_endpoint_configuration *ep = usb_get_endpoint_configuration(EP0_IN_ADDR);
    ep->next_pid = 1;
    uint16_t len = sizeof(struct usb_device_descriptor);
    if (len > pkt->wLength) len = pkt->wLength;
    usb_start_transfer(ep, (uint8_t *)d, len);
}

static void usb_handle_config_descriptor(volatile struct usb_setup_packet *pkt) {
    uint8_t *buf = &ep0_buf[0];

    /* El descriptor de configuración va seguido del de interfaz y los de endpoint */
    const struct usb_configuration_descriptor *d = dev_config.config_descriptor;
    memcpy(buf, d, sizeof(struct usb_configuration_descriptor));
    buf += sizeof(struct usb_configuration_descriptor);

    if (pkt->wLength >= d->wTotalLength) {
        memcpy(buf, dev_config.interface_descriptor,
               sizeof(struct usb_interface_descriptor));
        buf += sizeof(struct usb_interface_descriptor);

        /* Añadir todos los endpoints no-EP0 que tengan descriptor */
        const struct usb_endpoint_configuration *eps = dev_config.endpoints;
        for (uint i = 2; i < USB_NUM_ENDPOINTS; i++) {
            if (eps[i].descriptor) {
                memcpy(buf, eps[i].descriptor,
                       sizeof(struct usb_endpoint_descriptor));
                buf += sizeof(struct usb_endpoint_descriptor);
            }
        }
    }

    uint32_t len = (uint32_t)buf - (uint32_t)&ep0_buf[0];
    if (len > pkt->wLength) len = pkt->wLength;
    struct usb_endpoint_configuration *ep = usb_get_endpoint_configuration(EP0_IN_ADDR);
    ep->next_pid = 1;
    usb_start_transfer(ep, &ep0_buf[0], (uint16_t)len);
}

#define DESCRIPTOR_STRING_COUNT 3

static void usb_handle_string_descriptor(volatile struct usb_setup_packet *pkt) {
    uint8_t i = pkt->wValue & 0xff;
    uint8_t len = 0;

    if (i == 0) {
        /* Índice 0: descriptor de idioma */
        len = 4;
        memcpy(&ep0_buf[0], dev_config.lang_descriptor, len);
    } else if (i <= DESCRIPTOR_STRING_COUNT) {
        len = usb_prepare_string_descriptor(
                  dev_config.descriptor_strings[i - 1], &ep0_buf[0]);
    } else {
        /* Índice desconocido — responder con STALL */
        usb_stall_ep0_in();
        return;
    }

    uint16_t send_len = len;
    if (send_len > pkt->wLength) send_len = pkt->wLength;
    struct usb_endpoint_configuration *ep = usb_get_endpoint_configuration(EP0_IN_ADDR);
    ep->next_pid = 1;
    usb_start_transfer(ep, &ep0_buf[0], send_len);
}

/* ---------------------------------------------------------------------- */
/*  Gestión de paquetes SETUP                                              */
/* ---------------------------------------------------------------------- */

/* Envía un ACK de longitud cero en EP0 IN (confirma una petición OUT) */
static void usb_acknowledge_out_request(void) {
    usb_start_transfer(usb_get_endpoint_configuration(EP0_IN_ADDR), NULL, 0);
}

/* Activa STALL en EP0 IN para rechazar peticiones no soportadas */
static void usb_stall_ep0_in(void) {
    usb_hw_set->ep_stall_arm = USB_EP_STALL_ARM_EP0_IN_BITS;
    struct usb_endpoint_configuration *ep = usb_get_endpoint_configuration(EP0_IN_ADDR);
    *ep->buffer_control = USB_BUF_CTRL_STALL;
}

static void usb_set_device_address(volatile struct usb_setup_packet *pkt) {
    /* Guardamos la dirección pero la aplicamos después del ACK (en ep0_in_handler),
     * tal como exige la especificación USB 2.0 §9.4.6. */
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

    /* La especificación USB exige resetear los toggles de DATA a DATA0
     * en cada SET_CONFIGURATION. */
    usb_reset_endpoint_pids();
    vendor_rx_ready = false;
    vendor_tx_busy = false;

    /* Armar EP1 OUT para que el host pueda enviar el primer comando */
    usb_start_transfer(usb_get_endpoint_configuration(EP1_OUT_ADDR), NULL, 64);
}

static void usb_handle_clear_feature(volatile struct usb_setup_packet *pkt) {
    uint8_t recipient = pkt->bmRequestType & USB_REQ_TYPE_RECIPIENT_MASK;

    if (recipient == USB_REQ_TYPE_RECIPIENT_ENDPOINT &&
        pkt->wValue == USB_FEAT_ENDPOINT_HALT) {
        uint8_t ep_addr = (uint8_t)(pkt->wIndex & 0xFF);
        struct usb_endpoint_configuration *ep = usb_get_endpoint_configuration(ep_addr);
        if (ep) {
            /* CLEAR_FEATURE/ENDPOINT_HALT resetea el toggle y rearma el endpoint */
            ep->next_pid = 0;
            if (!(ep_addr & USB_DIR_IN)) {
                vendor_rx_ready = false;
                usb_start_transfer(ep, NULL, 64);
            } else {
                vendor_tx_busy = false;
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

    /*
     * Petición de vendor para el descriptor MS OS 2.0.
     * Windows la genera automáticamente cuando ve el BOS descriptor con la
     * capability de plataforma MS OS 2.0. La respuesta indica qué driver
     * cargar (WinUSB en nuestro caso cuando usamos nuestro propio VID/PID).
     */
    if ((req_direction & USB_DIR_IN) &&
        (req_direction & USB_REQ_TYPE_TYPE_MASK) == USB_REQ_TYPE_TYPE_VENDOR &&
        req == MS_OS_20_VENDOR_CODE &&
        pkt->wIndex == 0x07) {
        uint16_t len = ms_os_20_descriptor_set_len;
        if (len > pkt->wLength) len = pkt->wLength;
        memcpy(&ep0_buf[0], ms_os_20_descriptor_set, len);
        struct usb_endpoint_configuration *ep =
            usb_get_endpoint_configuration(EP0_IN_ADDR);
        ep->next_pid = 1;
        usb_start_transfer(ep, &ep0_buf[0], len);
        return;
    }

    if (!(req_direction & USB_DIR_IN)) {
        /* Peticiones estándar de salida (host → dispositivo) */
        if (req == USB_REQUEST_SET_ADDRESS) {
            usb_set_device_address(pkt);
        } else if (req == USB_REQUEST_SET_CONFIGURATION) {
            usb_set_device_configuration(pkt);
        } else if (req == USB_REQUEST_CLEAR_FEATURE) {
            usb_handle_clear_feature(pkt);
        } else {
            usb_acknowledge_out_request();
        }
    } else {
        /* Peticiones estándar de entrada (dispositivo → host) */
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
            case USB_DT_BOS: {
                /* El BOS descriptor es necesario para que Windows descubra
                 * la capability MS OS 2.0 y nos asigne WinUSB automáticamente */
                uint16_t len = bos_descriptor_len;
                if (len > pkt->wLength) len = pkt->wLength;
                memcpy(&ep0_buf[0], bos_descriptor, len);
                struct usb_endpoint_configuration *ep =
                    usb_get_endpoint_configuration(EP0_IN_ADDR);
                ep->next_pid = 1;
                usb_start_transfer(ep, &ep0_buf[0], len);
                break;
            }
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
/*  Gestión del buffer status (notificación de transferencias completadas) */
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

/* El registro buf_status tiene un bit por cada endpoint (IN y OUT separados).
 * Iteramos todos los bits activos, limpiamos cada uno y llamamos al handler. */
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

    /*
     * Intencionadamente NO limpiamos 'enumerated' aquí. Ese flag solo sirve
     * para el bucle de espera en main() y debe ponerse a true exactamente
     * una vez. Si lo limpiáramos en cada reset de bus, main() podría no
     * verlo nunca si el host resetea el bus justo después de SET_ADDRESS.
     */
    vendor_rx_ready = false;
    vendor_tx_busy = false;

    /*
     * Resetear los toggles PID de TODOS los endpoints (USB 2.0 §9.4.6):
     * tras un bus reset tanto el host como el dispositivo vuelven a DATA0.
     * Si no se hace, la segunda sesión libusb (que reinicia su toggle a DATA0)
     * choca con el device que espera DATA1 del ciclo anterior.
     */
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
        /* Aplicar la dirección USB ahora que el ACK se ha enviado */
        usb_hw->dev_addr_ctrl = dev_addr;
        should_set_address = false;
        enumerated = true;
    } else {
        /* Preparar EP0 OUT para el siguiente paquete de control */
        struct usb_endpoint_configuration *ep =
            usb_get_endpoint_configuration(EP0_OUT_ADDR);
        usb_start_transfer(ep, NULL, 0);
    }
}

void ep0_out_handler(uint8_t *buf, uint16_t len) {
    (void)buf; (void)len;
}

/* Handler para EP1 OUT: el host ha enviado un comando J-Link.
 * Copiamos los datos al buffer interno y señalamos que hay datos disponibles. */
void ep1_out_handler(uint8_t *buf, uint16_t len) {
    if (len > 64) len = 64;
    memcpy(vendor_rx_buf, buf, len);
    vendor_rx_len = len;
    vendor_rx_ready = true;
}

/*
 * Handler para EP1 IN (0x81, interrupt).
 * Solo está presente en el descriptor para que jlink.sys entre en modo
 * de comandos bulk. Nunca se arma; jlink.sys no lee respuestas aquí.
 */
void ep1_in_handler(uint8_t *buf, uint16_t len) {
    (void)buf; (void)len;
}

/*
 * Handler para EP2 IN (0x82, bulk).
 * Canal principal de respuestas J-Link: jlink.sys lee aquí las respuestas
 * a sus comandos (igual que el hardware J-Link V9 real).
 */
void ep2_in_handler(uint8_t *buf, uint16_t len) {
    (void)buf; (void)len;
    vendor_tx_busy = false;
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

    /* Resetear y liberar el controlador USB */
    reset_block(RESETS_RESET_USBCTRL_BITS);
    unreset_block_wait(RESETS_RESET_USBCTRL_BITS);

    /* Limpiar toda la DPRAM — el hardware la comparte para datos y registros */
    memset(usb_dpram, 0, sizeof(*usb_dpram));

    /* Conectar el controlador USB al PHY interno */
    usb_hw->muxing = USB_USB_MUXING_TO_PHY_BITS
                   | USB_USB_MUXING_SOFTCON_BITS;

    /* Habilitar detección de VBUS (necesario para que el controlador funcione) */
    usb_hw->pwr = USB_USB_PWR_VBUS_DETECT_BITS
                | USB_USB_PWR_VBUS_DETECT_OVERRIDE_EN_BITS;

    usb_hw->main_ctrl = USB_MAIN_CTRL_CONTROLLER_EN_BITS;

    /* Generar interrupción en EP0 con buffer único (no doble) */
    usb_hw->sie_ctrl = USB_SIE_CTRL_EP0_INT_1BUF_BITS;

    /* Habilitar las tres interrupciones que nos interesan */
    usb_hw->inte = USB_INTS_BUFF_STATUS_BITS
                 | USB_INTS_BUS_RESET_BITS
                 | USB_INTS_SETUP_REQ_BITS;

    usb_setup_endpoints();

    irq_set_exclusive_handler(USBCTRL_IRQ, isr_usbctrl);
    irq_set_enabled(USBCTRL_IRQ, true);

    /* Activar la resistencia pull-up en D+ para señalizar al host que hay
     * un dispositivo full-speed conectado */
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
    vendor_rx_len = 0;

    /* No rearmar EP1 OUT aquí — el llamador debe hacerlo con
     * usb_vendor_arm_rx() después de enviar la respuesta. */

    return len;
}

void usb_vendor_arm_rx(void) {
    usb_start_transfer(usb_get_endpoint_configuration(EP1_OUT_ADDR), NULL, 64);
}

bool usb_vendor_write(const uint8_t *data, uint16_t len) {
    if (vendor_tx_busy) {
        /*
         * Fallback de polling: si la ISR no limpió vendor_tx_busy comprobamos
         * directamente el registro buffer_control de EP2 IN para ver si el
         * hardware ya terminó la transferencia bulk.
         */
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

    /* Consultar el hardware directamente por si la ISR se perdió el evento */
    struct usb_endpoint_configuration *ep =
        usb_get_endpoint_configuration(EP2_IN_ADDR);
    uint32_t bc = *ep->buffer_control;
    if (!(bc & USB_BUF_CTRL_AVAIL)) {
        vendor_tx_busy = false;
        return false;
    }
    return true;
}
