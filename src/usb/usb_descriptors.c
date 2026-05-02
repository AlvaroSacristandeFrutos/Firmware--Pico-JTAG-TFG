/*
 * Descriptores USB para Pico Boundary Scan Adapter.
 * Adaptado de pico-examples/usb/device/dev_lowlevel/dev_lowlevel.h
 * (Copyright (c) 2020 Raspberry Pi (Trading) Ltd, licencia BSD-3-Clause)
 *
 * VID/PID: 0x2E8A / 0x000A  (Raspberry Pi Pico, CDC composite)
 * 4 interfaces: CDC ACM Control (MI_00) + CDC Data (MI_01)
 *               UART CDC ACM Control (MI_02) + UART CDC Data (MI_03)
 * Endpoints (8 total):
 *   EP1 IN  (0x81) interrupt — notificaciones CDC (nunca armado)
 *   EP3 OUT (0x03) bulk     — datos CDC host→device (protocolo PicoAdapter)
 *   EP3 IN  (0x83) bulk     — datos CDC device→host (respuestas PicoAdapter)
 *   EP2 IN  (0x82) interrupt — notificaciones UART CDC (nunca armado)
 *   EP4 OUT (0x04) bulk     — datos UART host→device
 *   EP4 IN  (0x84) bulk     — datos UART device→host
 */

#include "usb_descriptors.h"
#include "pico/unique_id.h"
#include <string.h>

/*
 * Declaraciones adelantadas de los handlers de endpoint definidos en usb_device.c
 * y uart_bridge.c.
 */
void ep0_in_handler(uint8_t *buf, uint16_t len);
void ep0_out_handler(uint8_t *buf, uint16_t len);
void cdc_notify_in_handler(uint8_t *buf, uint16_t len);  /* CDC EP1 IN  (0x81) notify         */
void cdc_data_out_handler(uint8_t *buf, uint16_t len);   /* CDC EP3 OUT (0x03) datos entrantes */
void cdc_data_in_handler(uint8_t *buf, uint16_t len);    /* CDC EP3 IN  (0x83) datos salientes */
void uart_notify_in_handler(uint8_t *buf, uint16_t len); /* UART EP2 IN  (0x82) notify        */
void uart_data_out_handler(uint8_t *buf, uint16_t len);  /* UART EP4 OUT (0x04) datos entrada */
void uart_data_in_handler(uint8_t *buf, uint16_t len);   /* UART EP4 IN  (0x84) datos salida  */

/* ---------------------------------------------------------------------- */
/*  Descriptores de endpoint                                               */
/* ---------------------------------------------------------------------- */

static const struct usb_endpoint_descriptor ep0_out = {
    .bLength          = sizeof(struct usb_endpoint_descriptor),
    .bDescriptorType  = USB_DT_ENDPOINT,
    .bEndpointAddress = EP0_OUT_ADDR,
    .bmAttributes     = USB_TRANSFER_TYPE_CONTROL,
    .wMaxPacketSize   = 64,
    .bInterval        = 0
};

static const struct usb_endpoint_descriptor ep0_in = {
    .bLength          = sizeof(struct usb_endpoint_descriptor),
    .bDescriptorType  = USB_DT_ENDPOINT,
    .bEndpointAddress = EP0_IN_ADDR,
    .bmAttributes     = USB_TRANSFER_TYPE_CONTROL,
    .wMaxPacketSize   = 64,
    .bInterval        = 0
};

/* CDC EP1 IN interrupt — notificaciones CDC (0x81, nunca armado) */
static const struct usb_endpoint_descriptor cdc_ep_notify = {
    .bLength          = sizeof(struct usb_endpoint_descriptor),
    .bDescriptorType  = USB_DT_ENDPOINT,
    .bEndpointAddress = CDC_EP_NOTIFY,   /* 0x81 */
    .bmAttributes     = USB_TRANSFER_TYPE_INTERRUPT,
    .wMaxPacketSize   = 16,
    .bInterval        = 255
};

