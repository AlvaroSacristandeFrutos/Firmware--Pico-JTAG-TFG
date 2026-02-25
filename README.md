# jlink-pico-probe

Firmware bare-metal para **Raspberry Pi Pico (RP2040)** que emula una sonda de depuración **J-Link V9** ante el driver `jlink.sys` de SEGGER. La capa USB es completamente bare-metal (sin TinyUSB), basada en el ejemplo `pico-examples/usb/device/dev_lowlevel` de Raspberry Pi.

## Tabla de contenidos

- [Descripción](#descripción)
- [Requisitos de hardware](#requisitos-de-hardware)
- [Requisitos de software](#requisitos-de-software)
- [Instalación paso a paso](#instalación-paso-a-paso)
- [Compilar el proyecto](#compilar-el-proyecto)
- [Flashear el Pico](#flashear-el-pico)
- [Verificar que funciona](#verificar-que-funciona)
- [Estructura del proyecto](#estructura-del-proyecto)
- [Solución de problemas](#solución-de-problemas)

---

## Descripción

Este proyecto implementa el protocolo propietario J-Link de SEGGER sobre un Raspberry Pi Pico de bajo coste (~4 €), permitiendo usarlo como sonda de depuración JTAG compatible con SEGGER J-Link Commander y OpenOCD. El firmware accede directamente a los registros del controlador USB del RP2040 sin depender de librerías externas como TinyUSB.

## Requisitos de hardware

- **Raspberry Pi Pico** (RP2040) — cualquier variante: Pico original, Pico H, o clones compatibles con RP2040
- **Cable micro-USB** (datos, no solo carga) — para conectar el Pico al PC
- **PC con Windows 10/11** — las instrucciones están escritas para Windows; en Linux el proceso es similar pero con otras rutas

## Requisitos de software

| Software | Versión mínima | Para qué se usa |
|----------|---------------|-----------------|
| [Visual Studio Code](https://code.visualstudio.com/) | 1.80+ | Editor y entorno de compilación |
| [Extensión Raspberry Pi Pico (VS Code)](https://marketplace.visualstudio.com/items?itemName=raspberry-pi.raspberry-pi-pico) | 0.15+ | Descarga automática del SDK, toolchain y herramientas |
| [Git](https://git-scm.com/download/win) | 2.40+ | Clonar el repositorio |
| [Python](https://www.python.org/downloads/) | 3.9 – 3.13 | Requerido por el SDK para generar el boot stage 2 |

> **Importante:** Python 3.14 (pre-release) **no es compatible** con cmake 3.31. Usa Python 3.13 o inferior.

---

## Instalación paso a paso

### 1. Instalar Git

Descarga e instala Git desde [https://git-scm.com/download/win](https://git-scm.com/download/win).

Durante la instalación, las opciones por defecto son correctas. Asegúrate de que se añade al PATH del sistema.

Verifica la instalación abriendo una terminal (`cmd` o PowerShell):

```cmd
git --version
```

Debe mostrar algo como `git version 2.47.1.windows.1`.

### 2. Instalar Python 3.13

**Opción A — Desde la web:**

Descarga Python 3.13 desde [https://www.python.org/downloads/](https://www.python.org/downloads/). Durante la instalación, **marca la casilla "Add python.exe to PATH"**.

**Opción B — Desde terminal (winget):**

```cmd
winget install Python.Python.3.13 --accept-source-agreements --accept-package-agreements
```

Verifica:

```cmd
python --version
```

Debe mostrar `Python 3.13.x`. Si muestra 3.14, necesitas instalar 3.13 y asegurarte de que aparece primero en el PATH.

### 3. Instalar Visual Studio Code

Descarga e instala VS Code desde [https://code.visualstudio.com/](https://code.visualstudio.com/).

### 4. Instalar la extensión Raspberry Pi Pico

Esta extensión es la pieza clave. Se encarga de descargar automáticamente **todo lo necesario** para compilar firmware de RP2040:

- Pico SDK 2.2.0
- ARM GCC 14.2 (compilador cruzado para RP2040)
- CMake 3.31.5
- Ninja 1.12.1
- MinGW-w64 (compilador C++ para herramientas del host como `pioasm`)

**Pasos:**

1. Abre VS Code
2. Ve a la pestaña de extensiones (`Ctrl+Shift+X`)
3. Busca **"Raspberry Pi Pico"** (autor: Raspberry Pi)
4. Haz clic en **Install**
5. Tras instalar, aparecerá un icono de Raspberry Pi en la barra lateral izquierda
6. Haz clic en él y sigue el asistente de configuración si aparece
7. La extensión descargará las herramientas en `%USERPROFILE%\.pico-sdk\` (típicamente `C:\Users\TU_USUARIO\.pico-sdk\`). Este proceso puede tardar varios minutos la primera vez.

**Verifica que la descarga se completó:** comprueba que existe la carpeta:

```
%USERPROFILE%\.pico-sdk\sdk\2.2.0\
```

Si no existe, abre el panel de la extensión (icono Raspberry Pi) y busca una opción para seleccionar/descargar la versión del SDK.

### 5. Instalar SEGGER J-Link Software (opcional, para pruebas)

Para verificar que el firmware funciona correctamente necesitas el software de SEGGER:

1. Descarga "J-Link Software and Documentation Pack" desde [https://www.segger.com/downloads/jlink/](https://www.segger.com/downloads/jlink/)
2. Requiere aceptar la licencia de uso (es gratuito para evaluación)
3. Instala con las opciones por defecto
4. Tras instalar tendrás disponible `JLink.exe` (J-Link Commander) en `C:\Program Files\SEGGER\JLink\`

---

## Compilar el proyecto

### Opción A — Desde VS Code (recomendado)

1. Clona el repositorio:

```cmd
git clone <url-del-repo>
```

2. Abre la carpeta del proyecto en VS Code: `File → Open Folder → selecciona la carpeta del repo`

3. La extensión Raspberry Pi Pico detectará el `CMakeLists.txt` automáticamente y configurará el proyecto. Espera a que termine (barra inferior de VS Code muestra el progreso).

4. Haz clic en el botón **"Compile Project"** en la barra inferior de VS Code (icono de engranaje o texto "Build").

5. El archivo compilado estará en `build/jlink_pico_probe.uf2`.

### Opción B — Desde línea de comandos (CMD)

Si prefieres compilar sin VS Code, puedes usar directamente las herramientas que descargó la extensión:

```cmd
git clone <url-del-repo>
cd jlink-pico-probe

REM Configurar el PATH con las herramientas del SDK
set TOOLS=%USERPROFILE%\.pico-sdk
set PATH=%TOOLS%\cmake\v3.31.5\bin;%TOOLS%\ninja\v1.12.1;%TOOLS%\toolchain\14_2_Rel1\bin;%PATH%

REM Configurar y compilar
cmake -B build -G Ninja -DPICO_SDK_PATH=%TOOLS%\sdk\2.2.0
cmake --build build --target jlink_pico_probe
```

El resultado es `build\jlink_pico_probe.uf2`.

> **Nota:** Las rutas exactas de las versiones pueden variar si la extensión descargó versiones diferentes. Comprueba los nombres de las carpetas dentro de `%USERPROFILE%\.pico-sdk\`.

---

## Flashear el Pico

1. **Desconecta** el Pico del USB si está conectado.
2. **Mantén pulsado el botón BOOTSEL** del Pico (el botón blanco de la placa).
3. **Sin soltar BOOTSEL**, conecta el cable USB al PC.
4. **Suelta BOOTSEL**. Aparecerá una unidad USB llamada **RPI-RP2** en el explorador de archivos.
5. **Copia** (arrastra) el archivo `build/jlink_pico_probe.uf2` a la unidad RPI-RP2.
6. El Pico se reiniciará automáticamente y comenzará a ejecutar el firmware.

> Para volver a flashear, repite el proceso desde el paso 1.

---


## Estructura del proyecto

```
jlink-pico-probe/
├── CMakeLists.txt              ← Build system principal
├── pico_sdk_import.cmake       ← Localizador del Pico SDK
├── src/
│   ├── main.c                  ← Punto de entrada, bucle principal
│   ├── board/
│   │   ├── board_config.h      ← Pinout (GP25 LED, JTAG, UART...)
│   │   ├── clock_init.c/.h     ← Configuración PLLs (125 MHz sys, 48 MHz USB)
│   │   └── gpio_init.c/.h      ← Configuración de GPIOs
│   ├── usb/
│   │   ├── usb_common.h        ← Structs USB 2.0 (descriptores, setup packet)
│   │   ├── usb_device.c/.h     ← Controlador USB bare-metal (DPRAM, EP0, ISR)
│   │   └── usb_descriptors.c/.h← Descriptores del dispositivo (VID/PID, endpoints)
│   ├── jlink/
│   │   ├── jlink_protocol.h    ← Constantes EMU_CMD_* del protocolo J-Link
│   │   ├── jlink_handler.c/.h  ← Dispatcher de comandos J-Link
│   │   └── jlink_caps.c/.h     ← Respuestas: VERSION, GET_CAPS, HW_VERSION...
│   ├── jtag/
│   │   ├── jtag_pio.c/.h       ← Motor JTAG con PIO + DMA (stub)
│   │   ├── jtag_tap.c/.h       ← Operaciones TAP de alto nivel (stub)
│   │   └── jtag.pio            ← Programa PIO para señales JTAG
│   ├── cdc/
│   │   └── cdc_uart.c/.h       ← Puente UART ↔ USB CDC (stub)
│   └── util/
│       ├── led.c/.h            ← Control de LEDs de estado
│       └── adc.c/.h            ← Lectura de Vref del target (stub)
└── doc/
    └── ...                     ← Documentación adicional
```

---

## Solución de problemas

### "cmake no se reconoce como comando"

Las herramientas no están en el PATH. Usa la opción de compilación desde VS Code, o configura el PATH manualmente como se indica en la sección de compilación por CMD.

### "Python not found" durante cmake

CMake necesita Python para el boot stage 2. Asegúrate de que Python 3.13 está instalado y en el PATH:

```cmd
python --version
```

Si muestra Python 3.14, desinstálalo o ajusta el PATH para que 3.13 aparezca primero.

### El Pico no aparece como RPI-RP2

- Asegúrate de que el cable USB es de datos (no solo de carga).
- Mantén BOOTSEL pulsado **antes** de conectar el USB, no después.
- Prueba con otro puerto USB del PC.

### "Device not found" en J-Link Commander

- Comprueba en el Administrador de dispositivos de Windows que aparece un dispositivo USB con VID `1366` y PID `0101`.
- Si aparece con un triángulo amarillo, puede que necesites instalar el driver con [Zadig](https://zadig.akeo.ie/).

### El LED no parpadea tras flashear

- Verifica que el archivo `.uf2` se copió completamente a RPI-RP2 (no desconectes durante la copia).
- Reintenta el proceso de flasheo desde el principio.

---

## Licencia

Este proyecto se desarrolla como Trabajo de Fin de Grado (TFG). Consulta el archivo LICENSE para los términos de uso.