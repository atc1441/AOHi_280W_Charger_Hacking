# Makefile - standalone BWX468 stage-1 bootloader (flash 0x0, 32 KiB region).
# Builds build/bootloader.bin, a flat image to flash at 0x00000000. Separate
# from the application project (../Makefile, which builds the 0x8000 image).
#
#   make            -> build/bootloader.elf + .bin + size
#   make clean

CROSS   ?= arm-none-eabi-
CC      := $(CROSS)gcc
OBJCOPY := $(CROSS)objcopy
SIZE    := $(CROSS)size

ARCH    := -mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16
INCLUDES := -I. -I../device -I../hal
CFLAGS  := $(ARCH) -Os -g3 -ffreestanding -fno-builtin -std=c11 \
           -Wall -Wextra -ffunction-sections -fdata-sections $(INCLUDES)
LDFLAGS := $(ARCH) -nostdlib -nostartfiles -T bootloader.ld \
           -Wl,--gc-sections -Wl,-Map=build/bootloader.map

# Self-contained: the bootloader drives the EFM directly (register level), so it
# needs no DDL / hal flash driver. Only the startup stub and bootloader.c.
SRCS := \
  startup_boot.c \
  bootloader.c

OBJS := $(patsubst %.c,build/%.o,$(notdir $(SRCS)))
VPATH := .

.PHONY: all clean size
all: build/bootloader.bin size

build:
	mkdir -p build

build/%.o: %.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/bootloader.elf: $(OBJS) bootloader.ld | build
	$(CC) $(OBJS) $(LDFLAGS) -o $@

build/bootloader.bin: build/bootloader.elf
	$(OBJCOPY) -O binary $< $@

size: build/bootloader.elf
	$(SIZE) $<

clean:
	rm -rf build