/* CDC EP3 OUT bulk — datos host→device (0x03, protocolo PicoAdapter) */
static const struct usb_endpoint_descriptor cdc_ep_data_out = {
    .bLength          = sizeof(struct usb_endpoint_descriptor),
    .bDescriptorType  = USB_DT_ENDPOINT,
    .bEndpointAddress = CDC_EP_DATA_OUT, /* 0x03 */
    .bmAttributes     = USB_TRANSFER_TYPE_BULK,
    .wMaxPacketSize   = 64,
    .bInterval        = 0
};

/* CDC EP3 IN bulk — datos device→host (0x83, respuestas PicoAdapter) */
static const struct usb_endpoint_descriptor cdc_ep_data_in = {
    .bLength          = sizeof(struct usb_endpoint_descriptor),
    .bDescriptorType  = USB_DT_ENDPOINT,
    .bEndpointAddress = CDC_EP_DATA_IN,  /* 0x83 */
    .bmAttributes     = USB_TRANSFER_TYPE_BULK,
    .wMaxPacketSize   = 64,
    .bInterval        = 0
};

/* UART EP2 IN interrupt — notificaciones UART CDC (0x82, nunca armado) */
static const struct usb_endpoint_descriptor uart_ep_notify = {
    .bLength          = sizeof(struct usb_endpoint_descriptor),
    .bDescriptorType  = USB_DT_ENDPOINT,
    .bEndpointAddress = UART_EP_NOTIFY,   /* 0x82 */
    .bmAttributes     = USB_TRANSFER_TYPE_INTERRUPT,
    .wMaxPacketSize   = 16,
    .bInterval        = 255
};

/* UART EP4 OUT bulk — datos host→device (0x04) */
static const struct usb_endpoint_descriptor uart_ep_data_out = {
    .bLength          = sizeof(struct usb_endpoint_descriptor),
    .bDescriptorType  = USB_DT_ENDPOINT,
    .bEndpointAddress = UART_EP_DATA_OUT, /* 0x04 */
    .bmAttributes     = USB_TRANSFER_TYPE_BULK,
    .wMaxPacketSize   = 64,
    .bInterval        = 0
};

/* UART EP4 IN bulk — datos device→host (0x84) */
static const struct usb_endpoint_descriptor uart_ep_data_in = {
    .bLength          = sizeof(struct usb_endpoint_descriptor),
    .bDescriptorType  = USB_DT_ENDPOINT,
    .bEndpointAddress = UART_EP_DATA_IN,  /* 0x84 */
    .bmAttributes     = USB_TRANSFER_TYPE_BULK,
    .wMaxPacketSize   = 64,
    .bInterval        = 0
};

/* ---------------------------------------------------------------------- */
/*  Descriptor de dispositivo                                              */
/* ---------------------------------------------------------------------- */

static const struct usb_device_descriptor device_descriptor __attribute__((aligned(4))) = {
    .bLength            = sizeof(struct usb_device_descriptor),
    .bDescriptorType    = USB_DT_DEVICE,
    .bcdUSB             = 0x0200,
    /*
     * Clase 0xEF/0x02/0x01 (Misc/IAD): necesaria cuando el descriptor de
     * configuración contiene un IAD.
     */
    .bDeviceClass       = 0xEF,
    .bDeviceSubClass    = 0x02,
    .bDeviceProtocol    = 0x01,
    .bMaxPacketSize0    = 64,
    .idVendor           = 0x2E8A,   /* Raspberry Pi */
    .idProduct          = 0x000A,   /* Pico CDC composite */
    .bcdDevice          = 0x0100,
    .iManufacturer      = 1,
    .iProduct           = 2,
    .iSerialNumber      = 3,
    .bNumConfigurations = 1
};

