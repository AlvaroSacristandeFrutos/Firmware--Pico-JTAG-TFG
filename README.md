# jlink-pico-probe

Firmware bare-metal para **Raspberry Pi Pico (RP2040)** que implementa el protocolo
**PicoAdapter** — una sonda JTAG de bajo coste para boundary scan y depuración.
La capa USB es completamente bare-metal (sin TinyUSB).

El repositorio incluye también una **DLL de Windows** (`JLink_x64.dll`) que se hace
pasar por la DLL oficial de SEGGER, lo que permite que cualquier software compatible
con J-Link (como nuestro JtagScannerQt) funcione con la sonda **sin ningún cambio** en el
código de la aplicación — simplemente poniendo nuestra DLL en el directorio del ejecutable.

---

## Tabla de contenidos

- [Descripción](#descripción)
- [Requisitos de hardware](#requisitos-de-hardware)
- [Instalación de herramientas](#instalación-de-herramientas)
- [Clonar el repositorio](#clonar-el-repositorio)
- [Compilar el firmware](#compilar-el-firmware)
- [Compilar la DLL](#compilar-la-dll)
- [Flashear el Pico](#flashear-el-pico)
- [Estructura del proyecto](#estructura-del-proyecto)

---

## Descripción

El proyecto está formado por dos componentes:

### Firmware (Raspberry Pi Pico)

El firmware convierte la Pico en una sonda JTAG. Al conectarla al PC por USB,
Windows la reconoce automáticamente como un puerto COM virtual (sin drivers adicionales)
usando el VID/PID de Raspberry Pi (`2E8A:000A`). La comunicación entre el PC y la sonda
usa el **protocolo PicoAdapter** — un formato de trama serie propio:

```
[0xA5] [CMD] [LEN_LO] [LEN_HI] [PAYLOAD...] [CRC8]
```

El firmware implementa 15 comandos activos (PING, RESET TAP, desplazamiento de datos JTAG,
lectura de tensión de referencia, control UART del target, etc.).

### DLL de Windows (`JLink_x64.dll`)

Las aplicaciones de boundary scan existentes (incluida la nuestra, JtagScannerQt)
cargan la DLL oficial de SEGGER (`JLink_x64.dll`) y llaman a sus funciones
(`JLINKARM_OpenEx`, `JLINKARM_JTAG_StoreGetRaw`, etc.).

Nuestra DLL **exporta exactamente los mismos nombres de función** que la DLL de SEGGER,
por lo que actúa como un reemplazo directo (*drop-in replacement*). Internamente,
en lugar de hablar con hardware SEGGER, abre el puerto COM virtual de la Pico y
traduce las llamadas al protocolo PicoAdapter.

Para usar la sonda basta con **copiar nuestra `JLink_x64.dll` al directorio del ejecutable**
de la aplicación — sin modificar el código de la aplicación.

```
JtagScannerQt.exe       ← aplicación sin modificar
JLink_x64.dll           ← nuestra DLL (reemplaza a la de SEGGER)
      │
      │  USB-CDC (puerto COM virtual) — protocolo PicoAdapter
      ▼
Raspberry Pi Pico
      │  JTAG: TDI (GP16), TDO (GP17), TCK (GP18), TMS (GP19)
      │  Reset: nRST (GP20), nTRST (GP21)
      │  UART target: TX (GP12), RX (GP13)
      ▼
Circuito objetivo (FPGA, MCU, PCB...)
```

---

## Requisitos de hardware

- **Raspberry Pi Pico** (RP2040): el modelo original, Pico H o cualquier clon compatible con RP2040.
  La Pico W también funciona pero no aporta nada extra.
- **Cable micro-USB de datos**: muchos cables USB baratos solo tienen los cables de alimentación
  y no los de datos. Si el Pico no aparece en el PC, prueba con otro cable.
- **PC con Windows 10 o Windows 11 de 64 bits** (x64).

---

## Instalación de herramientas

Hay que instalar herramientas distintas según lo que se quiera compilar:

| Qué compilar | Herramientas necesarias |
|-------------|------------------------|
| Solo firmware (`.uf2`) | Git + Python 3.13 + VS Code + extensión Raspberry Pi Pico |
| Solo DLL (`.dll`) | Git + Visual Studio 2022 con C++ |
| Ambos | Todo lo anterior |

---

### 1. Git

Git es el sistema de control de versiones que se usa para descargar el repositorio.

**Instalación:**
1. Ve a [https://git-scm.com/download/win](https://git-scm.com/download/win)
2. Descarga el instalador de 64 bits (`Git-X.XX.X-64-bit.exe`)
3. Ejecuta el instalador. Las opciones por defecto son correctas en todas las pantallas.
   La única que merece atención:
   - En **"Adjusting your PATH environment"**: deja seleccionada la opción
     **"Git from the command line and also from 3rd-party software"** (opción del medio).
     Esto añade `git` al PATH del sistema para poder usarlo desde CMD/PowerShell.
4. Completa la instalación y cierra.

**Verificar:**
Abre un CMD nuevo (importante: nuevo, para que cargue el PATH actualizado) y ejecuta:
```cmd
git --version
```
Debe mostrar algo como `git version 2.42.0.windows.2`.

---

### 2. Python 3.13 (solo para el firmware)

El SDK de Pico necesita Python para generar el código de arranque del procesador
durante la compilación. **No se usa para programar** — es una dependencia interna del build.

> **Importante:** Python 3.14 (actualmente en pre-release) **no es compatible** con
> la versión de CMake incluida en el SDK. Instala la versión 3.13 estable.

**Instalación:**
1. Ve a [https://www.python.org/downloads/](https://www.python.org/downloads/)
2. Haz clic en **"Download Python 3.13.x"** (el botón amarillo grande)
3. Ejecuta el instalador descargado
4. **Paso crítico:** en la primera pantalla del instalador aparece una casilla en la
   parte inferior que dice **"Add python.exe to PATH"**. Esta casilla está **desmarcada
   por defecto**. **Márcala antes de continuar.**
5. Haz clic en **"Install Now"** y espera a que termine.

**Verificar:**
Abre un CMD nuevo y ejecuta:
```cmd
python --version
```
Debe mostrar `Python 3.13.x`. Si no aparece o muestra otra versión, cierra y vuelve
a abrir el CMD para recargar el PATH.

> **Alternativa con winget** (si tienes winget disponible en Windows 10/11):
> ```cmd
> winget install Python.Python.3.13 --accept-source-agreements --accept-package-agreements
> ```

---

### 3. Visual Studio Code (solo para el firmware)

VS Code es el editor que usamos para compilar el firmware gracias a la extensión
de Raspberry Pi. No es imprescindible (se puede compilar desde CMD), pero es la
forma más cómoda.

**Instalación:**
1. Ve a [https://code.visualstudio.com/](https://code.visualstudio.com/)
2. Haz clic en **"Download for Windows"** (descarga el instalador de usuario `.exe`)
3. Ejecuta el instalador. En la pantalla **"Seleccionar tareas adicionales"** marca:
   - **"Agregar al PATH"** (permite escribir `code .` en CMD para abrir VS Code)
   - Las demás opciones son opcionales.
4. Completa la instalación.
5. Abre VS Code.

---

### 4. Extensión Raspberry Pi Pico y descarga del SDK

Esta extensión es la pieza clave para compilar el firmware. Se encarga de descargar
automáticamente el compilador ARM, el SDK de Pico, CMake y Ninja — no hay que
instalar ninguna de estas herramientas por separado.

**Instalación de la extensión:**
1. Con VS Code abierto, ve a la pestaña de **Extensiones** en la barra lateral izquierda
   (icono de cuatro cuadrados, o atajo `Ctrl+Shift+X`)
2. En el buscador escribe: `Raspberry Pi Pico`
3. Aparecerá la extensión con autor **Raspberry Pi**. Haz clic en **Install**.
4. Espera a que se instale (unos segundos).

**Descarga del SDK y herramientas:**

Tras instalar la extensión, aparece un icono de Raspberry Pi en la barra lateral
izquierda de VS Code. Haz clic en él. La extensión iniciará la descarga del SDK y
las herramientas en segundo plano. El progreso aparece en la barra de estado inferior
de VS Code. **La primera descarga puede tardar varios minutos** dependiendo de la
velocidad de conexión a internet.

Las herramientas se instalan en `C:\Users\<tu_usuario>\.pico-sdk\`:

| Carpeta | Contenido | Versión |
|---------|-----------|---------|
| `.pico-sdk\sdk\2.2.0\` | Pico SDK (cabeceras y librerías) | 2.2.0 |
| `.pico-sdk\toolchain\14_2_Rel1\` | ARM GCC (compilador cruzado para RP2040) | 14.2 |
| `.pico-sdk\cmake\v3.31.5\` | CMake (sistema de build) | 3.31.5 |
| `.pico-sdk\ninja\v1.12.1\` | Ninja (ejecutor de builds) | 1.12.1 |

**Verificar que la descarga se completó:**
Comprueba que existe el archivo:
```
C:\Users\<tu_usuario>\.pico-sdk\toolchain\14_2_Rel1\bin\arm-none-eabi-gcc.exe
```
Si no existe, abre el panel de la extensión (icono Raspberry Pi en la barra lateral)
y busca la opción **"Switch SDK version"** o **"Install SDK"** para forzar la descarga.

---

### 5. Visual Studio 2022 (solo para la DLL)

Visual Studio 2022 es necesario para compilar la DLL. La DLL usa la API Win32 de bajo
nivel (`SetupDI`, `CreateFile`, overlapped I/O) que requiere el compilador MSVC de
Microsoft. **MinGW o GCC no son compatibles.**

**Instalación:**
1. Ve a [https://visualstudio.microsoft.com/es/vs/community/](https://visualstudio.microsoft.com/es/vs/community/)
2. Haz clic en **"Descargar Community 2022"** (gratuito)
3. Ejecuta el instalador (`vs_community.exe`). El instalador descargará primero el
   propio Visual Studio Installer (~5 MB) y luego mostrará la pantalla de cargas de trabajo.
4. En la pantalla de **"Cargas de trabajo"**, marca **"Desarrollo de escritorio con C++"**.

   Esto instala automáticamente:
   - Compilador MSVC v143 (`cl.exe`) — el compilador C/C++ de Microsoft
   - Linker (`link.exe`)
   - Windows SDK 10 — cabeceras y librerías de la API de Windows
   - CMake integrado en VS 2022 — no hace falta instalar CMake por separado para la DLL
   - Herramientas de diagnóstico y depuración

5. Haz clic en **"Instalar"** en la esquina inferior derecha.
   La instalación ocupa aproximadamente **6 GB** en disco y puede tardar 15-30 minutos
   según la velocidad de la conexión.

> **CMake para la DLL:** VS 2022 incluye CMake 3.x en su ruta de instalación.
> Cuando compilas la DLL usando el "Developer Command Prompt for VS 2022", CMake
> ya está disponible en el PATH automáticamente, sin instalarlo por separado.

---

## Clonar el repositorio

Abre un CMD o PowerShell y ejecuta:

```cmd
git clone <url-del-repo>
cd jlink-pico-probe
```

Esto descarga el código fuente completo del repositorio en una carpeta llamada
`jlink-pico-probe`.

---

## Compilar el firmware

### Opción A — Desde VS Code (recomendado)

Esta es la forma más sencilla. La extensión Raspberry Pi Pico gestiona CMake y Ninja
de forma transparente.

1. En VS Code: **File → Open Folder** → selecciona la carpeta `jlink-pico-probe`
   (la carpeta raíz del repositorio, donde está el `CMakeLists.txt`)

2. VS Code abre el proyecto. La extensión Raspberry Pi Pico detecta el `CMakeLists.txt`
   y muestra en la **barra de estado inferior** (barra azul en la parte baja de VS Code)
   el texto **"Pico SDK 2.2.0"** o similar. Si no aparece enseguida, espera unos segundos
   o pulsa `Ctrl+Shift+P` → "Reload Window".

3. La primera vez que abres el proyecto, la extensión lo **configura automáticamente**
   (crea la carpeta `build\` con la caché de CMake). Esto puede tardar 1-2 minutos
   porque el SDK compila algunas de sus librerías internas. Verás actividad en el
   terminal de salida de VS Code.

4. Cuando la configuración termine, haz clic en el botón **"Compile"** de la barra
   de estado inferior.
   También puedes usar: `Ctrl+Shift+P` → "Raspberry Pi Pico: Compile Project".

5. La compilación tarda unos segundos. El resultado estará en:
   ```
   build\jlink_pico_probe.uf2
   ```

> **Sobre la caché de CMake:** la carpeta `build\` contiene la caché de CMake
> (`build\CMakeCache.txt`). Esta caché recuerda la ruta del SDK, el compilador y
> los parámetros de configuración. **No hace falta borrarla ni gestionarla.**
> En compilaciones posteriores, la extensión simplemente recompila los archivos
> modificados. Solo si cambias la versión del SDK o la configuración necesitarías
> borrar `build\` y reconfiguar.

### Opción B — Desde CMD

Si no tienes VS Code o prefieres la línea de comandos:

```cmd
REM Añadir las herramientas del SDK al PATH de esta sesión
set TOOLS=%USERPROFILE%\.pico-sdk
set PATH=%TOOLS%\cmake\v3.31.5\bin;%TOOLS%\ninja\v1.12.1;%TOOLS%\toolchain\14_2_Rel1\bin;%PATH%

REM Ir a la raíz del repositorio
cd jlink-pico-probe

REM Configurar el proyecto (solo la primera vez — crea la carpeta build\)
cmake -B build -G Ninja ^
  -DPICO_SDK_PATH=%TOOLS%\sdk\2.2.0 ^
  -DCMAKE_TOOLCHAIN_FILE=%TOOLS%\sdk\2.2.0\cmake\preload\toolchains\pico_arm_cortex_m0plus_gcc.cmake

REM Compilar
cmake --build build --target jlink_pico_probe
```

Resultado: `build\jlink_pico_probe.uf2`

> En compilaciones posteriores solo es necesario ejecutar `cmake --build build`.
> El paso de configuración (`cmake -B build ...`) solo se necesita una vez.

---

## Compilar la DLL

La DLL se compila desde la subcarpeta `dll\` del repositorio, **usando el Developer
Command Prompt de VS 2022** (no un CMD normal), que tiene MSVC en el PATH:

1. Busca **"Developer Command Prompt for VS 2022"** en el menú Inicio de Windows
   y ábrelo. Es un CMD especial que configura automáticamente todas las variables
   de entorno necesarias para MSVC.

2. Navega hasta la carpeta `dll\` del repositorio:
   ```cmd
   cd C:\ruta\hasta\jlink-pico-probe\dll
   ```

3. Configura el proyecto (solo la primera vez):
   ```cmd
   cmake -B build64 -G "Visual Studio 17 2022" -A x64
   ```
   - `-B build64`: crea y usa la carpeta `build64\` como directorio de build
   - `-G "Visual Studio 17 2022"`: usa el generador de VS 2022
   - `-A x64`: compila para 64 bits

4. Compila:
   ```cmd
   cmake --build build64 --config Release
   ```
   - `--config Release`: compilación optimizada (sin símbolos de depuración)

Resultado: `dll\build64\Release\JLink_x64.dll`

### DLL de 32 bits (opcional)

Si se necesita una versión de 32 bits (`JLinkARM.dll`):
```cmd
cmake -B build32 -G "Visual Studio 17 2022" -A Win32 -DBUILD_ARM=1
cmake --build build32 --config Release
```
Resultado: `dll\build32\Release\JLinkARM.dll`

### Desplegar la DLL

Para que una aplicación use nuestra DLL, basta con copiarla al mismo directorio
que el ejecutable de la aplicación:

```cmd
copy dll\build64\Release\JLink_x64.dll <directorio_del_ejecutable>\
```

Windows carga la DLL del directorio del ejecutable antes de buscar en otras rutas,
por lo que nuestra DLL tiene prioridad sobre cualquier otra instalada en el sistema.

---

## Flashear el Pico

Una vez compilado el firmware, hay que cargarlo en la Pico. El RP2040 tiene un
bootloader de fábrica que aparece como unidad USB al mantener pulsado BOOTSEL.

**Pasos:**
1. **Desconecta** el Pico del USB si estaba conectado.
2. **Mantén pulsado el botón BOOTSEL** — es el botón blanco pequeño en la placa Pico.
3. **Sin soltar BOOTSEL**, conecta el cable USB al PC.
4. **Suelta BOOTSEL**. En el explorador de archivos de Windows aparecerá una nueva
   unidad de disco llamada **RPI-RP2** (como si fuera un pendrive).
5. **Copia** el archivo `build\jlink_pico_probe.uf2` a la unidad RPI-RP2.
6. La copia tarda 1-2 segundos. El Pico **se reinicia automáticamente** al terminar
   y desaparece la unidad RPI-RP2. El LED de la placa se encenderá si el firmware arrancó.

Para volver a flashear (p.ej. tras recompilar), repite desde el paso 1.

---

## Estructura del proyecto

```
jlink-pico-probe/
├── CMakeLists.txt              ← Build system del firmware (Ninja + ARM GCC)
├── pico_sdk_import.cmake       ← Script que localiza automáticamente el Pico SDK
├── src/
│   ├── main.c                  ← Punto de entrada: inicialización y bucle principal
│   ├── board/
│   │   ├── board_config.h      ← Pinout completo (JTAG GP16-19, UART GP12-13, LEDs...)
│   │   └── gpio_init.c/.h      ← Inicialización de todos los GPIOs del PCB
│   ├── usb/
│   │   ├── usb_device.c/.h     ← Controlador USB bare-metal (IRQ-driven, DPRAM)
│   │   └── usb_descriptors.c/.h← Descriptores USB (VID=0x2E8A, PID=0x000A, 2 interfaces CDC)
│   ├── jtag/
│   │   ├── jtag_pio.c/.h       ← Motor JTAG: PIO + DMA (transferencia bidireccional)
│   │   ├── jtag_tap.c/.h       ← Máquina de estados TAP (16 estados, IEEE 1149.1)
│   │   └── jtag.pio            ← Programa PIO: 8 instrucciones, LSB-first, 4 ciclos/bit
│   ├── cdc/
│   │   ├── cdc_rx.c/.h         ← Buffer circular RX CDC (ISR → bucle principal)
│   │   └── pico_protocol.c/.h  ← Parser protocolo PicoAdapter (15 comandos activos)
│   ├── uart/
│   │   ├── uart_driver.c/.h    ← UART0 IRQ-driven para debug serie del target (GP12/GP13)
│   │   └── uart_bridge.c/.h    ← Puente transparente EP4 ↔ UART0 (bridge task)
│   └── util/
│       ├── led.c/.h            ← Control de LEDs de estado (verde GP14, rojo GP15)
│       └── adc.c/.h            ← Lectura de tensión de referencia del target (GP26/ADC0)
└── dll/
    ├── CMakeLists.txt          ← Build system de la DLL (MSVC, soporta x64 y x86)
    ├── JLink_x64.def           ← Tabla de exportaciones x64 (44 funciones con nombres SEGGER)
    ├── JLinkARM.def            ← Tabla de exportaciones x86 (variante 32-bit)
    └── src/
        ├── jlink_api.c         ← 41 funciones exportadas: JLINKARM_* + PICO_UART_* + JLINK_PICO_*
        ├── pico_transport.c/.h ← Capa serie: construcción/envío de tramas PicoAdapter
        ├── com_detect.c/.h     ← Detección del puerto COM (SetupDI + overlapped I/O)
        ├── jtag_chain.c/.h     ← JTAG de alto nivel: escaneo de cadena, lectura IDCODE
        ├── jtag_tap_track.c/.h ← Seguimiento de estado TAP en la DLL
        ├── dll_state.h         ← Variables globales compartidas entre módulos
        └── jlink_types.h       ← Structs y tipos compatibles con la API SEGGER
```

---

## Licencia

Este proyecto se desarrolla como Trabajo de Fin de Grado (TFG). Consulta el archivo LICENSE para los términos de uso.
