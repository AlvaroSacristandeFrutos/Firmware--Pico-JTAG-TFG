/*
 * Descriptores USB para J-Link Pico Probe.
 * Adaptado de pico-examples/usb/device/dev_lowlevel/dev_lowlevel.h
 * (Copyright (c) 2020 Raspberry Pi (Trading) Ltd, licencia BSD-3-Clause)
 */

#include "usb_descriptors.h"
#include "pico/unique_id.h"
#include <string.h>

/*
 * Declaraciones adelantadas de los handlers de endpoint definidos en usb_device.c.
 */
void ep0_in_handler(uint8_t *buf, uint16_t len);
void ep0_out_handler(uint8_t *buf, uint16_t len);
void ep1_out_handler(uint8_t *buf, uint16_t len);   /* J-Link: EP3 OUT (0x03) */
void ep2_in_handler(uint8_t *buf, uint16_t len);    /* J-Link: EP3 IN (0x83)  */
void cdc_notify_in_handler(uint8_t *buf, uint16_t len);  /* CDC EP1 IN  (0x81) */
void cdc_data_out_handler(uint8_t *buf, uint16_t len);   /* CDC EP1 OUT (0x01) */
void cdc_data_in_handler(uint8_t *buf, uint16_t len);    /* CDC EP2 IN  (0x82) */

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

/* CDC EP1 IN interrupt (notificaciones CDC, 0x81) */
static const struct usb_endpoint_descriptor cdc_ep_notify = {
    .bLength          = sizeof(struct usb_endpoint_descriptor),
    .bDescriptorType  = USB_DT_ENDPOINT,
    .bEndpointAddress = CDC_EP_NOTIFY,   /* 0x81 */
    .bmAttributes     = USB_TRANSFER_TYPE_INTERRUPT,
    .wMaxPacketSize   = 16,
    .bInterval        = 255              /* 255 ms */
};

/* CDC EP1 OUT bulk (datos host→device, 0x01) */
static const struct usb_endpoint_descriptor cdc_ep_data_out = {
    .bLength          = sizeof(struct usb_endpoint_descriptor),
    .bDescriptorType  = USB_DT_ENDPOINT,
    .bEndpointAddress = CDC_EP_DATA_OUT, /* 0x01 */
    .bmAttributes     = USB_TRANSFER_TYPE_BULK,
    .wMaxPacketSize   = 64,
    .bInterval        = 0
};

/* CDC EP2 IN bulk (datos device→host, 0x82) */
static const struct usb_endpoint_descriptor cdc_ep_data_in = {
    .bLength          = sizeof(struct usb_endpoint_descriptor),
    .bDescriptorType  = USB_DT_ENDPOINT,
    .bEndpointAddress = CDC_EP_DATA_IN,  /* 0x82 */
    .bmAttributes     = USB_TRANSFER_TYPE_BULK,
    .wMaxPacketSize   = 64,
    .bInterval        = 0
};

/* EP3 OUT bulk (comandos J-Link, 0x03) */
static const struct usb_endpoint_descriptor ep3_out = {
    .bLength          = sizeof(struct usb_endpoint_descriptor),
    .bDescriptorType  = USB_DT_ENDPOINT,
    .bEndpointAddress = EP3_OUT_ADDR,
    .bmAttributes     = USB_TRANSFER_TYPE_BULK,
    .wMaxPacketSize   = 64,
    .bInterval        = 0
};

/* EP3 IN bulk (respuestas J-Link, 0x83) */
static const struct usb_endpoint_descriptor ep3_in = {
    .bLength          = sizeof(struct usb_endpoint_descriptor),
    .bDescriptorType  = USB_DT_ENDPOINT,
    .bEndpointAddress = EP3_IN_ADDR,
    .bmAttributes     = USB_TRANSFER_TYPE_BULK,
    .wMaxPacketSize   = 64,
    .bInterval        = 0
};

/* ---------------------------------------------------------------------- */
/*  Descriptor de dispositivo                                              */
/* ---------------------------------------------------------------------- */

static const struct usb_device_descriptor device_descriptor = {
    .bLength            = sizeof(struct usb_device_descriptor),
    .bDescriptorType    = USB_DT_DEVICE,
    .bcdUSB             = 0x0200,
    /*
     * Clase 0xEF/0x02/0x01 (Misc/IAD): necesaria cuando el descriptor de
     * configuración contiene un IAD (Interface Association Descriptor).
     * Sin esta clase, Windows no aplica el IAD correctamente.
     */
    .bDeviceClass       = 0xEF,
    .bDeviceSubClass    = 0x02,
    .bDeviceProtocol    = 0x01,
    .bMaxPacketSize0    = 64,
    .idVendor           = 0x1366,   /* Segger Microteq */
    .idProduct          = 0x0105,   /* J-Link OB */
    .bcdDevice          = 0x0100,
    .iManufacturer      = 1,
    .iProduct           = 2,
    .iSerialNumber      = 3,
    .bNumConfigurations = 1
};