/* ---------------------------------------------------------------------- */
/*  Descriptor de configuración completo (141 bytes, pre-construido)      */
/*                                                                         */
/*  Estructura:                                                            */
/*    [  9] Config header                                                  */
/*    [  8] IAD (interfaces 0+1 como función CDC — PicoAdapter)           */
/*    [  9] Interface 0: CDC ACM Control (class 0x02/0x02/0x01)           */
/*    [  5] CDC Header Functional Descriptor                               */
/*    [  4] CDC ACM Functional Descriptor                                  */
/*    [  5] CDC Union Functional Descriptor                                */
/*    [  5] CDC Call Management Functional Descriptor                      */
/*    [  7] EP1 IN interrupt (CDC notify, 0x81, nunca armado)             */
/*    [  9] Interface 1: CDC Data (class 0x0A/0x00/0x00)                 */
/*    [  7] EP3 OUT bulk (datos host→device, 0x03)                       */
/*    [  7] EP3 IN  bulk (datos device→host, 0x83)                       */
/*    [  8] IAD (interfaces 2+3 como función CDC — UART bridge)           */
/*    [  9] Interface 2: CDC ACM Control (class 0x02/0x02/0x01)           */
/*    [  5] CDC Header Functional Descriptor                               */
/*    [  4] CDC ACM Functional Descriptor (bmCapabilities=0x02)           */
/*    [  5] CDC Union Functional Descriptor                                */
/*    [  5] CDC Call Management Functional Descriptor                      */
/*    [  7] EP2 IN interrupt (UART notify, 0x82, nunca armado)            */
/*    [  9] Interface 3: CDC Data (class 0x0A/0x00/0x00)                 */
/*    [  7] EP4 OUT bulk (datos host→device, 0x04)                       */
/*    [  7] EP4 IN  bulk (datos device→host, 0x84)                       */
/*  TOTAL = 75 + 66 = 141 bytes = 0x8D                                   */
/* ---------------------------------------------------------------------- */
static const uint8_t s_config_desc[] __attribute__((aligned(4))) = {
    /* --- Cabecera de configuración (9 bytes) --- */
    0x09, 0x02,         /* bLength=9, bDescriptorType=CONFIGURATION */
    0x8D, 0x00,         /* wTotalLength=141 (LE) */
    0x04,               /* bNumInterfaces=4 */
    0x01,               /* bConfigurationValue=1 */
    0x00,               /* iConfiguration=0 */
    0xC0,               /* bmAttributes: self-powered */
    0x32,               /* bMaxPower: 100 mA */

    /* --- IAD: agrupa interfaces 0+1 como función CDC PicoAdapter (8 bytes) --- */
    0x08, 0x0B,         /* bLength=8, bDescriptorType=INTERFACE_ASSOCIATION */
    0x00,               /* bFirstInterface=0 */
    0x02,               /* bInterfaceCount=2 */
    0x02,               /* bFunctionClass=CDC */
    0x02,               /* bFunctionSubClass=ACM */
    0x01,               /* bFunctionProtocol=AT commands */
    0x00,               /* iFunction=0 */

    /* --- Interface 0: CDC ACM Control (9 bytes) --- */
    0x09, 0x04,         /* bLength=9, bDescriptorType=INTERFACE */
    0x00,               /* bInterfaceNumber=0 */
    0x00,               /* bAlternateSetting=0 */
    0x01,               /* bNumEndpoints=1 */
    0x02,               /* bInterfaceClass=CDC */
    0x02,               /* bInterfaceSubClass=ACM */
    0x01,               /* bInterfaceProtocol=AT commands */
    0x00,               /* iInterface=0 */

    /* CDC Header Functional Descriptor (5 bytes) */
    0x05, 0x24, 0x00,   /* bFunctionLength=5, CS_INTERFACE, HEADER */
    0x10, 0x01,         /* bcdCDC=1.10 */

    /* CDC ACM Functional Descriptor (4 bytes) */
    0x04, 0x24, 0x02,   /* bFunctionLength=4, CS_INTERFACE, ABSTRACT_CONTROL_MANAGEMENT */
    0x00,               /* bmCapabilities=0 */

    /* CDC Union Functional Descriptor (5 bytes) */
    0x05, 0x24, 0x06,   /* bFunctionLength=5, CS_INTERFACE, UNION */
    0x00,               /* bControlInterface=0 */
    0x01,               /* bSubordinateInterface0=1 */

    /* CDC Call Management Functional Descriptor (5 bytes) */
    0x05, 0x24, 0x01,   /* bFunctionLength=5, CS_INTERFACE, CALL_MANAGEMENT */
    0x00,               /* bmCapabilities=0 */
    0x01,               /* bDataInterface=1 */

    /* EP1 IN interrupt — notify CDC (7 bytes) */
    0x07, 0x05,         /* bLength=7, bDescriptorType=ENDPOINT */
    0x81,               /* bEndpointAddress=EP1 IN */
    0x03,               /* bmAttributes=interrupt */
    0x10, 0x00,         /* wMaxPacketSize=16 */
    0xFF,               /* bInterval=255 ms */

    /* --- Interface 1: CDC Data (9 bytes) --- */
    0x09, 0x04,         /* bLength=9, bDescriptorType=INTERFACE */
    0x01,               /* bInterfaceNumber=1 */
    0x00,               /* bAlternateSetting=0 */
    0x02,               /* bNumEndpoints=2 */
    0x0A,               /* bInterfaceClass=CDC Data */
    0x00,               /* bInterfaceSubClass=0 */
    0x00,               /* bInterfaceProtocol=0 */
    0x00,               /* iInterface=0 */

    /* EP3 OUT bulk — datos host→device (7 bytes) */
    0x07, 0x05,         /* bLength=7, bDescriptorType=ENDPOINT */
    0x03,               /* bEndpointAddress=EP3 OUT */
    0x02,               /* bmAttributes=bulk */
    0x40, 0x00,         /* wMaxPacketSize=64 */
    0x00,               /* bInterval=0 */

    /* EP3 IN bulk — datos device→host (7 bytes) */
    0x07, 0x05,         /* bLength=7, bDescriptorType=ENDPOINT */
    0x83,               /* bEndpointAddress=EP3 IN */
    0x02,               /* bmAttributes=bulk */
    0x40, 0x00,         /* wMaxPacketSize=64 */
    0x00,               /* bInterval=0 */

    /* ================================================================== */
    /* Second CDC function: UART transparent bridge (interfaces 2+3)      */
    /* ================================================================== */

    /* --- IAD: agrupa interfaces 2+3 como función CDC UART (8 bytes) --- */
    0x08, 0x0B,         /* bLength=8, bDescriptorType=INTERFACE_ASSOCIATION */
    0x02,               /* bFirstInterface=2 */
    0x02,               /* bInterfaceCount=2 */
    0x02,               /* bFunctionClass=CDC */
    0x02,               /* bFunctionSubClass=ACM */
    0x01,               /* bFunctionProtocol=AT commands */
    0x00,               /* iFunction=0 */

    /* --- Interface 2: CDC ACM Control (9 bytes) --- */
    0x09, 0x04,         /* bLength=9, bDescriptorType=INTERFACE */
    0x02,               /* bInterfaceNumber=2 */
    0x00,               /* bAlternateSetting=0 */
    0x01,               /* bNumEndpoints=1 */
    0x02,               /* bInterfaceClass=CDC */
    0x02,               /* bInterfaceSubClass=ACM */
    0x01,               /* bInterfaceProtocol=AT commands */
    0x00,               /* iInterface=0 */

    /* CDC Header Functional Descriptor (5 bytes) */
    0x05, 0x24, 0x00,   /* bFunctionLength=5, CS_INTERFACE, HEADER */
    0x10, 0x01,         /* bcdCDC=1.10 */

    /* CDC ACM Functional Descriptor (4 bytes) */
    0x04, 0x24, 0x02,   /* bFunctionLength=4, CS_INTERFACE, ABSTRACT_CONTROL_MANAGEMENT */
    0x02,               /* bmCapabilities=0x02: supports SET_LINE_CODING */

    /* CDC Union Functional Descriptor (5 bytes) */
    0x05, 0x24, 0x06,   /* bFunctionLength=5, CS_INTERFACE, UNION */
    0x02,               /* bControlInterface=2 */
    0x03,               /* bSubordinateInterface0=3 */

    /* CDC Call Management Functional Descriptor (5 bytes) */
    0x05, 0x24, 0x01,   /* bFunctionLength=5, CS_INTERFACE, CALL_MANAGEMENT */
    0x00,               /* bmCapabilities=0 */
    0x03,               /* bDataInterface=3 */

    /* EP2 IN interrupt — notify UART CDC (7 bytes) */
    0x07, 0x05,         /* bLength=7, bDescriptorType=ENDPOINT */
    0x82,               /* bEndpointAddress=EP2 IN */
    0x03,               /* bmAttributes=interrupt */
    0x10, 0x00,         /* wMaxPacketSize=16 */
    0xFF,               /* bInterval=255 ms */

    /* --- Interface 3: CDC Data (9 bytes) --- */
    0x09, 0x04,         /* bLength=9, bDescriptorType=INTERFACE */
    0x03,               /* bInterfaceNumber=3 */
    0x00,               /* bAlternateSetting=0 */
    0x02,               /* bNumEndpoints=2 */
    0x0A,               /* bInterfaceClass=CDC Data */
    0x00,               /* bInterfaceSubClass=0 */
    0x00,               /* bInterfaceProtocol=0 */
    0x00,               /* iInterface=0 */

    /* EP4 OUT bulk — datos UART host→device (7 bytes) */
    0x07, 0x05,         /* bLength=7, bDescriptorType=ENDPOINT */
    0x04,               /* bEndpointAddress=EP4 OUT */
    0x02,               /* bmAttributes=bulk */
    0x40, 0x00,         /* wMaxPacketSize=64 */
    0x00,               /* bInterval=0 */

    /* EP4 IN bulk — datos UART device→host (7 bytes) */
    0x07, 0x05,         /* bLength=7, bDescriptorType=ENDPOINT */
    0x84,               /* bEndpointAddress=EP4 IN */
    0x02,               /* bmAttributes=bulk */
    0x40, 0x00,         /* wMaxPacketSize=64 */
    0x00,               /* bInterval=0 */
};

