# Makefile - build the HC32F460xE firmware.
# Self-contained SDK: a from-scratch MCU support layer (mcu/) provides the
# register map, Cortex-M4 core, and the handful of peripheral drivers the
# firmware uses (PORT/SPI/USART/EFM/RTC/PWC/SWDT/INTC + system init). No vendor
# DDL. App is built in secondary-boot mode (runs at 0x8000 behind the
# bootloader), matching the stock layout.

CROSS   := arm-none-eabi-
CC      := $(CROSS)gcc
OBJCOPY := $(CROSS)objcopy
SIZE    := $(CROSS)size

MCU     := mcu
LDPARAM := $(MCU)/hc32f46x_param.ld

# flash layout (secondary boot mode: app behind bootloader)
FLASH_START := 0x8000
FLASH_SIZE  := 0x80000
BOOT_MODE   := 2

LDFLASH_DEFS := -DLD_FLASH_START=$(FLASH_START) -DLD_FLASH_SIZE=$(FLASH_SIZE) -DLD_BOOT_MODE=$(BOOT_MODE)

INCLUDES := -I$(MCU) -Iapp -Ihal -Ilib -Iboard

CPU    := -mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16
CFLAGS := $(CPU) -Os -g3 -ffreestanding -fno-builtin -std=gnu11 -Wall \
          -ffunction-sections -fdata-sections -DHC32F460 $(LDFLASH_DEFS) $(INCLUDES) $(EXTRA)
LDFLAGS := $(CPU) -nostartfiles -Wl,--gc-sections -Wl,-Map=build/firmware.map \
           -T build/hc32f460.ld

# sources
MCU_SRCS := $(wildcard mcu/*.c)
APP_SRCS := device/startup_hc32f460.c \
            $(wildcard hal/*.c) \
            $(wildcard app/*.c) \
            $(wildcard lib/*.c) \
            $(wildcard board/*.c)

OBJDIR := build
OBJS := $(patsubst %.c,$(OBJDIR)/%.o,$(notdir $(APP_SRCS))) \
        $(patsubst %.c,$(OBJDIR)/%.o,$(notdir $(MCU_SRCS)))

all: $(OBJDIR)/firmware.bin

$(OBJDIR)/hc32f460.ld: $(LDPARAM) | $(OBJDIR)
	$(CC) -E -P -x c $(LDFLASH_DEFS) $(LDPARAM) -o $@

# object rules
$(OBJDIR)/%.o: device/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@
$(OBJDIR)/%.o: app/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@
$(OBJDIR)/%.o: hal/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@
$(OBJDIR)/%.o: lib/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@
$(OBJDIR)/%.o: board/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@
$(OBJDIR)/%.o: mcu/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR)/firmware.elf: $(OBJS) $(OBJDIR)/hc32f460.ld
	$(CC) $(OBJS) $(LDFLAGS) -o $@
	$(SIZE) $@

$(OBJDIR)/firmware.bin: $(OBJDIR)/firmware.elf
	$(OBJCOPY) -O binary $< $@

$(OBJDIR):
	mkdir -p $(OBJDIR)

clean:
	rm -rf $(OBJDIR)

.PHONY: all clean
