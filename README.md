# jlink-pico-probe

Firmware bare-metal para **Raspberry Pi Pico (RP2040)** que implementa el protocolo
**PicoAdapter** — una sonda JTAG de bajo coste para boundary scan y depuración.
La capa USB es completamente bare-metal (sin TinyUSB), basada en el ejemplo
`pico-examples/usb/device/dev_lowlevel` de Raspberry Pi.

El repositorio incluye también la **DLL de Windows** (`JLink_x64.dll`) que actúa de
interfaz entre el firmware y el software de aplicación, con una API compatible con
la DLL oficial de SEGGER.

## Tabla de contenidos

- [Descripción](#descripción)
- [Requisitos de hardware](#requisitos-de-hardware)
- [Requisitos de software — Firmware](#requisitos-de-software--firmware)
- [Requisitos de software — DLL](#requisitos-de-software--dll)
- [Instalación paso a paso](#instalación-paso-a-paso)
- [Compilar el firmware](#compilar-el-firmware)
- [Compilar la DLL](#compilar-la-dll)
- [Flashear el Pico](#flashear-el-pico)
- [Estructura del proyecto](#estructura-del-proyecto)
- [Solución de problemas](#solución-de-problemas)

---

## Descripción

El proyecto tiene dos componentes que se usan conjuntamente:

```
Software de aplicación (JtagScannerQt.exe)
      │
      │  carga JLink_x64.dll (nuestra DLL, drop-in de SEGGER)
      ▼
JLink_x64.dll
      │
      │  USB-CDC (puerto COM virtual) — protocolo PicoAdapter
      │  [0xA5][CMD][LEN_LO][LEN_HI][PAYLOAD][CRC8]
      ▼
Raspberry Pi Pico  ←── firmware de este repositorio
      │
      │  JTAG: TDI (GP16), TDO (GP17), TCK (GP18), TMS (GP19)
      │  Reset: nRST (GP20), nTRST (GP21)
      │  UART target: TX (GP12), RX (GP13)
      ▼
Circuito objetivo (FPGA, MCU, PCB...)
```

El firmware **no implementa el protocolo J-Link** — implementa el protocolo
PicoAdapter propio. La DLL traduce las llamadas `JLINKARM_*` de la API SEGGER a
comandos PicoAdapter sobre el puerto COM virtual (VID `2E8A`, PID `000A`) que
Windows crea automáticamente con el driver estándar `usbser.sys`.

---

## Requisitos de hardware

- **Raspberry Pi Pico** (RP2040) — cualquier variante: Pico original, Pico H, o clones compatibles
- **Cable micro-USB** de datos (no solo carga) — para conectar el Pico al PC
- **PC con Windows 10/11 x64**
- *(Opcional)* Level-shifter 3.3 V ↔ voltaje del target si el circuito objetivo no trabaja a 3.3 V

---

## Requisitos de software — Firmware

| Software | Versión probada | Para qué se usa |
|----------|----------------|-----------------|
| [Git](https://git-scm.com/download/win) | 2.42 | Clonar el repositorio |
| [Python](https://www.python.org/downloads/) | 3.13 (**no usar 3.14**) | Requerido por el SDK para generar el boot stage 2 |
| [Visual Studio Code](https://code.visualstudio.com/) | 1.80+ | Editor y entorno de compilación |
| [Extensión Raspberry Pi Pico (VS Code)](https://marketplace.visualstudio.com/items?itemName=raspberry-pi.raspberry-pi-pico) | 0.15+ | Descarga el SDK y el toolchain automáticamente |

La extensión descarga y gestiona en `%USERPROFILE%\.pico-sdk\`:

| Herramienta | Versión |
|-------------|---------|
| Pico SDK | 2.2.0 |
| ARM GCC (compilador cruzado) | 14.2 (14_2_Rel1) |
| CMake | 3.31.5 |
| Ninja | 1.12.1 |
| MinGW-w64 (para `pioasm`) | incluido |

> **No hace falta instalar CMake ni el compilador ARM manualmente.** La extensión de
> VS Code lo gestiona todo.

> **Python 3.14 no es compatible** con CMake 3.31. Usa la versión 3.13.

---

## Requisitos de software — DLL

| Software | Versión probada | Para qué se usa |
|----------|----------------|-----------------|
| [Visual Studio 2022](https://visualstudio.microsoft.com/es/vs/community/) | Community (gratuito) | Compilador MSVC C11, linker, herramientas CMake |
| Carga de trabajo **"Desarrollo de escritorio con C++"** | — | Instala MSVC v143, Windows SDK, cabeceras Win32 |

> No se puede usar MinGW ni GCC para compilar la DLL porque usa la API Win32 de
> bajo nivel (`SetupDI`, `CreateFile`, overlapped I/O). Se requiere MSVC.

> VS 2022 incluye CMake integrado. Si prefieres CMake independiente, instálalo desde
> [cmake.org](https://cmake.org/download/) (versión 3.20+).

---

## Instalación paso a paso

### 1. Instalar Git

Descarga e instala desde [https://git-scm.com/download/win](https://git-scm.com/download/win).
Opciones por defecto son correctas; asegúrate de añadirlo al PATH.

```cmd
git --version
```

### 2. Instalar Python 3.13

Descarga Python 3.13 desde [https://www.python.org/downloads/](https://www.python.org/downloads/).
Durante la instalación, **marca "Add python.exe to PATH"**.

O con winget:

```cmd
winget install Python.Python.3.13 --accept-source-agreements --accept-package-agreements
```

```cmd
python --version   # debe mostrar Python 3.13.x
```

### 3. Instalar VS Code y la extensión Raspberry Pi Pico

1. Descarga VS Code desde [https://code.visualstudio.com/](https://code.visualstudio.com/)
2. Abre VS Code → Extensiones (`Ctrl+Shift+X`) → busca **"Raspberry Pi Pico"** (autor: Raspberry Pi) → **Install**
3. La extensión descarga automáticamente el SDK y el toolchain.
   Verifica que existen estas rutas:

```
%USERPROFILE%\.pico-sdk\sdk\2.2.0\
%USERPROFILE%\.pico-sdk\toolchain\14_2_Rel1\bin\arm-none-eabi-gcc.exe
%USERPROFILE%\.pico-sdk\cmake\v3.31.5\bin\cmake.exe
%USERPROFILE%\.pico-sdk\ninja\v1.12.1\ninja.exe
```

Si no existen, abre el panel de la extensión (icono Raspberry Pi en la barra lateral)
y selecciona la opción para descargar el SDK.

### 4. Instalar Visual Studio 2022 (solo para compilar la DLL)

1. Descarga VS 2022 Community desde [https://visualstudio.microsoft.com/es/vs/community/](https://visualstudio.microsoft.com/es/vs/community/)
2. En el instalador selecciona la carga de trabajo **"Desarrollo de escritorio con C++"**
3. Instala (~6 GB en disco)

---

## Compilar el firmware

### Opción A — Desde VS Code (recomendado)

1. Clona el repositorio:
   ```cmd
   git clone <url-del-repo>
   ```
2. Abre la carpeta raíz del proyecto en VS Code: `File → Open Folder`
3. La extensión Raspberry Pi Pico detecta el `CMakeLists.txt` y configura el proyecto automáticamente
4. Haz clic en **"Compile Project"** en la barra inferior de VS Code
5. El archivo compilado estará en `build/jlink_pico_probe.uf2`

### Opción B — Desde CMD

Usa las herramientas descargadas por la extensión:

```cmd
git clone <url-del-repo>
cd jlink-pico-probe

set TOOLS=%USERPROFILE%\.pico-sdk
set PATH=%TOOLS%\cmake\v3.31.5\bin;%TOOLS%\ninja\v1.12.1;%TOOLS%\toolchain\14_2_Rel1\bin;%PATH%

cmake -B build -G Ninja -DPICO_SDK_PATH=%TOOLS%\sdk\2.2.0
cmake --build build --target jlink_pico_probe
```

Resultado: `build\jlink_pico_probe.uf2`.

---

## Compilar la DLL

Ejecutar desde el directorio `dll\` del repositorio, con una **símbolo del sistema
para desarrolladores de VS 2022** (busca "Developer Command Prompt for VS 2022"
en el menú Inicio) o desde cmd normal si MSVC está en el PATH:

### JLink_x64.dll (64 bits — uso habitual)

```cmd
cd dll
cmake -B build64 -G "Visual Studio 17 2022" -A x64
cmake --build build64 --config Release
```

Resultado: `dll\build64\Release\JLink_x64.dll`.

### JLinkARM.dll (32 bits — si se necesita)

```cmd
cd dll
cmake -B build32 -G "Visual Studio 17 2022" -A Win32 -DBUILD_ARM=1
cmake --build build32 --config Release
```

Resultado: `dll\build32\Release\JLinkARM.dll`.

### Desplegar la DLL

```cmd
REM Copiar al directorio del software de aplicación
copy dll\build64\Release\JLink_x64.dll 1_0\Proyecto-TFG-1.0\out\build\release\

REM Borrar la caché de localización de DLL (OBLIGATORIO tras actualizar la DLL)
del %TEMP%\jlink_dll_cache.txt
```

---

## Flashear el Pico

1. **Desconecta** el Pico del USB si está conectado
2. **Mantén pulsado el botón BOOTSEL** (botón blanco de la placa)
3. **Sin soltar BOOTSEL**, conecta el cable USB al PC
4. **Suelta BOOTSEL** — aparecerá una unidad USB llamada **RPI-RP2** en el explorador de archivos
5. **Copia** el archivo `build/jlink_pico_probe.uf2` a la unidad RPI-RP2
6. El Pico se reinicia automáticamente y ejecuta el firmware

Para volver a flashear, repite desde el paso 1.

---

## Estructura del proyecto

```
jlink-pico-probe/
├── CMakeLists.txt              ← Build system del firmware
├── pico_sdk_import.cmake       ← Localizador del Pico SDK
├── src/
│   ├── main.c                  ← Punto de entrada, bucle principal
│   ├── board/
│   │   ├── board_config.h      ← Pinout (JTAG GP16-19, UART GP12-13, LEDs GP14-15...)
│   │   ├── clock_init.c/.h     ← PLLs (125 MHz sys, 48 MHz USB)
│   │   └── gpio_init.c/.h      ← Configuración de GPIOs
│   ├── usb/
│   │   ├── usb_device.c/.h     ← Controlador USB bare-metal (IRQ-driven, DPRAM)
│   │   └── usb_descriptors.c/.h← Descriptores (VID=0x2E8A, PID=0x000A, 2 interfaces CDC)
│   ├── jtag/
│   │   ├── jtag_pio.c/.h       ← Motor JTAG con PIO + DMA
│   │   ├── jtag_tap.c/.h       ← Máquina de estados TAP (16 estados IEEE 1149.1)
│   │   └── jtag.pio            ← Programa PIO (8 instrucciones, LSB-first)
│   ├── cdc/
│   │   ├── cdc_uart.c/.h       ← Recepción de bytes del puerto COM virtual
│   │   └── pico_protocol.c/.h  ← Parser protocolo PicoAdapter (17 comandos, 0x01–0x22)
│   ├── uart/
│   │   └── uart_driver.c/.h    ← UART0 para debug del target (GP12 TX, GP13 RX, IRQ)
│   └── util/
│       ├── led.c/.h            ← Control de LEDs (verde GP14, rojo GP15)
│       └── adc.c/.h            ← Lectura Vref del target (GP26 / ADC0)
└── dll/
    ├── CMakeLists.txt          ← Build system de la DLL (MSVC, soporta x64 y x86)
    ├── JLink_x64.def           ← Exportaciones x64 (44 funciones)
    ├── JLinkARM.def            ← Exportaciones x86 (32-bit)
    └── src/
        ├── jlink_api.c         ← 44 funciones: JLINKARM_* + JLINK_PICO_* + PICO_UART_*
        ├── pico_transport.c/.h ← Capa serie (tramas PicoAdapter sobre puerto COM)
        ├── com_detect.c/.h     ← Detección del puerto COM (SetupDI + overlapped I/O)
        ├── jtag_chain.c/.h     ← JTAG de alto nivel (IDCODE, escaneo de cadena)
        ├── dll_state.h         ← Variables globales compartidas entre módulos
        └── jlink_types.h       ← Tipos y structs compatibles con la API SEGGER
```

---

## Solución de problemas

### "cmake no se reconoce como comando"

Usa la opción de compilación desde VS Code, o añade manualmente al PATH las herramientas
del pico-sdk como se muestra en la sección de compilación por CMD.

### "Python not found" durante cmake

```cmd
python --version   # debe mostrar 3.13.x
```

Si muestra 3.14, instala 3.13 y ajusta el PATH para que aparezca primero.

### El Pico no aparece como RPI-RP2

- El cable debe ser de **datos** (muchos cables USB solo tienen carga).
- Mantén BOOTSEL pulsado **antes** de conectar el USB, no después.
- Prueba con otro puerto USB del PC.

### El Pico no aparece como puerto COM en el Administrador de dispositivos

- Verifica que el firmware se flasheó correctamente (el LED debería encenderse).
- Windows instala `usbser.sys` automáticamente al detectar el dispositivo CDC
  (VID `2E8A`, PID `000A`). Si no, actualiza el driver desde el Administrador de dispositivos.

### La DLL no detecta la sonda / "Device not found"

1. Comprueba que aparece un puerto COMx en el Administrador de dispositivos
2. Borra la caché de localización de DLL: `del %TEMP%\jlink_dll_cache.txt`
3. Verifica que la DLL copiada al directorio del ejecutable es la nuestra y no la de SEGGER

### El LED no parpadea tras flashear

Verifica que el `.uf2` se copió completamente antes de desconectar y reintenta el proceso.

---

## Licencia

Este proyecto se desarrolla como Trabajo de Fin de Grado (TFG). Consulta el archivo LICENSE para los términos de uso.