/* ---------------------------------------------------------------------- */
/*  Descriptores de cadena                                                 */
/* ---------------------------------------------------------------------- */

static const unsigned char lang_descriptor[] __attribute__((aligned(4))) = {
    4, USB_DT_STRING, 0x09, 0x04   /* English (US) */
};

static char serial_string[12];

static const unsigned char *descriptor_strings[] = {
    (const unsigned char *)"Raspberry Pi",
    (const unsigned char *)"Pico",
    (const unsigned char *)serial_string
};

/* ---------------------------------------------------------------------- */
/*  Instancia de configuración del dispositivo                             */
/* ---------------------------------------------------------------------- */

struct usb_device_configuration dev_config = {
    .device_descriptor = &device_descriptor,
    .config_desc_full  = s_config_desc,
    .config_desc_len   = sizeof(s_config_desc),
    .lang_descriptor   = lang_descriptor,
    .descriptor_strings = descriptor_strings,
    .endpoints = {
        {   /* EP0 OUT — control saliente */
            .descriptor       = &ep0_out,
            .handler          = &ep0_out_handler,
            .endpoint_control = NULL,
            .buffer_control   = &usb_dpram->ep_buf_ctrl[0].out,
            .data_buffer      = &usb_dpram->ep0_buf_a[0],
        },
        {   /* EP0 IN — control entrante */
            .descriptor       = &ep0_in,
            .handler          = &ep0_in_handler,
            .endpoint_control = NULL,
            .buffer_control   = &usb_dpram->ep_buf_ctrl[0].in,
            .data_buffer      = &usb_dpram->ep0_buf_a[0],
        },
        {   /* EP1 IN (0x81) — CDC notify (nunca armado) */
            .descriptor       = &cdc_ep_notify,
            .handler          = &cdc_notify_in_handler,
            .endpoint_control = &usb_dpram->ep_ctrl[0].in,
            .buffer_control   = &usb_dpram->ep_buf_ctrl[1].in,
            .data_buffer      = &usb_dpram->epx_data[0 * 64],
        },
        {   /* EP3 OUT (0x03) — datos CDC host→device */
            .descriptor       = &cdc_ep_data_out,
            .handler          = &cdc_data_out_handler,
            .endpoint_control = &usb_dpram->ep_ctrl[2].out,
            .buffer_control   = &usb_dpram->ep_buf_ctrl[3].out,
            .data_buffer      = &usb_dpram->epx_data[1 * 64],
        },
        {   /* EP3 IN (0x83) — datos CDC device→host */
            .descriptor       = &cdc_ep_data_in,
            .handler          = &cdc_data_in_handler,
            .endpoint_control = &usb_dpram->ep_ctrl[2].in,
            .buffer_control   = &usb_dpram->ep_buf_ctrl[3].in,
            .data_buffer      = &usb_dpram->epx_data[2 * 64],
        },
        {   /* EP2 IN (0x82) — UART notify (nunca armado) */
            .descriptor       = &uart_ep_notify,
            .handler          = &uart_notify_in_handler,
            .endpoint_control = &usb_dpram->ep_ctrl[1].in,
            .buffer_control   = &usb_dpram->ep_buf_ctrl[2].in,
            .data_buffer      = &usb_dpram->epx_data[3 * 64],
        },
        {   /* EP4 OUT (0x04) — UART datos host→device */
            .descriptor       = &uart_ep_data_out,
            .handler          = &uart_data_out_handler,
            .endpoint_control = &usb_dpram->ep_ctrl[3].out,
            .buffer_control   = &usb_dpram->ep_buf_ctrl[4].out,
            .data_buffer      = &usb_dpram->epx_data[4 * 64],
        },
        {   /* EP4 IN (0x84) — UART datos device→host */
            .descriptor       = &uart_ep_data_in,
            .handler          = &uart_data_in_handler,
            .endpoint_control = &usb_dpram->ep_ctrl[3].in,
            .buffer_control   = &usb_dpram->ep_buf_ctrl[4].in,
            .data_buffer      = &usb_dpram->epx_data[5 * 64],
        },
    }
};

