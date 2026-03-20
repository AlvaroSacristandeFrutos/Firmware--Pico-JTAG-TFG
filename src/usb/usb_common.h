/*
 * USB 2.0 standard definitions and descriptor structs.
 * Adapted from pico-examples/usb/device/dev_lowlevel/usb_common.h
 * (Copyright (c) 2020 Raspberry Pi (Trading) Ltd, BSD-3-Clause)
 */

#ifndef USB_COMMON_H
#define USB_COMMON_H

#include "pico/types.h"
#include "hardware/structs/usb.h"

/* bmRequestType */
#define USB_REQ_TYPE_TYPE_CLASS         0x20u
#define USB_REQ_TYPE_TYPE_VENDOR        0x40u
#define USB_REQ_TYPE_RECIPIENT_MASK     0x1fu
#define USB_REQ_TYPE_RECIPIENT_ENDPOINT 0x02u
#define USB_DIR_OUT                     0x00u
#define USB_DIR_IN                      0x80u

/* Transfer types (bmAttributes) */
#define USB_TRANSFER_TYPE_CONTROL       0x0
#define USB_TRANSFER_TYPE_BULK          0x2
#define USB_TRANSFER_TYPE_INTERRUPT     0x3

/* Descriptor types */
#define USB_DT_DEVICE                   0x01
#define USB_DT_CONFIG                   0x02
#define USB_DT_STRING                   0x03
#define USB_DT_ENDPOINT                 0x05

/* Standard request codes */
#define USB_REQUEST_CLEAR_FEATURE       0x01
#define USB_REQUEST_SET_ADDRESS         0x05
#define USB_REQUEST_GET_DESCRIPTOR      0x06
#define USB_REQUEST_SET_CONFIGURATION   0x09

/* Feature selectors */
#define USB_FEAT_ENDPOINT_HALT          0x00

/* ---- Packed descriptor structs ---- */

struct usb_setup_packet {
    uint8_t  bmRequestType;
    uint8_t  bRequest;
    uint16_t wValue;
    uint16_t wIndex;
    uint16_t wLength;
} __packed;

struct usb_device_descriptor {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint16_t bcdUSB;
    uint8_t  bDeviceClass;
    uint8_t  bDeviceSubClass;
    uint8_t  bDeviceProtocol;
    uint8_t  bMaxPacketSize0;
    uint16_t idVendor;
    uint16_t idProduct;
    uint16_t bcdDevice;
    uint8_t  iManufacturer;
    uint8_t  iProduct;
    uint8_t  iSerialNumber;
    uint8_t  bNumConfigurations;
} __packed;

struct usb_endpoint_descriptor {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint8_t  bEndpointAddress;
    uint8_t  bmAttributes;
    uint16_t wMaxPacketSize;
    uint8_t  bInterval;
} __packed;

#endif /* USB_COMMON_H */
