DEVICE_SINGLE := 1
BAREMETAL_DIR := baremetal
BOARD ?= elyte
BOARD_DIR := ./boards/$(BOARD)
APP ?= elyte
include $(APP)/$(APP).mk
include $(BAREMETAL_DIR)/main.mk

CUBE_PROGRAMMER_PATH := ~/STMicroelectronics/STM32Cube/STM32CubeProgrammer/bin

flash: ${TARGET_DIR}/$(TARGETNAME).hex
	@echo "Flashing $<"
	$(v)$(CUBE_PROGRAMMER_PATH)/STM32_Programmer_CLI -q -c port=SWD -e all -w $< -rst > /dev/null 2>&1
