#pragma once
/*
 * jlink_types.h — Tipos compatibles con la API pública de SEGGER JLinkARM.
 *
 * Sólo se definen los campos que usa nuestra implementación.  Si el software
 * destino requiere campos adicionales habrá que ampliar las estructuras.
 */

#include <stdint.h>

/* ---- Callback de logging ---- */
typedef void JLINKARM_LOG_FUNC(const char *msg);

/* ---- Estado del hardware (respuesta a JLINKARM_GetStatus) ---- */
typedef struct {
    uint16_t VTarget;   /* Tensión del target en mV */
    uint8_t  TDI;
    uint8_t  TDO;
    uint8_t  TCK;
    uint8_t  TMS;
    uint8_t  TRES;      /* nRST */
    uint8_t  TRST;      /* nTRST */
} JLINKARM_HW_STATUS;

/* ---- Descriptor de dispositivo en la cadena JTAG ---- */
typedef struct {
    uint32_t SizeOfStruct;  /* sizeof(JLINKARM_JTAG_DEVICE_CONF) */
    uint32_t IRLen;         /* Longitud del registro IR en bits */
    uint32_t Id;            /* IDCODE leído (rellena jtag_scan_chain) */
    uint32_t Caps;          /* Capacidades (reservado, 0) */
} JLINKARM_JTAG_DEVICE_CONF;

/* ---- Resultado de escaneo de cadena (GetIdChain) ---- */
typedef struct {
    uint32_t Id;            /* IDCODE del dispositivo */
    uint32_t IRLen;         /* Longitud IR detectada (no implementada: 0) */
} JLINKARM_JTAG_IDCODE_INFO;

/* ---- Información de emulador enumerado (GetList) ---- */
/* Layout idéntico al struct JLINKARM_EMU_INFO del TFG (JLinkAdapter.cpp).
 * Connection debe ser uint32_t y aIPAddr debe ser [16] para que los campos
 * acProduct / acFWString queden en los offsets correctos. */
typedef struct {
    uint32_t SerialNumber;
    uint32_t Connection;     /* 0=USB, 1=IP */
    uint32_t USBAddr;
    uint8_t  aIPAddr[16];
    int      Time;
    uint64_t Time2;
    uint32_t HWVersion;
    uint8_t  abMACAddr[6];
    char     acProduct[32];
    char     acNickName[32];
    char     acFWString[112];
    char     aDummy[32];
} JLINKARM_EMU_INFO;
