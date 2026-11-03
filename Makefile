#
# Makefile - Top-level build, clean and flash for the Zephyr demo application
#
# Author:  Sandesh Ghimire <sandesh@soccentric.com>
# Target:  NUCLEO-F767ZI (STM32F767ZI)
#
# Everything runs in the stm32-dev-zephyr container (ZCON-001), the same way as
# scripts/build.sh. Flashing and debugging use the same OpenOCD set-up as the
# C projects.
#
#   make                build the firmware (same as make build)
#   make build          cmake + ninja, then .elf/.bin/.hex in build/
#   make clean          remove build/
#   make rebuild        clean, then build
#   make menuconfig     Zephyr Kconfig menu (interactive)
#   make flash          build, then flash build/zephyr-app.elf, verify and reset
#   make erase          erase the whole flash
#   make reset          reset the board
#   make debug          build, flash, then GDB halted at reset
#   make gdbserver      OpenOCD GDB server on :3333
#   make serial         serial console on the ST-LINK VCP (115200 8N1)
#   make probe          detect the ST-LINK and the target chip
#   make size           show the firmware size
#   make image          build the stm32-dev-zephyr Docker image
#   make help           show this help
#

ELF   := build/zephyr-app.elf
DEV   := scripts/docker-dev.sh

.DEFAULT_GOAL := build

.PHONY: all build clean rebuild menuconfig flash erase reset debug gdbserver \
        serial probe size image help

all: build

build:
	scripts/build.sh

clean:
	rm -rf build

rebuild: clean
	@$(MAKE) --no-print-directory build

menuconfig: build
	$(DEV) run cmake --build /work/build --target menuconfig

flash: build
	$(DEV) flash $(ELF)

erase:
	$(DEV) erase

reset:
	$(DEV) reset

debug: build
	$(DEV) debug $(ELF) --load

gdbserver:
	$(DEV) gdbserver

serial:
	$(DEV) serial

probe:
	$(DEV) probe

size:
	@test -f $(ELF) || { echo "error: $(ELF) not found, run make build"; exit 1; }
	$(DEV) run arm-none-eabi-size /work/$(ELF)

image:
	$(DEV) build

help:
	@sed -n '/^#   make/p' $(firstword $(MAKEFILE_LIST)) | sed 's/^# \{0,1\}//'
