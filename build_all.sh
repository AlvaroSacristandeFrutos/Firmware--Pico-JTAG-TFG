#!/usr/bin/env bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo "=== RP2040 (Pico) ==="
cmake -DPICO_BOARD=pico   -B "$SCRIPT_DIR/build"       "$SCRIPT_DIR"
cmake --build "$SCRIPT_DIR/build"

echo "=== RP2350 (Pico 2) ==="
cmake -DPICO_BOARD=pico2  -B "$SCRIPT_DIR/build_pico2" "$SCRIPT_DIR"
cmake --build "$SCRIPT_DIR/build_pico2"

echo ""
echo "UF2 RP2040 : build/jlink_pico_probe.uf2"
echo "UF2 RP2350 : build_pico2/jlink_pico_probe.uf2"