/* ---------------------------------------------------------------------- */
/*  Funciones públicas                                                     */
/* ---------------------------------------------------------------------- */

static uint32_t derive_serial_number(void) {
    pico_unique_board_id_t id;
    pico_get_unique_board_id(&id);
    uint32_t hash = 2166136261u;
    for (int i = 0; i < PICO_UNIQUE_BOARD_ID_SIZE_BYTES; i++) {
        hash ^= id.id[i];
        hash *= 16777619u;
    }
    return hash % 1000000000u;
}

void usb_descriptors_init(void) {
    uint32_t sn = derive_serial_number();
    for (int i = 8; i >= 0; i--) {
        serial_string[i] = '0' + (sn % 10);
        sn /= 10;
    }
    serial_string[9] = '\0';
}

uint8_t usb_prepare_string_descriptor(const unsigned char *str, uint8_t *buf) {
    /* El llamador proporciona un buffer de 64 bytes.
     * Con 2 bytes de cabecera (bLength + bDescriptorType) quedan 62 bytes
     * para el string en UTF-16 LE → máximo 31 caracteres útiles. */
    size_t slen = strlen((const char *)str);
    if (slen > 31u) slen = 31u;

    uint8_t bLength = (uint8_t)(2u + slen * 2u);
    *buf++ = bLength;
    *buf++ = USB_DT_STRING;
    for (size_t i = 0u; i < slen; i++) {
        *buf++ = str[i];
        *buf++ = 0u;   /* byte alto UTF-16 LE (ASCII → siempre 0) */
    }
    return bLength;
}
