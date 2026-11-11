# app-c-zephyr

The NUCLEO-F767ZI demo application in C on Zephyr RTOS. It runs under the A/B bootloader from either slot and does exactly the same job as the C FreeRTOS demo: LED patterns, the user button, HWLINK communication with the desktop GUI, watchdog supervision and self-confirmation after a health check. Keeping the behaviour identical lets you compare C and Rust on FreeRTOS and Zephyr like for like. It is a freestanding Zephyr application built with plain CMake, so no west workspace is needed.

## Debug terminal

The debug log from a reset on a NUCLEO-F767ZI (USART6, 115200 8N1), recorded from the board. [Watch it replay line by line](https://soccentric-stm32.github.io/#app-zephyr) on the project page.

```console
$ make flash PORT=/dev/ttyACM0
Activate: ok, device is resetting
[      0] BOOT I start v0.1.0 d389f50d03e7, reset 0x14000000
[      5] BOOT I init: clock 216 MHz, PLL locked
[      9] BOOT I init: HWLINK on USART3, 115200 8N1
[     14] BOOT I scan: searching for an application
[    352] BOOT I init: flash dual bank, writes enabled
[    357] BOOT I slot A: ok, confirmed
[    361] BOOT I slot B: ok, pending
[    364] BOOT I sel: slot B on trial, count 0
[    368] BOOT I listen: 500 ms for a host
[    872] BOOT I boot slot B, trial 1
[      0] C-ZEP I start v0.1.0 0b248e954e01, slot B on trial, reset software
[      5] C-ZEP I task 3 Health: watches thread check-ins, kicks the watchdog, confirms the slot
[      5] C-ZEP I task 4 Comms: serves HWLINK on USART3: info, identity, button and LED commands
[      5] C-ZEP I task 2 Button: debounces B1, short press cycles the LED pattern, long press resets it
[      5] C-ZEP I task 1 LED: drives LD1-LD3 patterns, LD3 shows health and fault state
[ ... 19 lines omitted: the same task lines, once a second ... ]
[   5014] C-ZEP I health: passed, CONFIRMED written
[   5014] C-ZEP I task 3 Health: watches thread check-ins, kicks the watchdog, confirms the slot
[   6006] C-ZEP I task 1 LED: drives LD1-LD3 patterns, LD3 shows health and fault state
[   6011] C-ZEP I task 4 Comms: serves HWLINK on USART3: info, identity, button and LED commands
[   6011] C-ZEP I task 2 Button: debounces B1, short press cycles the LED pattern, long press resets it
```

## Features

- Runs from either bootloader slot, linked at `0x0800C000`
- Devicetree overlay that matches the bootloader flash layout
- 216 MHz clock and caches configured through devicetree and Kconfig
- Seven LED patterns on LD1/LD2 with 50 to 5000 ms periods
- LD3 reserved for errors
- B1 button on a GPIO interrupt with 30 ms debounce and short/long press actions
- HWLINK over USART3 with interrupt-driven RX ring and TX
- SetBaud to 921600 with automatic fallback to 115200 after 5 s of silence
- Malformed and unknown requests answered with NAK; repeated sequence numbers replayed
- Health thread is the only watchdog feeder; other threads check in through a `k_event`
- CONFIRMED record written to the metadata partition after the trial health check
- Fail-health Kconfig option to exercise bootloader rollback
- Fault, assertion and stack-overflow capture into no-init RAM
- Status reply with fault, reset cause, slot state, stack high-water marks and heap
- Build enforces a 128 KB flash and 64 KB RAM budget
- Produces a ready-to-flash bootloader image (`zephyr-app.img`)
- Reproducible Docker build environment with Zephyr and the Zephyr SDK

## Project layout

The source tree is grouped by responsibility so each module has one reason to
change and dependencies point in one direction: `main` wires the application,
board code owns the pins, comms owns the wire protocol, boot owns the
bootloader contract, and system owns supervision and diagnostics.

```text
.
├── boards/                    Board-specific devicetree overlay
├── ci/                        Release and packaging helpers
├── docker/                    Pinned Zephyr build environment
├── docs/                      Requirements and documentation status
├── protocol/                  HWLINK FlatBuffers schema (single source of truth)
├── scripts/                   Build, flash, serial and image tools
├── src/
│   ├── main.c                 Entry point and start-up sequence
│   ├── app/                   Shared configuration and thread handles
│   ├── board/                 LED and button drivers
│   ├── boot/                  Boot contract, hand-over, identity, confirmation
│   ├── comms/                 HWLINK framing, UART transport, flatcc port
│   ├── generated/             Committed flatcc output (do not edit)
│   └── system/                Health, watchdog, faults and reset cause
├── third_party/flatcc/        Vendored FlatCC runtime
├── CMakeLists.txt             Freestanding Zephyr build
├── Kconfig                    Application Kconfig options
├── prj.conf                   Zephyr configuration
└── Makefile                   Build, flash and debug shortcuts
```

Headers are included with a module prefix, for example
`#include "comms/hwlink.h"`, so dependencies are visible at the call site.

## Documentation

- [HWLINK protocol schema](protocol/README.md)
- [Docker development environment](docker/README.md)

## Build and run

```bash
scripts/docker-dev.sh build   # build the stm32-dev-zephyr container image once
scripts/build.sh              # build inside the container
make flash                    # upload build/zephyr-app.img through the bootloader
make help                     # list all targets
```

## Related projects

- [bootloader](https://github.com/soccentric-stm32/bootloader) — A/B bootloader with UART update, CRC32 checks, trial boot and rollback
- [app-c-freertos](https://github.com/soccentric-stm32/app-c-freertos) — Demo application in C on FreeRTOS
- [app-rust-freertos](https://github.com/soccentric-stm32/app-rust-freertos) — Demo application in Rust on Embassy
- [app-rust-zephyr](https://github.com/soccentric-stm32/app-rust-zephyr) — Demo application in Rust on Zephyr RTOS
- [validation-firmware](https://github.com/soccentric-stm32/validation-firmware) — HWVAL hardware validation firmware
- [validation-gui](https://github.com/soccentric-stm32/validation-gui) — HWVAL Studio: desktop GUI for tests, updates and flashing
- [factory-station](https://github.com/soccentric-stm32/factory-station) — HWVAL Factory: end-of-line test and provisioning station
- [soccentric-stm32.github.io](https://github.com/soccentric-stm32/soccentric-stm32.github.io) — Project website

## License

Released under the [MIT License](LICENSE) © 2026 Sandesh Ghimire. You are free
to use, modify and ship it in your own products.

Contributions are welcome. Fork the repository, make your change on a branch and
open a pull request. Bug reports, ideas and improvements are all appreciated.
