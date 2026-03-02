/*
 * USB descriptors and endpoint configuration for J-Link Pico Probe.
 * Adapted from pico-examples/usb/device/dev_lowlevel/dev_lowlevel.h
 * (Copyright (c) 2020 Raspberry Pi (Trading) Ltd, BSD-3-Clause)
 */

#ifndef USB_DESCRIPTORS_H
#define USB_DESCRIPTORS_H

#include "usb_common.h"

/* ---- Endpoint handler callback type ---- */
typedef void (*usb_ep_handler)(uint8_t *buf, uint16_t len);

/* ---- Endpoint configuration (from dev_lowlevel.h) ---- */
struct usb_endpoint_configuration {
    const struct usb_endpoint_descriptor *descriptor;
    usb_ep_handler handler;

    /* Pointers to endpoint + buffer control registers in DPRAM */
    volatile uint32_t *endpoint_control;
    volatile uint32_t *buffer_control;
    volatile uint8_t  *data_buffer;

    /* Toggle after each packet (unless replying to a SETUP) */
    uint8_t next_pid;
};

/* ---- Device configuration ---- */
struct usb_device_configuration {
    const struct usb_device_descriptor  *device_descriptor;

    /*
     * Descriptor de configuración completo, pre-construido como array de bytes.
     * Incluye el header de configuración, el IAD, las tres interfaces
     * (CDC Control + CDC Data + J-Link vendor) y todos sus endpoints y
     * los descriptores funcionales CDC.
     */
    const uint8_t   *config_desc_full;
    uint16_t         config_desc_len;

    const unsigned char  *lang_descriptor;
    const unsigned char **descriptor_strings;

    /* USB_NUM_ENDPOINTS = 16 (from SDK) */
    struct usb_endpoint_configuration endpoints[USB_NUM_ENDPOINTS];
};

/* ---- Endpoint addresses ---- */
#define EP0_IN_ADDR    (USB_DIR_IN  | 0)   /* 0x80 — control IN  */
#define EP0_OUT_ADDR   (USB_DIR_OUT | 0)   /* 0x00 — control OUT */

/* CDC endpoints (interfaces 0 y 1) */
#define CDC_EP_NOTIFY  (USB_DIR_IN  | 1)   /* 0x81 — interrupt IN: notificaciones CDC */
#define CDC_EP_DATA_OUT (USB_DIR_OUT | 1)  /* 0x01 — bulk OUT: datos CDC host→device */
#define CDC_EP_DATA_IN  (USB_DIR_IN  | 2)  /* 0x82 — bulk IN: datos CDC device→host */

/* J-Link endpoints (interface 2) */
#define EP3_OUT_ADDR   (USB_DIR_OUT | 3)   /* 0x03 — bulk OUT: comandos J-Link */
#define EP3_IN_ADDR    (USB_DIR_IN  | 3)   /* 0x83 — bulk IN:  respuestas J-Link */

/* Aliases para no romper referencias antiguas en usb_device.c */
#define EP1_OUT_ADDR   EP3_OUT_ADDR
#define EP2_IN_ADDR    EP3_IN_ADDR

/* ---- Extern declarations ---- */
extern struct usb_device_configuration dev_config;

/* Initialize the serial number string from flash unique ID.
 * Must be called before USB enumeration. */
void usb_descriptors_init(void);

/* Convert a C string to a USB string descriptor in the provided buffer.
 * Returns the descriptor length. */
uint8_t usb_prepare_string_descriptor(const unsigned char *str, uint8_t *buf);

/* BOS descriptor (includes MS OS 2.0 Platform Capability) */
extern const uint8_t  bos_descriptor[];
extern const uint16_t bos_descriptor_len;

/* MS OS 2.0 Descriptor Set (returned via vendor request) */
extern const uint8_t  ms_os_20_descriptor_set[];
extern const uint16_t ms_os_20_descriptor_set_len;

/* Vendor request code for MS OS 2.0 descriptor set */
#define MS_OS_20_VENDOR_CODE  0xBF

#endif /* USB_DESCRIPTORS_H */
