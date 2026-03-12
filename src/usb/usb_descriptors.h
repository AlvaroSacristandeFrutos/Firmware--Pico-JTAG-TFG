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
     * Incluye IAD + CDC (interfaces 0+1) + J-Link vendor (interface 2).
     */
    const uint8_t   *config_desc_full;
    uint16_t         config_desc_len;

    const unsigned char  *lang_descriptor;
    const unsigned char **descriptor_strings;

    /* USB_NUM_ENDPOINTS = 16 (from SDK) */
    struct usb_endpoint_configuration endpoints[USB_NUM_ENDPOINTS];
};

/* ---- Endpoint addresses ---- */
#define EP0_IN_ADDR       (USB_DIR_IN  | 0)   /* 0x80 — control IN  */
#define EP0_OUT_ADDR      (USB_DIR_OUT | 0)   /* 0x00 — control OUT */

/*
 * CDC endpoints (interfaces 0 y 1):
 *   EP1 IN  (0x81) interrupt — notificaciones CDC (nunca armado)
 *   EP3 OUT (0x03) bulk     — datos CDC host→device (absorber y descartar)
 *   EP3 IN  (0x83) bulk     — datos CDC device→host (nunca armado)
 *
 * CDC usa EP3 para datos (no EP1/EP2) para dejar EP2 libre para J-Link.
 * La presencia de CDC hace que jlink.sys identifique el device como
 * J-Link V9 y use el protocolo estándar (host-initiated).
 */
#define CDC_EP_NOTIFY     (USB_DIR_IN  | 1)   /* 0x81 — interrupt IN: notify CDC */
#define CDC_EP_DATA_OUT   (USB_DIR_OUT | 3)   /* 0x03 — bulk OUT: datos CDC      */
#define CDC_EP_DATA_IN    (USB_DIR_IN  | 3)   /* 0x83 — bulk IN:  datos CDC      */

/*
 * J-Link endpoints (interface 2, class 0xFF/0xFF/0xFF):
 *   EP2 OUT (0x02) bulk — comandos J-Link (host → device)
 *   EP2 IN  (0x82) bulk — respuestas J-Link (device → host)
 */
#define JLINK_EP_CMD_OUT  (USB_DIR_OUT | 2)   /* 0x02 — bulk OUT comandos  */
#define JLINK_EP_RSP_IN   (USB_DIR_IN  | 2)   /* 0x82 — bulk IN respuestas */

/* Aliases usados en usb_device.c */
#define EP1_OUT_ADDR  JLINK_EP_CMD_OUT   /* 0x02 — J-Link comandos */
#define EP2_IN_ADDR   JLINK_EP_RSP_IN    /* 0x82 — J-Link respuestas */

/* ---- Extern declarations ---- */
extern struct usb_device_configuration dev_config;

/* Initialize the serial number string from flash unique ID.
 * Must be called before USB enumeration. */
void usb_descriptors_init(void);

/* Convert a C string to a USB string descriptor in the provided buffer.
 * Returns the descriptor length. */
uint8_t usb_prepare_string_descriptor(const unsigned char *str, uint8_t *buf);

#endif /* USB_DESCRIPTORS_H */
