# jlink-pico-probe

Firmware bare-metal para **Raspberry Pi Pico (RP2040)** que emula una sonda **J-Link V9** ante el driver `jlink.sys` de SEGGER. La capa USB es completamente bare-metal (sin TinyUSB), basada en `pico-examples/usb/device/dev_lowlevel`.

---

## Requisitos previos (instalar una sola vez)

### 1. VS Code + extensión Raspberry Pi Pico

Instala [Visual Studio Code](https://code.visualstudio.com/) y luego la extensión **"Raspberry Pi Pico"** desde el marketplace. La extensión descarga automáticamente en `%USERPROFILE%\.pico-sdk\`:

- cmake 3.31.5
- ninja 1.12.1
- ARM GCC 14.2 (cross-compiler para RP2040)
- Pico SDK 2.2.0
- MinGW-w64 (compilador C++ para el host, necesario para pioasm y picotool)

### 2. Python 3.9–3.13

El SDK lo necesita para generar el boot stage 2 del RP2040.

```cmd
winget install Python.Python.3.13 --accept-source-agreements --accept-package-agreements
```

> **Nota:** Python 3.14 (pre-release) no funciona con cmake 3.31. Si lo tienes instalado via Chocolatey, instala igualmente Python 3.13 con el comando anterior.

### 3. Git

---

## Compilar desde CMD

```cmd
git clone <url-del-repo>
cd <nombre-del-repo>

set TOOLS=%USERPROFILE%\.pico-sdk
set PATH=%TOOLS%\cmake\v3.31.5\bin;%TOOLS%\ninja\v1.12.1;%TOOLS%\toolchain\14_2_Rel1\bin;%PATH%

cmake -B build -G Ninja -DPICO_SDK_PATH=%TOOLS%\sdk\2.2.0
cmake --build build --target jlink_pico_probe
```

Resultado: **`build\jlink_pico_probe.uf2`**

---

## Compilar desde VS Code

Abre el proyecto con VS Code. La extensión Raspberry Pi Pico detecta el `CMakeLists.txt` y configura el entorno automáticamente. Usa el botón **"Compile Project"** de la barra inferior.

---

## Flashear el Pico

1. Mantén pulsado **BOOTSEL** mientras conectas el Pico por USB.
2. Aparece una unidad USB llamada `RPI-RP2`.
3. Copia `build\jlink_pico_probe.uf2` a esa unidad. El Pico se reinicia solo.
