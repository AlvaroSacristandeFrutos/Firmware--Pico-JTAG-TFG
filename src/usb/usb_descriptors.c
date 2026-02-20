/*
 * Descriptores USB para J-Link Pico Probe.
 * Adaptado de pico-examples/usb/device/dev_lowlevel/dev_lowlevel.h
 * (Copyright (c) 2020 Raspberry Pi (Trading) Ltd, licencia BSD-3-Clause)
 */

#include "usb_descriptors.h"
#include "pico/unique_id.h"
#include <string.h>

/*
 * Declaraciones adelantadas de los handlers de endpoint, que están
 * definidos en usb_device.c. Se declaran aquí porque usb_descriptors.c
 * necesita sus direcciones para rellenar la tabla dev_config.endpoints,
 * pero no tiene acceso al .c donde se definen.
 *
 * Nota: ep2_in_handler es el handler de EP1 IN — el nombre viene del
 * ejemplo dev_lowlevel original donde el endpoint de vendor era EP2.
 */
void ep0_in_handler(uint8_t *buf, uint16_t len);
void ep0_out_handler(uint8_t *buf, uint16_t len);
void ep1_out_handler(uint8_t *buf, uint16_t len);
void ep2_in_handler(uint8_t *buf, uint16_t len);

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

/* EP1 OUT — el host envía comandos J-Link por aquí */
static const struct usb_endpoint_descriptor ep1_out = {
    .bLength          = sizeof(struct usb_endpoint_descriptor),
    .bDescriptorType  = USB_DT_ENDPOINT,
    .bEndpointAddress = EP1_OUT_ADDR,
    .bmAttributes     = USB_TRANSFER_TYPE_BULK,
    .wMaxPacketSize   = 64,
    .bInterval        = 0
};

/* EP1 IN — el probe envía respuestas al host por aquí */
static const struct usb_endpoint_descriptor ep1_in = {
    .bLength          = sizeof(struct usb_endpoint_descriptor),
    .bDescriptorType  = USB_DT_ENDPOINT,
    .bEndpointAddress = EP1_IN_ADDR,
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
    .bcdUSB             = 0x0200,   /* USB 2.0, igual que un J-Link real */
    .bDeviceClass       = 0x00,     /* Clase definida por la interfaz */
    .bDeviceSubClass    = 0x00,
    .bDeviceProtocol    = 0x00,
    .bMaxPacketSize0    = 64,
    .idVendor           = 0x1366,   /* SEGGER — temporal para validar el protocolo */
    .idProduct          = 0x0101,   /* J-Link — temporal para validar el protocolo */
    .bcdDevice          = 0x0100,   /* Versión de dispositivo 1.0 */
    .iManufacturer      = 1,
    .iProduct           = 2,
    .iSerialNumber      = 3,
    .bNumConfigurations = 1
};

/* ---------------------------------------------------------------------- */
/*  Descriptor de interfaz                                                 */
/* ---------------------------------------------------------------------- */

static const struct usb_interface_descriptor interface_descriptor = {
    .bLength            = sizeof(struct usb_interface_descriptor),
    .bDescriptorType    = USB_DT_INTERFACE,
    .bInterfaceNumber   = 0,
    .bAlternateSetting  = 0,
    .bNumEndpoints      = 2,        /* EP1 OUT + EP1 IN */
    .bInterfaceClass    = 0xFF,     /* Vendor specific */
    .bInterfaceSubClass = 0x00,
    .bInterfaceProtocol = 0x00,
    .iInterface         = 0
};

/* ---------------------------------------------------------------------- */
/*  Descriptor de configuración                                            */
/* ---------------------------------------------------------------------- */

static const struct usb_configuration_descriptor config_descriptor = {
    .bLength             = sizeof(struct usb_configuration_descriptor),
    .bDescriptorType     = USB_DT_CONFIG,
    .wTotalLength        = sizeof(struct usb_configuration_descriptor) +
                           sizeof(struct usb_interface_descriptor) +
                           sizeof(struct usb_endpoint_descriptor) +
                           sizeof(struct usb_endpoint_descriptor),
    .bNumInterfaces      = 1,
    .bConfigurationValue = 1,
    .iConfiguration      = 0,
    .bmAttributes        = 0xC0,    /* Alimentado por el propio dispositivo */
    .bMaxPower           = 0x32     /* 100 mA (unidades de 2 mA) */
};

/* ---------------------------------------------------------------------- */
/*  Descriptores de cadena                                                 */
/* ---------------------------------------------------------------------- */

