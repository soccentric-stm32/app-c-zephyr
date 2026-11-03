# soccentric-stm32-zephyr-application

The demo application of the NUCLEO-F767ZI system built on Zephyr RTOS (board `nucleo_f767zi`). It runs under the A/B bootloader from either slot and does the same job as the C FreeRTOS demo, so the C, Rust and Zephyr versions can be compared like for like.

| | |
|---|---|
| Requirements | [SRS.md](SRS.md) — SRS-ZEPHYR-F767 (v0.1 draft) |
| Functional baseline | [SRS-DEMO-F767](https://github.com/soccentric-stm32/soccentric-stm32-application/blob/HEAD/SRS.md) — same behaviour as the C FreeRTOS demo |
| Board | NUCLEO-F767ZI — see [Hardware.md](https://github.com/soccentric-stm32/soccentric-stm32-validation-firmware/blob/HEAD/Hardware.md) |
| Status | Skeleton: runs from the bootloader slot, 216 MHz clock and caches from devicetree/Kconfig, heartbeat on LD1, fatal-error handler. Comms, button, health and the image file still to come. |
| Build environment | [docker/README.md](docker/README.md): `stm32-dev-zephyr` = the shared `stm32-dev` image + Zephyr v4.4.2 (`hal_stm32`, CMSIS) + Zephyr SDK 1.0.1 (`arm-zephyr-eabi`) |

## Build and flash

Everything runs in the container through `scripts/docker-dev.sh`:

```bash
scripts/docker-dev.sh build   # build the image once (and the stm32-dev base if missing)
scripts/build.sh              # build inside the container
make flash                    # flash build/zephyr-app.elf over SWD, verify, reset
make help                     # all targets
```

Outputs in `build/`: `zephyr-app.elf`, `zephyr-app.bin`, `zephyr-app.hex`. The image is linked at `0x0800C000` (`CONFIG_FLASH_LOAD_OFFSET`) and starts only through the bootloader. It is a freestanding Zephyr app: plain CMake against `$ZEPHYR_BASE`, no west workspace needed.

## Related projects

| Repository | Role |
|---|---|
| [soccentric-stm32-validation-firmware](https://github.com/soccentric-stm32/soccentric-stm32-validation-firmware) | HWVAL hardware validation firmware |
| [soccentric-stm32-bootloader](https://github.com/soccentric-stm32/soccentric-stm32-bootloader) | A/B bootloader (UART update, CRC32, trial boot and rollback) |
| [soccentric-stm32-application](https://github.com/soccentric-stm32/soccentric-stm32-application) | FreeRTOS demo application in C |
| [soccentric-stm32-rust-application](https://github.com/soccentric-stm32/soccentric-stm32-rust-application) | The same demo in Rust (Embassy) |
| [soccentric-stm32-zephyr-application](https://github.com/soccentric-stm32/soccentric-stm32-zephyr-application) | The same demo on Zephyr RTOS |
| [soccentric-stm32-validation-gui](https://github.com/soccentric-stm32/soccentric-stm32-validation-gui) | PySide6 GUI: runs HWVAL tests, updates and flashes firmware |
| [soccentric-stm32-factory-provisioning](https://github.com/soccentric-stm32/soccentric-stm32-factory-provisioning) | HWVAL Factory: end-of-line test and provisioning station |

## License

[MIT](LICENSE) © 2026 Sandesh Ghimire. Use it as a starting point for your own
products. Third-party code (STM32CubeF7, FreeRTOS, LwIP, Embassy, Zephyr and
others) is fetched at build time and keeps its own licence.

Author: Sandesh Ghimire <sandesh@soccentric.com>