/* ---------------------------------------------------------------------- */
/*  Descriptor de configuración completo (98 bytes, pre-construido)       */
/*                                                                         */
/*  Estructura:                                                            */
/*    [  9] Config header                                                  */
/*    [  8] IAD (Interface Association Descriptor) para CDC               */
/*    [  9] Interface 0: CDC ACM Control (class 0x02/0x02/0x01)           */
/*    [  5] CDC Header Functional Descriptor                               */
/*    [  4] CDC ACM Functional Descriptor                                  */
/*    [  5] CDC Union Functional Descriptor                                */
/*    [  5] CDC Call Management Functional Descriptor                      */
/*    [  7] EP1 IN interrupt (CDC notificaciones, 0x81)                   */
/*    [  9] Interface 1: CDC Data (class 0x0A/0x00/0x00)                 */
/*    [  7] EP1 OUT bulk (CDC datos, 0x01)                                */
/*    [  7] EP2 IN  bulk (CDC datos, 0x82)                                */
/*    [  9] Interface 2: J-Link Vendor (class 0xFF/0xFF/0xFF)             */
/*    [  7] EP3 OUT bulk (J-Link comandos, 0x03)                          */
/*    [  7] EP3 IN  bulk (J-Link respuestas, 0x83)                        */
/*  TOTAL = 98 bytes = 0x62                                               */
/* ---------------------------------------------------------------------- */
static const uint8_t s_config_desc[] = {
    /* --- Cabecera de configuración (9 bytes) --- */
    0x09, 0x02,         /* bLength=9, bDescriptorType=CONFIGURATION */
    0x62, 0x00,         /* wTotalLength=98 (LE) */
    0x03,               /* bNumInterfaces=3 */
    0x01,               /* bConfigurationValue=1 */
    0x00,               /* iConfiguration=0 */
    0xC0,               /* bmAttributes: self-powered */
    0x32,               /* bMaxPower: 100 mA */

    /* --- IAD: agrupa interfaces 0+1 como función CDC (8 bytes) --- */
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
    0x01,               /* bNumEndpoints=1 (solo EP1 IN interrupt) */
    0x02,               /* bInterfaceClass=CDC */
    0x02,               /* bInterfaceSubClass=ACM */
    0x01,               /* bInterfaceProtocol=AT commands */
    0x00,               /* iInterface=0 */

    /* CDC Header Functional Descriptor (5 bytes) */
    0x05, 0x24, 0x00,   /* bFunctionLength=5, CS_INTERFACE, HEADER */
    0x10, 0x01,         /* bcdCDC=1.10 */

    /* CDC ACM Functional Descriptor (4 bytes) */
    0x04, 0x24, 0x02,   /* bFunctionLength=4, CS_INTERFACE, ABSTRACT_CONTROL_MANAGEMENT */
    0x00,               /* bmCapabilities: ninguna requerida */

    /* CDC Union Functional Descriptor (5 bytes) */
    0x05, 0x24, 0x06,   /* bFunctionLength=5, CS_INTERFACE, UNION */
    0x00,               /* bControlInterface=0 */
    0x01,               /* bSubordinateInterface0=1 */

    /* CDC Call Management Functional Descriptor (5 bytes) */
    0x05, 0x24, 0x01,   /* bFunctionLength=5, CS_INTERFACE, CALL_MANAGEMENT */
    0x00,               /* bmCapabilities: no call management */
    0x01,               /* bDataInterface=1 */

    /* EP1 IN interrupt — notificaciones CDC (7 bytes) */
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

    /* EP1 OUT bulk — datos CDC (7 bytes) */
    0x07, 0x05,         /* bLength=7, bDescriptorType=ENDPOINT */
    0x01,               /* bEndpointAddress=EP1 OUT */
    0x02,               /* bmAttributes=bulk */
    0x40, 0x00,         /* wMaxPacketSize=64 */
    0x00,               /* bInterval=0 */

    /* EP2 IN bulk — datos CDC (7 bytes) */
    0x07, 0x05,         /* bLength=7, bDescriptorType=ENDPOINT */
    0x82,               /* bEndpointAddress=EP2 IN */
    0x02,               /* bmAttributes=bulk */
    0x40, 0x00,         /* wMaxPacketSize=64 */
    0x00,               /* bInterval=0 */

    /* --- Interface 2: J-Link Vendor (9 bytes) --- */
    0x09, 0x04,         /* bLength=9, bDescriptorType=INTERFACE */
    0x02,               /* bInterfaceNumber=2 */
    0x00,               /* bAlternateSetting=0 */
    0x02,               /* bNumEndpoints=2 */
    0xFF,               /* bInterfaceClass=Vendor */
    0xFF,               /* bInterfaceSubClass=Vendor */
    0xFF,               /* bInterfaceProtocol=Vendor */
    0x00,               /* iInterface=0 */

    /* EP3 OUT bulk — comandos J-Link (7 bytes) */
    0x07, 0x05,         /* bLength=7, bDescriptorType=ENDPOINT */
    0x03,               /* bEndpointAddress=EP3 OUT */
    0x02,               /* bmAttributes=bulk */
    0x40, 0x00,         /* wMaxPacketSize=64 */
    0x00,               /* bInterval=0 */

    /* EP3 IN bulk — respuestas J-Link (7 bytes) */
    0x07, 0x05,         /* bLength=7, bDescriptorType=ENDPOINT */
    0x83,               /* bEndpointAddress=EP3 IN */
    0x02,               /* bmAttributes=bulk */
    0x40, 0x00,         /* wMaxPacketSize=64 */
    0x00,               /* bInterval=0 */
};