static const unsigned char lang_descriptor[] = {
    4,          /* bLength */
    USB_DT_STRING,
    0x09, 0x04  /* English (US) — código de idioma estándar */
};

/*
 * Número de serie en formato de cadena ASCII.
 * Se genera en tiempo de ejecución a partir del ID único de flash del RP2040
 * (ver usb_descriptors_init). El formato es numérico para ser compatible con
 * el formato que usa J-Link en sus números de serie.
 * 12 bytes: 9 dígitos + terminador nulo + 2 de relleno para alineación a 4 bytes.
 */
static char serial_string[12];

static const unsigned char *descriptor_strings[] = {
    (const unsigned char *)"SEGGER",               /* Índice 1: Fabricante */
    (const unsigned char *)"J-Link",               /* Índice 2: Producto */
    (const unsigned char *)serial_string            /* Índice 3: Número de serie */
};

/* ---------------------------------------------------------------------- */
/*  BOS descriptor y MS OS 2.0                                            */
/*                                                                         */
/*  El BOS (Binary device Object Store) es un mecanismo USB 2.1 para      */
/*  anunciar capabilities adicionales. Windows lo consulta y, al ver la   */
/*  capability de plataforma MS OS 2.0, hace una petición de vendor para   */
/*  obtener el descriptor set, que le indica qué driver asignar.          */
/*  Con VID/PID propios esto hace que Windows cargue WinUSB.sys           */
/*  automáticamente sin necesidad de un .inf.                             */
/* ---------------------------------------------------------------------- */

/* Longitud total del MS OS 2.0 descriptor set: cabecera (10) + CompatID (20) */
#define MS_OS_20_DESC_SET_LEN 30

/*
 * BOS descriptor = cabecera BOS (5 bytes) + Platform Capability MS OS 2.0 (28 bytes)
 * Total: 33 bytes.
 * Referencia: Microsoft OS 2.0 Descriptors Specification, Tables 1 y 4.
 */
const uint8_t bos_descriptor[] = {
    /* Cabecera BOS */
    0x05,                           /* bLength = 5 */
    USB_DT_BOS,                     /* bDescriptorType = 0x0F */
    0x21, 0x00,                     /* wTotalLength = 33 (little-endian) */
    0x01,                           /* bNumDeviceCaps = 1 */

    /* Platform Capability Descriptor para MS OS 2.0 */
    0x1C,                           /* bLength = 28 */
    0x10,                           /* bDescriptorType = DEVICE CAPABILITY */
    0x05,                           /* bDevCapabilityType = PLATFORM */
    0x00,                           /* bReserved */
    /* PlatformCapabilityUUID = {D8DD60DF-4589-4CC7-9CD2-659D9E648A9F} */
    0xDF, 0x60, 0xDD, 0xD8,
    0x89, 0x45,
    0xC7, 0x4C,
    0x9C, 0xD2,
    0x65, 0x9D, 0x9E, 0x64, 0x8A, 0x9F,
    /* Datos específicos de vendor para MS OS 2.0 */
    0x00, 0x00, 0x03, 0x06,         /* dwWindowsVersion = 0x06030000 (Windows 8.1+) */
    MS_OS_20_DESC_SET_LEN, 0x00,    /* wMSOSDescriptorSetTotalLength (little-endian) */
    MS_OS_20_VENDOR_CODE,           /* bMS_VendorCode — código para la petición de vendor */
    0x00                            /* bAltEnumCode = 0 (sin enumeración alternativa) */
};
const uint16_t bos_descriptor_len = sizeof(bos_descriptor);

/*
 * MS OS 2.0 Descriptor Set = Cabecera (10 bytes) + Compatible ID (20 bytes) = 30 bytes.
 * Se devuelve en respuesta a la petición de vendor bRequest=0x01, wIndex=0x07.
 * El campo CompatibleID "WINUSB" le indica a Windows que cargue WinUSB.sys.
 */
const uint8_t ms_os_20_descriptor_set[] = {
    /* Cabecera del MS OS 2.0 Descriptor Set */
    0x0A, 0x00,                     /* wLength = 10 */
    0x00, 0x00,                     /* wDescriptorType = MS_OS_20_SET_HEADER_DESCRIPTOR */
    0x00, 0x00, 0x03, 0x06,         /* dwWindowsVersion = 0x06030000 */
    MS_OS_20_DESC_SET_LEN, 0x00,    /* wTotalLength (little-endian) */

    /* Compatible ID Feature Descriptor */
    0x14, 0x00,                     /* wLength = 20 */
    0x03, 0x00,                     /* wDescriptorType = MS_OS_20_FEATURE_COMPATIBLE_ID */
    'W', 'I', 'N', 'U', 'S', 'B', 0x00, 0x00,  /* CompatibleID: "WINUSB" */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00  /* SubCompatibleID: vacío */
};
const uint16_t ms_os_20_descriptor_set_len = sizeof(ms_os_20_descriptor_set);

