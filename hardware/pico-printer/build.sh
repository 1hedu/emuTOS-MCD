#!/usr/bin/env bash
# Build the Pico printer-bridge firmware from the vendored SDK.
# Needs cmake + the arm-none-eabi GCC toolchain on PATH. No network required.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export PICO_SDK_PATH="${PICO_SDK_PATH:-$HERE/../../vendor/pico-sdk}"

if ! command -v arm-none-eabi-gcc >/dev/null 2>&1; then
  echo "error: arm-none-eabi-gcc not found (apt-get install gcc-arm-none-eabi)" >&2
  exit 1
fi

rm -rf "$HERE/build"
mkdir -p "$HERE/build"
cmake -S "$HERE" -B "$HERE/build" >/dev/null
make -C "$HERE/build" -j"$(nproc)" >/dev/null
cp "$HERE/build/pico_printer.uf2" "$HERE/pico_printer.uf2"
echo "built $HERE/pico_printer.uf2 ($(stat -c%s "$HERE/pico_printer.uf2") bytes)"
