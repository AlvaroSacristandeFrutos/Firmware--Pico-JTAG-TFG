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

/*
 * Lee los datos recibidos en EP1 OUT (bulk de vendor).
 * Devuelve el número de bytes copiados en buf, o 0 si no hay datos.
 *
 * Importante: esta función NO rearma EP1 OUT. Hay que llamar a
 * usb_vendor_arm_rx() después de haber enviado la respuesta por EP1 IN,
 * para evitar tener ambos endpoints del mismo número activos a la vez
 * (comportamiento problemático en el RP2040).
 */
uint16_t usb_vendor_read(uint8_t *buf, uint16_t max_len);

/*
 * Rearma EP1 OUT para recibir el siguiente paquete del host.
 * Llamar siempre después de que la respuesta en EP1 IN haya completado.
 */
void usb_vendor_arm_rx(void);

/* Envía datos por EP1 IN (bulk de vendor).
 * Devuelve true si la transferencia se inició, false si el endpoint
 * estaba ocupado con una transferencia anterior. */
bool usb_vendor_write(const uint8_t *data, uint16_t len);

/* Devuelve true si EP1 IN tiene una transferencia pendiente.
 * Consulta el registro hardware directamente, no solo el flag interno. */
bool usb_vendor_tx_busy(void);

/* Devuelve el número de resets de bus USB desde el arranque. */
uint32_t usb_get_bus_reset_count(void);

#endif /* USB_DEVICE_H */