/* ---------------------------------------------------------------------- */
/*  Instancia de configuración del dispositivo                             */
/* ---------------------------------------------------------------------- */

struct usb_device_configuration dev_config = {
    .device_descriptor    = &device_descriptor,
    .interface_descriptor = &interface_descriptor,
    .config_descriptor    = &config_descriptor,
    .lang_descriptor      = lang_descriptor,
    .descriptor_strings   = descriptor_strings,
    .endpoints = {
        {   /* EP0 OUT — control saliente */
            .descriptor       = &ep0_out,
            .handler          = &ep0_out_handler,
            .endpoint_control = NULL,   /* EP0 no tiene registro endpoint_control */
            .buffer_control   = &usb_dpram->ep_buf_ctrl[0].out,
            .data_buffer      = &usb_dpram->ep0_buf_a[0],
        },
        {   /* EP0 IN — control entrante */
            .descriptor       = &ep0_in,
            .handler          = &ep0_in_handler,
            .endpoint_control = NULL,   /* EP0 no tiene registro endpoint_control */
            .buffer_control   = &usb_dpram->ep_buf_ctrl[0].in,
            .data_buffer      = &usb_dpram->ep0_buf_a[0],
        },
        {   /* EP1 OUT — comandos J-Link del host al probe */
            .descriptor       = &ep1_out,
            .handler          = &ep1_out_handler,
            .endpoint_control = &usb_dpram->ep_ctrl[0].out,
            .buffer_control   = &usb_dpram->ep_buf_ctrl[1].out,
            .data_buffer      = &usb_dpram->epx_data[0 * 64],
        },
        {   /* EP1 IN — respuestas del probe al host */
            .descriptor       = &ep1_in,
            .handler          = &ep2_in_handler,  /* ver nota sobre el nombre en usb_device.c */
            .endpoint_control = &usb_dpram->ep_ctrl[0].in,
            .buffer_control   = &usb_dpram->ep_buf_ctrl[1].in,
            .data_buffer      = &usb_dpram->epx_data[1 * 64],
        }
    }
};

/* ---------------------------------------------------------------------- */
/*  Funciones públicas                                                     */
/* ---------------------------------------------------------------------- */

/*
 * Deriva un número de serie numérico de 9 dígitos a partir del ID único
 * de flash del RP2040 (8 bytes). Usamos FNV-1a para reducir los 8 bytes a
 * un uint32 y luego tomamos módulo 10^9 para obtener 9 dígitos decimales.
 * El resultado es determinista: el mismo chip produce siempre el mismo número.
 */
static uint32_t derive_serial_number(void) {
    pico_unique_board_id_t id;
    pico_get_unique_board_id(&id);
    uint32_t hash = 2166136261u;    /* offset base FNV-1a de 32 bits */
    for (int i = 0; i < PICO_UNIQUE_BOARD_ID_SIZE_BYTES; i++) {
        hash ^= id.id[i];
        hash *= 16777619u;          /* primo FNV */
    }
    return hash % 1000000000u;      /* 9 dígitos decimales */
}

void usb_descriptors_init(void) {
    uint32_t sn = derive_serial_number();
    /* Formatear como cadena decimal de 9 dígitos con ceros por la izquierda */
    for (int i = 8; i >= 0; i--) {
        serial_string[i] = '0' + (sn % 10);
        sn /= 10;
    }
    serial_string[9] = '\0';
}

uint8_t usb_prepare_string_descriptor(const unsigned char *str, uint8_t *buf) {
    /* El descriptor de cadena USB usa UTF-16LE: cada carácter ASCII ocupa 2 bytes.
     * El encabezado son 2 bytes (bLength + bDescriptorType). */
    uint8_t bLength = (uint8_t)(2 + strlen((const char *)str) * 2);
    *buf++ = bLength;
    *buf++ = USB_DT_STRING;

    uint8_t c;
    do {
        c = *str++;
        *buf++ = c;
        *buf++ = 0;     /* byte alto de UTF-16LE siempre 0 para ASCII */
    } while (c != '\0');

    return bLength;
}
