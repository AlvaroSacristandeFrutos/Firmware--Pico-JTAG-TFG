/*
 * Controlador USB en modo dispositivo — implementación bare-metal sin TinyUSB.
 * Adaptado de pico-examples/usb/device/dev_lowlevel/dev_lowlevel.c
 */

#ifndef USB_DEVICE_H
#define USB_DEVICE_H

#include <stdint.h>
#include <stdbool.h>
#include "usb_descriptors.h"

/* Inicializa el controlador USB en modo dispositivo.
 * Resetea el controlador, borra la DPRAM, configura los endpoints,
 * habilita interrupciones y pone D+ a alto para señalizar conexión. */
void usb_device_init(void);

/*
 * Punto de llamada desde el bucle principal. Actualmente no hace nada
 * porque todos los eventos USB se atienden en la ISR (isr_usbctrl).
 * Existe como hook para añadir lógica de polling si alguna vez se
 * deshabilitan las interrupciones.
 */
void usb_device_task(void);

/* Devuelve true una vez que el host ha enviado SET_CONFIGURATION. */
bool usb_is_configured(void);

/* Devuelve true una vez que SET_ADDRESS ha sido aplicado (el dispositivo
 * tiene dirección USB y la enumeración está completa). */
bool usb_is_enumerated(void);

/* Inicia una transferencia en el endpoint dado.
 * Para endpoints IN (TX), copia los datos de buf a la DPRAM.
 * Para endpoints OUT (RX), buf se ignora; los datos se leen tras la
 * compleción. Máximo 64 bytes por transferencia. */
void usb_start_transfer(struct usb_endpoint_configuration *ep,
                        uint8_t *buf, uint16_t len);

/* Busca la configuración de un endpoint por su dirección.
 * Devuelve NULL si no existe. */
struct usb_endpoint_configuration *usb_get_endpoint_configuration(uint8_t addr);

/* Devuelve el número de resets de bus USB desde el arranque. */
uint32_t usb_get_bus_reset_count(void);

/* Envía datos al host por CDC Data IN (EP3 IN, 0x83).
 * Fragmenta en chunks de 64 bytes y bloquea (spin con ISR activa)
 * hasta que todos los chunks son aceptados por el hardware. */
void cdc_send(const uint8_t *data, uint16_t len);

#endif /* USB_DEVICE_H */
