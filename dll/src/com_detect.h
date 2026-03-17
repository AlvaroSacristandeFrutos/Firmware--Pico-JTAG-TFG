#pragma once
/*
 * com_detect.h — Detección automática del PicoAdapter en los puertos COM.
 */

#include <stddef.h>
#include <stdbool.h>

/*
 * Busca el primer PicoAdapter conectado al sistema.
 *
 * Algoritmo:
 *   A) Buscar en el registro el VID=0x2E8A / PID=0x000A (Raspberry Pi Pico)
 *      y verificar con handshake PING + GET_VERSION.
 *   B) Si A falla, escanear COM1-COM64 e intentar el handshake en cada uno.
 *
 * Parámetros:
 *   out_port  — buffer donde se copia el nombre del puerto ("COM7")
 *   out_size  — tamaño del buffer
 *
 * Devuelve true si se encontró un PicoAdapter, false en caso contrario.
 */
bool pico_detect(char *out_port, size_t out_size);
