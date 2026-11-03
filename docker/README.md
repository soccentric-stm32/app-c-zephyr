# Docker Development Environment

| | |
|---|---|
| **Author** | Sandesh Ghimire <sandesh@soccentric.com> |
| **Date** | 2026-11-03 |
| **Board** | NUCLEO-F767ZI |

The image `stm32-dev-zephyr:latest` extends the shared `stm32-dev:latest` image
of the C projects, so OpenOCD, GDB, picocom, `arm-none-eabi-objcopy` and the
`scripts/docker-dev.sh` commands (flash, debug, serial, probe) work exactly as
there. It adds Zephyr v4.4.2 (only the `hal_stm32` and CMSIS modules) in `/opt/zephyrproject`, its Python requirements in `/opt/venv`, and the Zephyr SDK 1.0.1 Arm toolchain.

| File | Role |
|---|---|
| [Dockerfile](Dockerfile) | The zephyr layer on top of `stm32-dev` (build args `ZEPHYR_VERSION`, `ZEPHYR_SDK_VERSION`) |
| [Dockerfile.base](Dockerfile.base) | Identical copy of the validation firmware's Dockerfile, used to build `stm32-dev` when it is not on the machine yet |

`scripts/docker-dev.sh build` builds the base image first if it is missing,
then this one. The full description of the base image is in the validation
firmware's
[docker/README.md](https://github.com/soccentric-stm32/soccentric-stm32-validation-firmware/blob/HEAD/docker/README.md).

| Variable | Default |
|---|---|
| `STM32_IMAGE` | `stm32-dev-zephyr:latest` |
| `BASE_IMAGE` | `stm32-dev:latest` |
