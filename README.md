# soccentric-stm32-zephyr-application

The demo application of the NUCLEO-F767ZI system built on Zephyr RTOS (board `nucleo_f767zi`). It runs under the A/B bootloader from either slot and does the same job as the C FreeRTOS demo, so the C, Rust and Zephyr versions can be compared like for like.

| | |
|---|---|
| Requirements | [SRS.md](SRS.md) — SRS-ZEPHYR-F767 (v0.1 draft) |
| Functional baseline | [SRS-DEMO-F767](https://github.com/soccentric-stm32/soccentric-stm32-application/blob/HEAD/SRS.md) — same behaviour as the C FreeRTOS demo |
| Board | NUCLEO-F767ZI — see [Hardware.md](https://github.com/soccentric-stm32/soccentric-stm32-validation-firmware/blob/HEAD/Hardware.md) |
| Status | Implemented: bootloader slot and hand-over record, 216 MHz clock and caches from devicetree/Kconfig, LED patterns on LD1/LD2/LD3, B1 debounce and button events, HWLINK on USART3 (Hello/Ping/Status/SetLed/SetLedAll/ClearFault/SetBaud/EnterBootloader/Reset), health check with IWDG and CONFIRMED record, fatal-error record, and the `zephyr-app.img` bootloader image. |
| Build environment | [docker/README.md](docker/README.md): `stm32-dev-zephyr` = the shared `stm32-dev` image + Zephyr v4.4.2 (`hal_stm32`, CMSIS) + Zephyr SDK 1.0.1 (`arm-zephyr-eabi`) |

## Build and flash

Everything runs in the container through `scripts/docker-dev.sh`:

```bash
scripts/docker-dev.sh build   # build the image once (and the stm32-dev base if missing)
scripts/build.sh              # build inside the container
make flash                    # upload build/zephyr-app.img through BOOT (bootctl)
make erase-all                # erase BOOT + slots (asks for confirmation)
make help                     # all targets
```

Outputs in `build/`: `zephyr-app.elf`, `zephyr-app.bin`, `zephyr-app.hex`,
`zephyr-app.map` and the BOOT image file `zephyr-app.img` (256-byte image
header + binary). The image is linked at `0x0800C000`
(`CONFIG_FLASH_LOAD_OFFSET`) and starts only through the bootloader. `make
flash` uploads the `.img` with `bootctl.py update --activate`; never program
the raw `.elf`/`.bin` into a slot, because BOOT requires the header and
metadata records. Point `BOOTCTL` or `PORT` elsewhere if the defaults do not
fit. It is a
freestanding Zephyr app: plain CMake against `$ZEPHYR_BASE`, no west workspace
needed. The build also enforces the 128 KB flash / 64 KB RAM budget and prints
both figures.

The flash layout in [app.overlay](app.overlay) matches the project bootloader:
32 KB bootloader, a 16 KB metadata sector at `0x08008000`, and the 976 KB
application slot from `0x0800C000`. The metadata sector is exposed to the
application as the `app_meta_partition` devicetree partition, which is the
only flash the application ever writes (the `CONFIRMED` record).

## What it does

- **LEDs** — LD1 green, LD2 blue and LD3 red. LD1 boots with the `HEARTBEAT`
  pattern. The GUI can set `OFF`, `ON`, `BLINK`, `HEARTBEAT`, `FAST`, `CHASE`
  and `MORSE_SOS` with a 50–5000 ms period in 10 ms steps. LD3 is reserved for
  errors and needs `force` to be set by `SetLed`.
- **Button** — B1 uses the GPIO interrupt with 30 ms debounce. A short press
  cycles the LD1+LD2 pattern; a long press (≥ 2 s) restores the defaults. Each
  press sends an unsolicited `ButtonEvent` when the HWLINK link is open.
- **HWLINK** — USART3 at 115200 8N1 by default, with an interrupt-driven RX
  ring and interrupt-driven TX. `SetBaud` switches to 921600 and reverts to
  115200 after 5 s without a valid frame. Requests are verified with the
  flatcc verifier; malformed frames get `Nak MALFORMED`, unknown requests get
  `Nak UNSUPPORTED`, and a repeated sequence number resends the last reply.
- **Health** — the Health thread is the only IWDG feeder. All other threads
  check in through a `k_event`; a 2 s miss stops the watchdog. A pending slot
  runs the health check 5 s after start (task check-ins, no fault, heap and
  UART receiver) and writes the `CONFIRMED` record into the metadata sector.
- **Fail-health build** — set `CONFIG_ZDEMO_FAIL_HEALTH=y` (or pass
  `-DCONFIG_ZDEMO_FAIL_HEALTH=y` to CMake) to force the health check to fail
  and exercise bootloader rollback.
- **Diagnostics** — HardFault/MemManage/BusFault/UsageFault, assertions and
  stack failures save CFSR/HFSR/MMFAR/BFAR/PC/LR into no-init RAM, flash the
  LD3 fault pattern and reset. `Status` reports the saved fault, reset cause,
  hand-over slot/trial state, LED state, task stack high-water marks and heap
  statistics.

## Starting a new application

1. Copy this repository and rename `zephyr-app`/`zephyr_app` and the
   `APP_FIRMWARE_NAME`/image name in `CMakeLists.txt`.
2. Keep `CONFIG_FLASH_LOAD_OFFSET=0xC000`, `CONFIG_FLASH_LOAD_SIZE=0xF4000`,
   `CONFIG_USE_DT_CODE_PARTITION=n` and the `app.overlay` flash layout. The
   application will not start from the BOOT slot without them.
3. Keep the `RTC_BKP1R`/`RTC_BKP2R` hand-over helpers and the `CONFIRMED`
   metadata write in `src/handover.c` and `src/flash_confirm.c`; they are the
   application side of the bootloader contract.
4. Add your threads with `K_THREAD_DEFINE` and explicit stack sizes, and call
   `health_checkin()` from every thread that must keep the watchdog alive.
5. Keep USART3 for HWLINK unless the bootloader and GUI are changed together,
   and do not enable the Zephyr console/log/shell backends on it.
6. Share `protocol/hwlink.fbs` unchanged with BOOT, HWVAL, the GUI and the other
   demo applications; the generated headers in `src/generated` are committed
   for builds without a host `flatcc`.

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