/* ---------------------------------------------------------------------- */
/*  Descriptores de cadena                                                 */
/* ---------------------------------------------------------------------- */

static const unsigned char lang_descriptor[] = {
    4, USB_DT_STRING, 0x09, 0x04   /* English (US) */
};

static char serial_string[12];

static const unsigned char *descriptor_strings[] = {
    (const unsigned char *)"SEGGER",
    (const unsigned char *)"J-Link",
    (const unsigned char *)serial_string
};

/* ---------------------------------------------------------------------- */
/*  BOS + MS OS 2.0                                                       */
/* ---------------------------------------------------------------------- */

#define MS_OS_20_DESC_SET_LEN 30

const uint8_t bos_descriptor[] = {
    0x05, USB_DT_BOS, 0x21, 0x00, 0x01,
    0x1C, 0x10, 0x05, 0x00,
    0xDF, 0x60, 0xDD, 0xD8,
    0x89, 0x45, 0xC7, 0x4C,
    0x9C, 0xD2,
    0x65, 0x9D, 0x9E, 0x64, 0x8A, 0x9F,
    0x00, 0x00, 0x03, 0x06,
    MS_OS_20_DESC_SET_LEN, 0x00,
    MS_OS_20_VENDOR_CODE,
    0x00
};
const uint16_t bos_descriptor_len = sizeof(bos_descriptor);

const uint8_t ms_os_20_descriptor_set[] = {
    0x0A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x06,
    MS_OS_20_DESC_SET_LEN, 0x00,
    0x14, 0x00, 0x03, 0x00,
    'W', 'I', 'N', 'U', 'S', 'B', 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};
const uint16_t ms_os_20_descriptor_set_len = sizeof(ms_os_20_descriptor_set);

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
        {   /* EP3 OUT (0x03) — comandos J-Link */
            .descriptor       = &ep3_out,
            .handler          = &ep1_out_handler,
            .endpoint_control = &usb_dpram->ep_ctrl[2].out,
            .buffer_control   = &usb_dpram->ep_buf_ctrl[3].out,
            .data_buffer      = &usb_dpram->epx_data[0 * 64],
        },
        {   /* EP1 IN (0x81) — CDC notificaciones (interrupt, never armed) */
            .descriptor       = &cdc_ep_notify,
            .handler          = &cdc_notify_in_handler,
            .endpoint_control = &usb_dpram->ep_ctrl[0].in,
            .buffer_control   = &usb_dpram->ep_buf_ctrl[1].in,
            .data_buffer      = &usb_dpram->epx_data[1 * 64],
        },
        {   /* EP3 IN (0x83) — respuestas J-Link */
            .descriptor       = &ep3_in,
            .handler          = &ep2_in_handler,
            .endpoint_control = &usb_dpram->ep_ctrl[2].in,
            .buffer_control   = &usb_dpram->ep_buf_ctrl[3].in,
            .data_buffer      = &usb_dpram->epx_data[2 * 64],
        },
        {   /* EP1 OUT (0x01) — CDC datos host→device (absorber y descartar) */
            .descriptor       = &cdc_ep_data_out,
            .handler          = &cdc_data_out_handler,
            .endpoint_control = &usb_dpram->ep_ctrl[0].out,
            .buffer_control   = &usb_dpram->ep_buf_ctrl[1].out,
            .data_buffer      = &usb_dpram->epx_data[3 * 64],
        },
        {   /* EP2 IN (0x82) — CDC datos device→host (nunca armado) */
            .descriptor       = &cdc_ep_data_in,
            .handler          = &cdc_data_in_handler,
            .endpoint_control = &usb_dpram->ep_ctrl[1].in,
            .buffer_control   = &usb_dpram->ep_buf_ctrl[2].in,
            .data_buffer      = &usb_dpram->epx_data[4 * 64],
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
    uint8_t bLength = (uint8_t)(2 + strlen((const char *)str) * 2);
    *buf++ = bLength;
    *buf++ = USB_DT_STRING;
    uint8_t c;
    do {
        c = *str++;
        *buf++ = c;
        *buf++ = 0;
    } while (c != '\0');
    return bLength;
}
