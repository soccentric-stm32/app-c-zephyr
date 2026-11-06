# Doxygen Documentation Progress

One commit per file: add/extend Doxygen-style documentation, mark it `done`, commit, push, next file.

## In scope (project source, `src/`)

Sources are grouped by domain: `app/`, `board/`, `boot/`, `comms/` and `system/`.

| # | File | Status |
|---|------|--------|
| 1 | src/system/app_fault.h | done |
| 2 | src/boot/boot_contract.h | done |
| 3 | src/board/button.c | done |
| 4 | src/board/button.h | done |
| 5 | src/comms/comms.c | done |
| 6 | src/comms/comms.h | done |
| 7 | src/system/fatal.c | done |
| 8 | src/boot/flash_confirm.c | done |
| 9 | src/boot/flash_confirm.h | done |
| 10 | src/comms/flatcc_port.c | done |
| 11 | src/comms/flatcc_port.h | done |
| 12 | src/boot/handover.c | done |
| 13 | src/boot/handover.h | done |
| 14 | src/system/health.c | done |
| 15 | src/system/health.h | done |
| 16 | src/comms/hwlink.c | done |
| 17 | src/comms/hwlink.h | done |
| 18 | src/boot/identity.c | done |
| 19 | src/boot/identity.h | done |
| 20 | src/system/iwdg.c | done |
| 21 | src/system/iwdg.h | done |
| 22 | src/board/led.c | done |
| 23 | src/board/led.h | done |
| 24 | src/main.c | done |
| 25 | src/system/reset_cause.c | done |
| 26 | src/system/reset_cause.h | done |
| 27 | src/comms/uart_hwlink.c | done |
| 28 | src/comms/uart_hwlink.h | done |
| 29 | src/app/zdemo.h | done |
| 30 | src/app/zdemo_threads.h | done |

## Out of scope (not modified)

- `src/generated/*` — flatcc-generated code (rebuilt by `flatc`; edits would be overwritten)
- `third_party/flatcc/*` — vendored FlatCC library
