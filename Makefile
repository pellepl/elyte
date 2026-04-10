DEVICE_SINGLE := 1
BAREMETAL_DIR := baremetal
BOARD ?= elyte
BOARD_DIR := ./boards/$(BOARD)
APP ?= elyte
include $(APP)/$(APP).mk
include $(BAREMETAL_DIR)/main.mk
