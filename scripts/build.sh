#!/usr/bin/env bash
#
# build.sh - Build the Zephyr demo application inside the stm32-dev-zephyr
#            container (ZCON-001, ZCON-003)
#
# Author:  Sandesh Ghimire <sandesh@soccentric.com>
# Usage:   scripts/build.sh            configure (if needed) + build
#          scripts/build.sh clean      rm -rf build, then configure + build
#
# Outputs: build/zephyr-app.elf, build/zephyr-app.bin, build/zephyr-app.hex,
#          build/zephyr-app.map and the bootloader image build/zephyr-app.img
#

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
DEV="$ROOT/scripts/docker-dev.sh"
cd "$ROOT"

if [[ "${1:-}" == "clean" ]]; then
    rm -rf build
fi

"$DEV" run cmake -S /work -B /work/build -G Ninja \
    -DUSER_CACHE_DIR=/work/build/.zephyr-cache  # $ZEPHYR_BASE is read-only
"$DEV" run cmake --build /work/build
"$DEV" run bash -c '
    set -e
    test -f build/zephyr-app.elf
    test -f build/zephyr-app.bin
    test -f build/zephyr-app.hex
    test -f build/zephyr-app.map
    test -f build/zephyr-app.img
    ls -l build/zephyr-app.elf build/zephyr-app.bin build/zephyr-app.hex \
          build/zephyr-app.map build/zephyr-app.img'
