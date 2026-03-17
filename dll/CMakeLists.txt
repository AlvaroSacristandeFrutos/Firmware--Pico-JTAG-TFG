cmake_minimum_required(VERSION 3.20)
project(JLink_x64 C)

set(CMAKE_C_STANDARD 11)

# --------------------------------------------------------------------------
# JLink_x64.dll  (x64)
# --------------------------------------------------------------------------
add_library(JLink_x64 SHARED
    src/pico_transport.c
    src/com_detect.c
    src/jtag_chain.c
    src/jlink_api.c
    JLink_x64.def
)

target_include_directories(JLink_x64 PRIVATE src)
target_link_libraries(JLink_x64 PRIVATE setupapi)

# --------------------------------------------------------------------------
# JLinkARM.dll  (x86 — compilar con -A Win32)
# Para generar esta variante se puede sobreescribir el nombre del proyecto:
#   cmake -B build32 -S dll -G "Visual Studio 17 2022" -A Win32 -DBUILD_ARM=1
# --------------------------------------------------------------------------
if(DEFINED BUILD_ARM)
    set_target_properties(JLink_x64 PROPERTIES OUTPUT_NAME JLinkARM)
    target_sources(JLink_x64 PRIVATE JLinkARM.def)
    # Quitar el .def x64 cuando compilamos x86
    list(REMOVE_ITEM SOURCES JLink_x64.def)
endif()
