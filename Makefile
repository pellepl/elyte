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
	$(v)$(CUBE_PROGRAMMER_PATH)/STM32_Programmer_CLI -q -c port=SWD -e all -w $< -v [fast] -rst

fonts:
	@echo "Generating fonts"
	elyte/fonts/gen.py elyte/fonts/PixeloidSans.ttf -o elyte/fonts/ -s 9 -c 0x20,0x7f
	elyte/fonts/gen.py elyte/fonts/Roboto-SemiBold.ttf -o elyte/fonts/ -s 13 -c 0x20,0x7f
	elyte/fonts/gen.py elyte/fonts/Roboto-SemiBold.ttf -o elyte/fonts/ -s 16 -c 0x20,0x7f
	elyte/fonts/gen.py elyte/fonts/Roboto-ExtraBold.ttf -o elyte/fonts/ -s 20 -c 0x20,0x7f
	elyte/fonts/gen.py elyte/fonts/Roboto-ExtraBold.ttf -o elyte/fonts/ -s 30 -c 0x2d,0x2e 0x30,0x39 0x45,0x45 0x4e,0x4e 0x4f,0x4f 0x53,0x53 0x59,0x59  
