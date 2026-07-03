# SWEET — STM32F411CEU6 Stm_g302 variant (HAL + USB Device Library)
# Purpose: Logitech G302 Daedalus Apex clone (VID 0x046D / PID 0xC07F) —
# 2-interface HID with Boot Mouse on IF0 and Multimedia + HID++ on IF1.
#
# Build from the variant directory:
#   cd stm32/Stm_g302 && make
#   make flash    # st-flash --reset write build/sweet_g302.bin 0x08000000

PROJECT   = sweet_g302
MCU       = cortex-m4
DEVICE    = STM32F411xE

# ── Directories ────────────────────────────────────────────────────────────
ST_ROOT   = ..
CUBE      = $(ST_ROOT)/lib/stm32cube
HAL_INC   = $(CUBE)/Drivers/STM32F4xx_HAL_Driver/Inc
HAL_SRC   = $(CUBE)/Drivers/STM32F4xx_HAL_Driver/Src
CMSIS_DEV = $(CUBE)/Drivers/CMSIS/Device/ST/STM32F4xx/Include
CMSIS_INC = $(CUBE)/Drivers/CMSIS/Include
USBD_CORE_INC = $(CUBE)/Middlewares/ST/STM32_USB_Device_Library/Core/Inc
USBD_CORE_SRC = $(CUBE)/Middlewares/ST/STM32_USB_Device_Library/Core/Src
G302_DIR      = USB_DEVICE/Class/G302
PLATFORM      = $(ST_ROOT)/libraries/sweet_platform
BUILD_DIR = build
TMPDIR   ?= /tmp
export TMPDIR

# ── Toolchain ──────────────────────────────────────────────────────────────
TRIPLET   = arm-none-eabi
CC        = $(TRIPLET)-gcc
LD        = $(TRIPLET)-gcc
OBJCOPY   = $(TRIPLET)-objcopy
SIZE      = $(TRIPLET)-size

# ── Flags ──────────────────────────────────────────────────────────────────
MCU_FLAGS = -mcpu=$(MCU) -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16
DEFINES   = -D$(DEVICE) -DUSE_HAL_DRIVER

CFLAGS    = $(MCU_FLAGS) $(DEFINES) \
            -Os -g3 -Wall -Wextra \
            -Wno-unused-parameter -Wno-unused-variable \
            -fdata-sections -ffunction-sections \
            -fno-strict-aliasing -pipe

LDFLAGS   = $(MCU_FLAGS) -T STM32F411CEUx_FLASH.ld \
            -Wl,--gc-sections -Wl,--print-memory-usage \
            -specs=nano.specs -specs=nosys.specs

# ── Sources ────────────────────────────────────────────────────────────────
SRC_C  = startup_stm32f411xe.c
SRC_C += $(CUBE)/system_stm32f4xx.c

SRC_C += Core/Src/main.c
SRC_C += Core/Src/stm32f4xx_it.c
SRC_C += Core/Src/stm32f4xx_hal_msp.c

SRC_C += USB_DEVICE/App/usb_device.c
SRC_C += USB_DEVICE/App/usbd_desc.c
SRC_C += USB_DEVICE/Target/usbd_conf.c

# HAL driver — only the modules enabled in stm32f4xx_hal_conf.h
SRC_C += $(HAL_SRC)/stm32f4xx_hal.c
SRC_C += $(HAL_SRC)/stm32f4xx_hal_cortex.c
SRC_C += $(HAL_SRC)/stm32f4xx_hal_rcc.c
SRC_C += $(HAL_SRC)/stm32f4xx_hal_rcc_ex.c
SRC_C += $(HAL_SRC)/stm32f4xx_hal_gpio.c
SRC_C += $(HAL_SRC)/stm32f4xx_hal_dma.c
SRC_C += $(HAL_SRC)/stm32f4xx_hal_dma_ex.c
SRC_C += $(HAL_SRC)/stm32f4xx_hal_pwr.c
SRC_C += $(HAL_SRC)/stm32f4xx_hal_pwr_ex.c
SRC_C += $(HAL_SRC)/stm32f4xx_hal_flash.c
SRC_C += $(HAL_SRC)/stm32f4xx_hal_flash_ex.c
SRC_C += $(HAL_SRC)/stm32f4xx_hal_flash_ramfunc.c
SRC_C += $(HAL_SRC)/stm32f4xx_hal_exti.c
SRC_C += $(HAL_SRC)/stm32f4xx_hal_pcd.c
SRC_C += $(HAL_SRC)/stm32f4xx_hal_pcd_ex.c
SRC_C += $(HAL_SRC)/stm32f4xx_ll_usb.c
SRC_C += $(HAL_SRC)/stm32f4xx_hal_uart.c

# USB Device Library — core + G302 2-interface HID class
SRC_C += $(USBD_CORE_SRC)/usbd_core.c
SRC_C += $(USBD_CORE_SRC)/usbd_ctlreq.c
SRC_C += $(USBD_CORE_SRC)/usbd_ioreq.c
SRC_C += $(G302_DIR)/usbd_g302.c
SRC_C += $(G302_DIR)/hidpp_timing.c
SRC_C += $(G302_DIR)/hidpp_handler.c
SRC_C += $(G302_DIR)/hidpp_features.c
SRC_C += $(G302_DIR)/hidpp_rgb.c

# Shared platform helpers (DWT cycle counter init for hidpp_timing)
SRC_C += $(PLATFORM)/platform.c

# ── Includes ───────────────────────────────────────────────────────────────
INC  = -ICore/Inc
INC += -IUSB_DEVICE/App
INC += -IUSB_DEVICE/Target
INC += -I$(HAL_INC)
INC += -I$(HAL_INC)/Legacy
INC += -I$(CMSIS_DEV)
INC += -I$(CMSIS_INC)
INC += -I$(USBD_CORE_INC)
INC += -I$(G302_DIR)
INC += -I$(PLATFORM)

# ── Objects (flattened by basename — Makefile pattern rule + VPATH) ────────
OBJS  = $(addprefix $(BUILD_DIR)/, $(notdir $(SRC_C:.c=.o)))
VPATH = $(sort $(dir $(SRC_C)))

# ── Build rules ─────────────────────────────────────────────────────────────
.PHONY: all clean flash flash-ocd

all: $(BUILD_DIR)/$(PROJECT).bin $(BUILD_DIR)/$(PROJECT).hex

$(BUILD_DIR)/%.o: %.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INC) -c $< -o $@

$(BUILD_DIR)/$(PROJECT).elf: $(OBJS)
	TMPDIR=$(TMPDIR) TEMP=$(TMPDIR) TMP=$(TMPDIR) $(LD) $(LDFLAGS) $^ -o $@

$(BUILD_DIR)/%.bin: $(BUILD_DIR)/%.elf
	$(OBJCOPY) -O binary $< $@

$(BUILD_DIR)/%.hex: $(BUILD_DIR)/%.elf
	$(OBJCOPY) -O ihex $< $@

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

flash: $(BUILD_DIR)/$(PROJECT).bin
	st-flash --reset write $< 0x08000000

flash-ocd: $(BUILD_DIR)/$(PROJECT).elf
	openocd -f interface/stlink.cfg -f target/stm32f4x.cfg \
	        -c "program $< verify reset exit"

clean:
	rm -rf $(BUILD_DIR)
