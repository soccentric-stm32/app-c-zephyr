# Software Requirements Specification — NUCLEO-F767ZI Zephyr Demo Application

| | |
|---|---|
| **Document** | SRS-ZEPHYR-F767 |
| **Version** | 0.1 (draft) |
| **Author** | Sandesh Ghimire <sandesh@soccentric.com> |
| **Date** | 2026-11-03 |
| **Status** | Draft for review |
| **Hardware** | NUCLEO-F767ZI, STM32F767ZIT6 rev Z (see [Hardware.md](https://github.com/soccentric-stm32/validation-firmware/blob/HEAD/Hardware.md)) |
| **Software base** | Zephyr RTOS v4.4.2, Zephyr SDK 1.0.1 (`arm-zephyr-eabi`), board `nucleo_f767zi`, module `hal_stm32` |

---

## Contents

1. [Introduction](#1-introduction)
2. [Overall description](#2-overall-description)
3. [General requirements](#3-general-requirements)
4. [Bootloader contract](#4-bootloader-contract)
5. [Kernel and thread requirements](#5-kernel-and-thread-requirements)
6. [Functional requirements](#6-functional-requirements)
7. [HWLINK on Zephyr](#7-hwlink-on-zephyr)
8. [Safety and robustness](#8-safety-and-robustness)
9. [Non-functional requirements](#9-non-functional-requirements)
10. [Verification matrix](#10-verification-matrix)
- [Appendix A — Mapping to the C demo](#appendix-a--mapping-to-the-c-demo)
- [Appendix B — Open items](#appendix-b--open-items)

---

## 1. Introduction

### 1.1 Purpose

This document specifies **ZDEMO**, the demo application of SRS-DEMO-F767
built on the Zephyr RTOS. It does the same job as the C demo: it blinks the
LEDs, reads the button, speaks HWLINK to the GUI, runs under the A/B
bootloader and confirms itself after a health check. Zephyr provides the
drivers through devicetree and Kconfig instead of hand-written HAL code, which
is the point of the comparison with the C and Rust demos.

### 1.2 Scope

ZDEMO shall implement the **behaviour** of SRS-DEMO-F767 §6–§9 (LEDs,
button, communication, health and confirmation) and its bootloader contract
(§4), with the implementation constraints of this document. Where this SRS
says "as [DEMO] X", requirement X of SRS-DEMO-F767 applies unchanged; only
its implementation differs.

ZDEMO does not use MCUboot: the project's own A/B bootloader (SRS-BOOT-F767)
starts it.

### 1.3 Definitions

| Term | Meaning |
|---|---|
| **ZDEMO** | The Zephyr application specified here |
| **[DEMO]** | The C FreeRTOS demo application, SRS-DEMO-F767 (functional baseline) |
| **Devicetree (DT)** | Zephyr's hardware description; overlays adapt the board |
| **Kconfig** | Zephyr's build-time configuration (`prj.conf`) |
| **Freestanding app** | An application built against `$ZEPHYR_BASE` outside a west workspace |
| **HWLINK** | UART framing and FlatBuffers protocol ([LINK]) |
| **BOOT** | The A/B bootloader, SRS-BOOT-F767 |

### 1.4 Requirement conventions

**shall / should / may**, IDs `<AREA>-<NNN>`, methods **T** (test),
**D** (demonstration), **I** (inspection), **A** (analysis), as in [BOOT] §1.4.

### 1.5 References

| ID | Document |
|---|---|
| [DEMO] | [app-c-freertos/SRS.md](https://github.com/soccentric-stm32/app-c-freertos/blob/HEAD/SRS.md) — SRS-DEMO-F767 |
| [BOOT] | [bootloader/SRS.md](https://github.com/soccentric-stm32/bootloader/blob/HEAD/SRS.md) — SRS-BOOT-F767 |
| [GUI] | [validation-gui/SRS.md](https://github.com/soccentric-stm32/validation-gui/blob/HEAD/SRS.md) — SRS-GUI-F767 |
| [LINK] | [GUI] Appendix A — HWLINK protocol (normative) |
| [RDEMO] | [app-rust-freertos/SRS.md](https://github.com/soccentric-stm32/app-rust-freertos/blob/HEAD/SRS.md) — SRS-RUST-F767 |
| [ZEPHYR] | Zephyr Project documentation, v4.4, board `nucleo_f767zi` |
| [RM0410] | ST, STM32F76xxx/F77xxx reference manual |

---

## 2. Overall description

### 2.1 Product perspective

ZDEMO replaces the C demo in a bootloader slot. It reports role `DEMO`; the
GUI tells the three demos apart by the `HelloReply` name and version.

```
 BOOT (C, bank 1 sectors 0-1) ── hand-over at 0x0800C000 ──> ZDEMO (Zephyr)
                                                              ├─ led thread
                                                              ├─ button (GPIO callback + work item)
                                                              ├─ comms thread <── HWLINK ──> GUI
                                                              └─ health thread ──> IWDG (watchdog API), CONFIRMED
```

### 2.2 Constraints

| ID | Constraint |
|---|---|
| ZCON-001 | ZDEMO shall be built in the project Docker image (`stm32-dev-zephyr`, which extends the shared `stm32-dev` image) with one command (`scripts/build.sh`). |
| ZCON-002 | The Zephyr version, module set and SDK version shall be pinned in the image (build arguments) and reported in the build output. Zephyr and its modules shall be used unmodified; board changes shall be made with devicetree overlays and Kconfig in this repository. |
| ZCON-003 | ZDEMO shall be a freestanding application (`find_package(Zephyr)` against `$ZEPHYR_BASE`) built with CMake + Ninja; west is not needed to build. |
| ZCON-004 | ZDEMO shall run only under BOOT, linked at `0x0800 C000` ([BOOT] LAY-002). |
| ZCON-005 | ZDEMO shall use USART3 (PD8/PD9, the board's `zephyr,console` UART) for HWLINK and shall disable the Zephyr console, shell and logging backends on that UART. |
| ZCON-006 | ZDEMO shall never reconfigure PA13/PA14 (SWD) or PH0/PH1 (HSE). |

### 2.3 Assumptions

As [DEMO] §2.4 (board provisioned for BOOT, LED and button pins, working VCP).
The Zephyr board `nucleo_f767zi` defines `led0` (LD1 green), `led1` (LD2 blue),
`led2` (LD3 red) and `sw0` (B1).

---

## 3. General requirements

| ID | Requirement | Method |
|---|---|---|
| ZGEN-001 | The build shall produce `build/zephyr-app.elf`, `.bin`, `.hex`, `zephyr.map` and the bootloader image file `zephyr-app.img` (via [BOOT] BHOST-001). | I |
| ZGEN-002 | ZDEMO shall embed its version (`VERSION` file), git commit and build date, and the same values shall go into the image header. | I, T |
| ZGEN-003 | ZDEMO shall run the core at 216 MHz from the 8 MHz HSE bypass clock through the PLL, as the board's devicetree does by default; any change shall be made in an overlay. I-cache and D-cache shall be on (`CONFIG_ICACHE`, `CONFIG_DCACHE`). | T |
| ZGEN-004 | Fatal errors (`k_sys_fatal_error_handler`, assertions with `CONFIG_ASSERT`) shall record the reason in a no-init RAM area, show the fault pattern on LD3 and reset (as [DEMO] DGEN-004). | T |
| ZGEN-005 | The reset cause shall be read with the `hwinfo` API, kept for `Status`, then cleared (as [DEMO] DGEN-005). | T |
| ZGEN-006 | The build shall have no warnings (`CONFIG_COMPILER_WARNINGS_AS_ERRORS=y`). | I |
| ZGEN-007 | All source files shall carry the author header of the project. | I |

---

## 4. Bootloader contract

ZDEMO shall meet [BOOT] §11 APP-001…APP-007, as [DEMO] DBT-001…DBT-006:

| ID | Requirement | Method |
|---|---|---|
| ZBT-001 | Kconfig shall set `CONFIG_FLASH_LOAD_OFFSET=0xC000` and `CONFIG_FLASH_LOAD_SIZE=0xF4000` (976 KB) with `CONFIG_USE_DT_CODE_PARTITION=n`, so that the vector table is at `0x0800 C000`; the build shall check the address. | I, T |
| ZBT-002 | The vector table shall be relocated to the image's own table at start-up (Zephyr sets `VTOR` for Cortex-M with `CONFIG_CPU_CORTEX_M_HAS_VTOR`); this shall be verified on the board. | T |
| ZBT-003 | Zephyr's clock initialisation shall start from the reset state BOOT leaves ([BOOT] HND-001). | T |
| ZBT-004 | ZDEMO shall read the hand-over record (`RTC_BKP2R`, through the RTC backup register access or a small SoC helper) and report slot and trial state, as [DEMO] DBT-004. | T |
| ZBT-005 | `EnterBootloader` shall behave as [DEMO] DBT-005 and reset with `sys_reboot(SYS_REBOOT_COLD)`. | T |
| ZBT-006 | The only flash write shall be the `CONFIRMED` record ([BOOT] Appendix B) in the running slot's metadata sector, using the Zephyr flash API on a devicetree partition that covers only that sector. | I |

---

## 5. Kernel and thread requirements

| ID | Requirement | Method |
|---|---|---|
| ZRT-001 | ZDEMO shall use statically defined threads (`K_THREAD_DEFINE`) with the jobs and relative priorities of [DEMO] §5.2: `health` highest, `comms` high, `button` and `led` normal. The button may be a GPIO callback that submits a work item instead of a thread. | I |
| ZRT-002 | Threads shall communicate through `k_msgq`, `k_sem` or `k_event` only. | I |
| ZRT-003 | The kernel tick shall be 1 kHz (`CONFIG_SYS_CLOCK_TICKS_PER_SEC=1000`). | I |
| ZRT-004 | Every thread shall check in with `health` at least once per second; `health` shall stop feeding the watchdog (`watchdog` API on the IWDG) when a thread misses its check-in for 2 s (as [DEMO] DRT-012). The watchdog started by BOOT shall be fed from the first millisecond (OPEN-Z02). | T |
| ZRT-005 | Thread stack sizes shall be set explicitly; `CONFIG_THREAD_ANALYZER` (or `k_thread_stack_space_get`) shall supply the high-water marks for `Status` (as [DEMO] DRT-013). | T |
| ZRT-006 | Stack protection (`CONFIG_HW_STACK_PROTECTION`, MPU) shall be enabled. | I |

---

## 6. Functional requirements

| ID | Requirement | Method |
|---|---|---|
| ZFN-001 | **LEDs:** as [DEMO] LED-001…LED-007, using `gpio_dt_spec` from the `led0`…`led2` aliases. | T, D |
| ZFN-002 | **Button:** as [DEMO] BTN-001…BTN-004, using `sw0` with a GPIO interrupt callback and 30 ms debounce. | T, D |
| ZFN-003 | **Communication:** as [DEMO] COM-001…COM-007 (messages of [DEMO] §8.1), using the interrupt-driven or async UART API on USART3. | T |
| ZFN-004 | **Health and confirmation:** as [DEMO] HLT-001…HLT-005, including a `CONFIG_ZDEMO_FAIL_HEALTH` Kconfig option (equivalent of `DEMO_FAIL_HEALTH`). | T |
| ZFN-005 | `HelloReply` shall report role `DEMO`, firmware name `zephyr-app` and the Zephyr version (`KERNEL_VERSION_STRING`) in its version text. | T |

---

## 7. HWLINK on Zephyr

| ID | Requirement | Method |
|---|---|---|
| ZLNK-001 | The FlatBuffers code shall be generated from the shared schema `protocol/hwlink.fbs` ([LINK] A.5) with flatcc at build time (CMake custom command), and the flatcc runtime shall be built as a library in this repository or as a Zephyr module. | I |
| ZLNK-002 | COBS framing and CRC32 shall follow [LINK] A.2; CRC32 shall use Zephyr's `crc32_ieee` (same algorithm as `zlib.crc32`) or the hardware CRC unit. | T |
| ZLNK-003 | Framing and message handling should be shared with the C demo as one portable C library (same sources), so that both C-based demos behave identically. | I |
| ZLNK-004 | Protocol unit tests shall run with Zephyr's `ztest` on `native_sim` (host) as well as on the board. | T |

---

## 8. Safety and robustness

| ID | Requirement | Method |
|---|---|---|
| ZSAFE-001 | ZDEMO shall never unlock or write option bytes; the flash partition used for `CONFIRMED` shall cover only the running slot's metadata sector. | I |
| ZSAFE-002 | The confirm function shall accept only the `CONFIRMED` record address (as [DEMO] DSAFE-002). | I, T |
| ZSAFE-003 | ZDEMO shall drive only PB0, PB7, PB14, read PC13 and use PD8/PD9 (as [DEMO] DSAFE-003); unused devicetree peripherals of the board (Ethernet, USB, other UARTs) shall be disabled in the overlay. | I |
| ZSAFE-004 | `CONFIG_ASSERT` shall be on in debug builds; release builds shall keep stack protection and the fatal error handler. | I |

---

## 9. Non-functional requirements

| ID | Requirement | Method |
|---|---|---|
| ZNFR-001 | **Footprint:** ZDEMO shall be under 128 KB of flash and 64 KB of RAM; the build shall print both (Zephyr memory report). | I |
| ZNFR-002 | **Start-up:** the LED pattern shall be visible within 300 ms of hand-over. | T |
| ZNFR-003 | **Reliability:** as [DEMO] DNFR-003 (24 h with the GUI polling every 100 ms). | T |
| ZNFR-004 | **Comparison:** contributes the Zephyr column to the C / Rust / Zephyr comparison report ([RDEMO] RNFR-004). | A |
| ZNFR-005 | **Template:** a README shall explain how to start a new Zephyr application from ZDEMO. | I |

---

## 10. Verification matrix

| Area | Requirements | Main method |
|---|---|---|
| General | ZGEN-001…007 | T, I |
| Bootloader contract | ZBT-001…006 | T, I |
| Kernel and threads | ZRT-001…006 | T, I |
| Functional | ZFN-001…005 | T, D |
| HWLINK | ZLNK-001…004 | T, I |
| Safety | ZSAFE-001…004 | I, T |
| Non-functional | ZNFR-001…005 | T, I, A |

---

## Appendix A — Mapping to the C demo

| C demo (FreeRTOS + HAL) | ZDEMO (Zephyr) |
|---|---|
| `xTaskCreate` with priorities | `K_THREAD_DEFINE` with priorities |
| Queues, task notifications | `k_msgq`, `k_sem`, `k_event` |
| `vTaskDelayUntil` | `k_sleep` with an absolute timeout (`K_TIMEOUT_ABS_MS`) |
| HAL GPIO / EXTI / UART | `gpio_dt_spec`, `gpio_add_callback`, UART async/IRQ API |
| Hand-written clock tree | devicetree clock nodes (`&pll`, `&rcc`) |
| Linker script at `0x0800C000` | `CONFIG_FLASH_LOAD_OFFSET=0xC000` |
| IWDG register access | `watchdog` driver (`st,stm32-watchdog`) |
| Fault hooks | `k_sys_fatal_error_handler` |

## Appendix B — Open items

| ID | Item | Status |
|---|---|---|
| OPEN-Z01 | Confirm `CONFIG_FLASH_LOAD_OFFSET` places the image correctly with the board's flash node in dual-bank mode, and add a devicetree partition for the metadata sector (ZBT-006). | Closed: `app.overlay` defines `app_meta_partition` and the slot; bench check of the offset remains part of CL-010. |
| OPEN-Z02 | Zephyr's IWDG driver installs the timeout itself; check that it works when BOOT has already started the IWDG (`CONFIG_WDT_DISABLE_AT_BOOT` cannot stop an IWDG). | Open |
| OPEN-Z03 | Decide between flatcc as an out-of-tree Zephyr module or a plain library in this repository. | Closed: flatcc is a plain vendored library under `third_party/`. |
| OPEN-Z04 | v4.4.2 is pinned (first release on Zephyr SDK 1.0; v4.5.0 was released 2026-10-28). Decide the update policy: follow each release, or stay on one line until the next LTS. | Open |
