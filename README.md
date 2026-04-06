# jlink-pico-probe

Firmware bare-metal para **Raspberry Pi Pico (RP2040)** que implementa el protocolo
**PicoAdapter**, una sonda JTAG de bajo coste para boundary scan y depuración.
La capa USB es completamente bare-metal (sin TinyUSB).

El repositorio incluye también una **DLL de Windows** (`JLink_x64.dll`) que se hace
pasar por la DLL oficial de SEGGER, lo que permite que cualquier software compatible
con J-Link (como nuestro JtagScannerQt) funcione con la sonda **sin ningún cambio** en el
código de la aplicación, simplemente poniendo nuestra DLL en el directorio del ejecutable.

---

## Tabla de contenidos

- [Descripción](#descripción)
- [Requisitos de hardware](#requisitos-de-hardware)
- [Instalación de herramientas](#instalación-de-herramientas)
- [Clonar el repositorio](#clonar-el-repositorio)
- [Descargar el SDK de Pico](#descargar-el-sdk-de-pico)
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
usa el **protocolo PicoAdapter**, un formato de trama serie propio:

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
de la aplicación, sin modificar el código de la aplicación.

```
JtagScannerQt.exe       ← aplicación sin modificar
JLink_x64.dll           ← nuestra DLL (reemplaza a la de SEGGER)
      │
      │  USB-CDC (puerto COM virtual), protocolo PicoAdapter
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
| Solo firmware (`.uf2`) | Git + Python 3.13 + ARM GCC + CMake + Ninja + Visual Studio (C++) |
| Solo DLL (`.dll`) | Git + Visual Studio (C++) |
| Ambos | Todo lo anterior |

> **Nota sobre Visual Studio:** tanto **Visual Studio 2022** como **Visual Studio 2026**
> funcionan correctamente. La única diferencia es el nombre del generador CMake al
> configurar la DLL por primera vez (ver sección [Compilar la DLL](#compilar-la-dll)).

---

### 1. Git

Git es necesario para descargar el repositorio y el SDK de Pico.

**Instalación:**
1. Ve a [https://git-scm.com/download/win](https://git-scm.com/download/win)
2. Descarga el instalador de 64 bits (`Git-X.XX.X-64-bit.exe`)
3. Ejecuta el instalador. En **"Adjusting your PATH environment"** deja seleccionada
   la opción **"Git from the command line and also from 3rd-party software"** (opción del medio).
   Esto añade `git` al PATH del sistema para usarlo desde cualquier CMD.
4. Completa la instalación y cierra.

**Verificar** (en un CMD nuevo):
```cmd
git --version
```

---

### 2. Python 3.13

El SDK de Pico necesita Python para generar el código de arranque del procesador
durante la compilación. No se usa para programar, es una dependencia interna del build.

> **Importante:** instala la versión **3.13 estable**. Python 3.14 (pre-release) no es
> compatible con la versión de CMake del SDK.

**Instalación:**
1. Ve a [https://www.python.org/downloads/](https://www.python.org/downloads/)
2. Descarga Python 3.13.x y ejecuta el instalador
3. **Paso crítico:** en la primera pantalla marca **"Add python.exe to PATH"**
   (está desmarcado por defecto)
4. Haz clic en **"Install Now"**

**Alternativa con winget:**
```cmd
winget install Python.Python.3.13 --accept-source-agreements --accept-package-agreements
```

**Verificar** (en un CMD nuevo):
```cmd
python --version
```

---

### 3. ARM GCC, CMake y Ninja

Estas tres herramientas son el núcleo del sistema de compilación del firmware:

- **ARM GCC**: compilador cruzado que genera código para el RP2040 (arquitectura ARM Cortex-M0+)
  desde Windows. Sin él no se puede compilar el firmware.
- **CMake**: sistema de build que lee el `CMakeLists.txt` y genera los ficheros de compilación.
- **Ninja**: ejecutor de builds rápido, invocado por CMake para compilar en paralelo.

**Instalación desde CMD con winget:**
```cmd
winget install Arm.GnuArmEmbeddedToolchain --accept-source-agreements --accept-package-agreements
winget install Kitware.CMake --accept-source-agreements --accept-package-agreements
winget install Ninja-build.Ninja --accept-source-agreements --accept-package-agreements
```

**Verificar** (en un CMD nuevo, importante abrir uno nuevo para que el PATH se actualice):
```cmd
arm-none-eabi-gcc --version
cmake --version
ninja --version
```

---

### 4. Visual Studio 2022 o 2026 (Community)

Visual Studio es necesario por dos motivos:

1. **Para compilar la DLL**: la DLL usa la API Win32 de bajo nivel (`SetupDI`, `CreateFile`,
   overlapped I/O) que requiere el compilador MSVC de Microsoft. MinGW o GCC no son compatibles.

2. **Para compilar el firmware la primera vez**: durante la primera compilación, el SDK
   necesita construir dos herramientas auxiliares para Windows (`pioasm` y `picotool`)
   que también requieren un compilador C++ nativo. En compilaciones posteriores ya no hace
   falta porque esas herramientas quedan compiladas en la carpeta `build\`.

**Instalación:**
1. Ve a [https://visualstudio.microsoft.com/es/vs/community/](https://visualstudio.microsoft.com/es/vs/community/)
2. Descarga e instala Visual Studio Community (versión 2022 o 2026, ambas válidas)
3. En la pantalla de cargas de trabajo, marca **"Desarrollo de escritorio con C++"**

   Esto instala automáticamente:
   - Compilador MSVC (`cl.exe`), el compilador C/C++ de Microsoft
   - Linker (`link.exe`)
   - Windows SDK, cabeceras y librerías de la API de Windows

4. Haz clic en **"Instalar"** (~6 GB, 15-30 minutos)

---

## Clonar el repositorio

Abre un CMD en la carpeta donde quieras guardar el proyecto y ejecuta:

```cmd
git clone https://github.com/AlvaroSacristandeFrutos/Firmware--Pico-JTAG-TFG.git jlink-pico-probe
cd jlink-pico-probe
git checkout development
```

Todos los comandos de compilación del resto de este documento se ejecutan desde
la raíz del repositorio (`jlink-pico-probe\`) salvo que se indique lo contrario.

> **Nota:** el repositorio incluye las carpetas `build\` y `dll\build64\` con la caché
> de CMake generada en la máquina de desarrollo original. Si al configurar CMake aparece
> el error `CMakeCache.txt directory mismatch`, borra la carpeta de build correspondiente
> antes de reconfigurar:
> ```cmd
> rmdir /s /q build        ← para el firmware
> rmdir /s /q dll\build64  ← para la DLL
> ```

---

## Descargar el SDK de Pico

El SDK de Pico contiene las cabeceras y librerías necesarias para compilar el firmware.
Se descarga manualmente con git en dos pasos: primero el SDK principal, luego sus submódulos.

**Paso 1, Clonar el SDK:**
```cmd
git clone https://github.com/raspberrypi/pico-sdk.git %USERPROFILE%\.pico-sdk\sdk\2.2.0 --branch 2.2.0 --depth 1
```

**Paso 2, Inicializar los submódulos:**
```cmd
cd %USERPROFILE%\.pico-sdk\sdk\2.2.0
git -c submodule."lib/mbedtls".update=none submodule update --init --depth 1
```

> El SDK usa submódulos para librerías externas (TinyUSB, lwIP, etc.). Se excluye
> `lib/mbedtls` (criptografía) porque este proyecto no la usa y su descarga es
> propensa a fallos de red. Si el comando falla por problemas de conexión, simplemente
> vuelve a ejecutarlo, git retoma desde donde se quedó sin descargar de cero.

Vuelve a la raíz del repositorio cuando termine:
```cmd
cd <ruta donde clonaste jlink-pico-probe>
```

---

## Compilar el firmware

### Primera vez

La primera compilación configura CMake (genera la caché en `build\`) y compila también
las herramientas auxiliares `pioasm` y `picotool`. Por eso requiere MSVC disponible en
el PATH, lo que se consigue abriendo el **Símbolo del sistema para desarrolladores**
(búscalo en el menú Inicio como "Símbolo del sistema para desarrolladores de Visual Studio").

Desde el **Símbolo del sistema para desarrolladores**, en la raíz del repositorio:

```cmd
cmake -B build -G Ninja -DPICO_SDK_PATH=%USERPROFILE%\.pico-sdk\sdk\2.2.0
cmake --build build
```

Resultado: `build\jlink_pico_probe.uf2`

> La primera compilación puede tardar varios minutos porque CMake descarga y compila
> `picotool` automáticamente desde internet.

### Compilaciones posteriores

Una vez configurado, solo se recompilan los archivos modificados. Ya **no hace falta**
el Símbolo del sistema para desarrolladores, funciona desde un CMD normal:

```cmd
cmake --build build
```

> Solo necesitas repetir el paso de configuración (`cmake -B build ...`) si borras
> la carpeta `build\` o cambias la versión del SDK.

---

## Compilar la DLL

La DLL siempre requiere MSVC, por lo que **siempre** se compila desde el
**Símbolo del sistema para desarrolladores**.

### Primera vez

El generador CMake depende de la versión de Visual Studio instalada:

**Con Visual Studio 2022:**
```cmd
cd dll
cmake -B build64 -G "Visual Studio 17 2022" -A x64
cmake --build build64 --config Release
```

**Con Visual Studio 2026:**
```cmd
cd dll
cmake -B build64 -G "Visual Studio 18 2026" -A x64
cmake --build build64 --config Release
```

Resultado: `dll\build64\Release\JLink_x64.dll`

### Compilaciones posteriores

Desde el **Símbolo del sistema para desarrolladores**, en la carpeta `dll\`:
```cmd
cmake --build build64 --config Release
```

### Desplegar la DLL

Para que una aplicación use nuestra DLL, cópiala al mismo directorio que el ejecutable:

```cmd
copy dll\build64\Release\JLink_x64.dll <directorio_del_ejecutable>\
```

Windows carga la DLL del directorio del ejecutable antes de buscar en otras rutas,
por lo que nuestra DLL tiene prioridad sobre cualquier otra instalada en el sistema.

### Limpiar la caché de la DLL en JtagScannerQt

JtagScannerQt guarda en un archivo temporal la ruta de la DLL que encontró la última vez,
para no tener que buscarla en cada arranque. Si recompilas o mueves la DLL y la aplicación
sigue cargando la versión antigua, borra ese archivo de caché:

```cmd
del %TEMP%\jlink_dll_cache.txt
```

La próxima vez que arranque JtagScannerQt buscará la DLL desde cero y usará la nueva.

---

## Flashear el Pico

Una vez compilado el firmware, hay que cargarlo en la Pico. El RP2040 tiene un
bootloader de fábrica que aparece como unidad USB al mantener pulsado BOOTSEL.

**Pasos:**
1. **Desconecta** el Pico del USB si estaba conectado.
2. **Mantén pulsado el botón BOOTSEL**, es el botón blanco pequeño en la placa Pico.
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
