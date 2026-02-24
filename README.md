# Firmware--Pico-JTAG-TFG

# Raspberry Pi Pico - Baremetal USB CDC (jlink_pico_probe)

Este proyecto implementa una conexión USB CDC a nivel baremetal para la Raspberry Pi Pico (RP2040), basándose en los conceptos descritos por Sommerville. 

El firmware generado (`jlink_pico_probe`) está configurado para gestionar los descriptores y el dispositivo USB de forma manual, desactivando la inicialización automática del `stdio` por USB del SDK estándar.

---

##  Requisitos Previos

Para compilar este proyecto, necesitas las siguientes herramientas. 

| Componente | Versión | Ubicación esperada (Windows) |
| :--- | :--- | :--- |
| **CMake** | $\ge$ 3.13 | Incluido en el `PATH` del sistema |
| **Pico SDK** | 2.2.0 | `%USERPROFILE%\.pico-sdk\sdk\2.2.0` |
| **ARM GCC** | 14.2 Rel1 | `%USERPROFILE%\.pico-sdk\toolchain\14_2_Rel1\bin` |
| **Ninja** | Cualquiera | Incluido en el `PATH` del sistema |

> ** La forma más fácil de instalar todo:**
> Instala **Visual Studio Code** y añade la extensión oficial **"Raspberry Pi Pico"**. La extensión se encargará de descargar el SDK, el toolchain (`arm-none-eabi-gcc`) y Ninja automáticamente, configurando el entorno por ti.

---

##  Compilación (Línea de Comandos)

Si estás usando la extensión de VS Code, puedes compilar usando la interfaz gráfica. Si prefieres usar la terminal (Bash o PowerShell), asegúrate de ejecutar los comandos **desde la raíz del proyecto** y dentro de la terminal integrada de VS Code (para que detecte las rutas del SDK).

### 1. Generar los archivos de configuración
Este paso crea el directorio `build` (si no existe) y configura el proyecto usando Ninja, manteniendo la raíz de tu código limpia:

```bash
cmake -B build -G Ninja
```
Una vez configurado, ejecuta desde el directorio jlink-pico-probe:

```bash
cmake --build build --target jlink_pico_probe
```

Si todo sale bien, el archivo binario compilado se generará en: build/jlink_pico_probe.uf2.
