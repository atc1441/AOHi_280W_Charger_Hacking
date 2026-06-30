/*
 * parameterized linker flag for the HC32F46x SOC.
 * the linker script has to be pre-processed using the C preprocessor
 * to allow for parameterization.
 * 
 * to do so, run "$CC -C -P -x c -E <SOURCE> -o <TARGET> -D<PARAM_NAME>=<VALUE>"", adding defines as described below.
 * see https://stackoverflow.com/a/35824964/13942493 
 *
 * 
 * Parameter List:
 * - LD_FLASH_START: start address of flash memory
 * - LD_FLASH_SIZE: size of flash memory
 * - LD_BOOT_MODE: boot mode of the firmware image
 *   * 1: primary boot mode, e.g. bootloader (default)
 *   * 2: secondary boot mode, e.g. application firmware (called by a bootloader) (saves ~1.5k flash)
 * - DDL_STACK_SIZE: size of the stack, in bytes
 * - DDL_HEAP_SIZE: size of the heap, in bytes
 * 
 * note: previously, ram options were also configurable. 
 * this was removed to simplify the linker script (all HC32F460 have the same ram anyway)
 */

/*
;*******************************************************************************
; * Copyright (C) 2020, Huada Semiconductor Co., Ltd. All rights reserved.
; *
; * This software component is licensed by HDSC under BSD 3-Clause license
; * (the "License"); You may not use this file except in compliance with the
; * License. You may obtain a copy of the License at:
; *                    opensource.org/licenses/BSD-3-Clause
*/
/*****************************************************************************/
/*  File        hc32f460xc_flash.ld                                          */
/*  Abstract    Linker script for HC32F460 Device with                       */
/*              256KByte FLASH, 192KByte RAM                                 */
/*  Version     V1.0                                                         */
/*  Date        2019-03-13                                                   */
/*****************************************************************************/

/* ensure parameters are defined */
#ifndef LD_FLASH_START
  #error "LD_FLASH_START not defined"
#endif

#ifndef LD_FLASH_SIZE
  #error "LD_FLASH_SIZE not defined"
#endif

#ifndef LD_BOOT_MODE
  #warning "LD_BOOT_MODE not defined, fallback to PRIMARY (1)"
  #define LD_BOOT_MODE 1
#endif

#ifndef DDL_STACK_SIZE
  #define DDL_STACK_SIZE 0x00000400 // 1KB
#endif

#ifndef DDL_HEAP_SIZE
  #define DDL_HEAP_SIZE 0x00002000 // 8KB
#endif

/* check if boot mode is valid */
#if LD_BOOT_MODE != 1 && LD_BOOT_MODE != 2
  #error "LD_BOOT_MODE must be PRIMARY (1) or SECONDARY (2)"
#endif

/* check boot mode is allowed */
#if LD_FLASH_START == 0 && LD_BOOT_MODE != 1
  #error "bootloader firmware is required to use PRIMARY boot mode!"
#endif

/* flash start offset reduces effective flash size available */
#define FLASH_SIZE_REAL (LD_FLASH_SIZE - LD_FLASH_START)
#if FLASH_SIZE_REAL < 0
  #error "effective flash size is negative! check your linker script parameters"
#endif

/* Use contiguous memory regions for simple. */
MEMORY
{
    FLASH       (rx): ORIGIN = LD_FLASH_START, LENGTH = FLASH_SIZE_REAL
    OTP         (rx): ORIGIN = 0x03000C00, LENGTH = 1020
    RAM        (rwx): ORIGIN = 0x1FFF8000, LENGTH = 188K
    RET_RAM    (rwx): ORIGIN = 0x200F0000, LENGTH = 4K
}

ENTRY(Reset_Handler)

SECTIONS
{
    /* vector table: 0x000 - 0x27F + padding  */
    .vectors :
    {
        KEEP(*(.vectors));

        #if LD_BOOT_MODE == 1
            /* add padding to ICG start */
            . = 0x400;
        #else
            /* no padding since ICG is removed */
            . = 0x280;
        #endif
    } >FLASH

    #if LD_BOOT_MODE == 1
        /* ICG: 0x400 - 0x41F*/
        .icg_sec :
        {
            ASSERT(. == (ORIGIN(FLASH) + 0x400), "icg_sec alignment issue");
            KEEP(*(.icg_sec));
            ASSERT(. == (ORIGIN(FLASH) + 0x420), "icg_sec alignment issue");
        } >FLASH
    #endif

    /* append all other FLASH sections after */
    .text :
    {
        . = ALIGN(4);
        *(.text)
        *(.text*)
        *(.glue_7)
        *(.glue_7t)
        *(.eh_frame)

        KEEP(*(.init))
        KEEP(*(.fini))
        . = ALIGN(4);
    } >FLASH

    .rodata :
    {
        . = ALIGN(4);
        *(.rodata)
        *(.rodata*)
        . = ALIGN(4);
    } >FLASH

    .ARM.extab :
    {
        *(.ARM.extab* .gnu.linkonce.armextab.*)
    } >FLASH

    __exidx_start = .;
    .ARM.exidx :
    {
        *(.ARM.exidx* .gnu.linkonce.armexidx.*)
    } >FLASH
    __exidx_end = .;

    .preinit_array :
    {
        . = ALIGN(4);
        /* preinit data */
        PROVIDE_HIDDEN (__preinit_array_start = .);
        KEEP(*(.preinit_array))
        PROVIDE_HIDDEN (__preinit_array_end = .);
        . = ALIGN(4);
    } >FLASH

    .init_array :
    {
        . = ALIGN(4);
        /* init data */
        PROVIDE_HIDDEN (__init_array_start = .);
        KEEP(*(SORT(.init_array.*)))
        KEEP(*(.init_array))
        PROVIDE_HIDDEN (__init_array_end = .);
        . = ALIGN(4);
    } >FLASH

    .fini_array :
    {
        . = ALIGN(4);
        /* finit data */
        PROVIDE_HIDDEN (__fini_array_start = .);
        KEEP(*(SORT(.fini_array.*)))
        KEEP(*(.fini_array))
        PROVIDE_HIDDEN (__fini_array_end = .);
        . = ALIGN(4);
    } >FLASH

    __etext = ALIGN(4);

    .otp_sec :
    {
        KEEP(*(.otp_sec))
    } >OTP

    .otp_lock_sec 0x03000FC0 :
    {
        KEEP(*(.otp_lock_sec))
    } >OTP

    /* --- stock RAM-layout reservation (user request: device RAM byte-comparable
       with the stock firmware). Pin the display-state structs at stock's exact
       absolute addresses, then push the general .data/.bss above them. NOLOAD =
       not stored in flash; this region is zeroed explicitly at boot. --- */
    .stock_ram (NOLOAD) :
    {
        . = ABSOLUTE(0x1FFF8150);   /* stock weather/loc struct (byte_1FFF8150) */
        KEEP(*(.stock_weather))
        . = ABSOLUTE(0x1FFF8294);   /* stock UI-state struct  (byte_1FFF8294) */
        KEEP(*(.stock_ui))
        . = ABSOLUTE(0x1FFF8700);   /* general .data/.bss start above the reserve */
    } >RAM

    .data : AT (__etext)
    {
        . = ALIGN(4);
        __data_start__ = .;
        *(vtable)
        *(.data)
        *(.data*)
        . = ALIGN(4);
        *(.ramfunc)
        *(.ramfunc*)
        . = ALIGN(4);
        __data_end__ = .;
    } >RAM

    __etext_ret_ram = __etext + ALIGN (SIZEOF(.data), 4);
    .ret_ram_data : AT (__etext_ret_ram)
    {
        . = ALIGN(4);
        __data_start_ret_ram__ = .;
        *(.ret_ram_data)
        *(.ret_ram_data*)
        . = ALIGN(4);
        __data_end_ret_ram__ = .;
    } >RET_RAM

    .bss :
    {
        . = ALIGN(4);
        _sbss = .;
        __bss_start__ = _sbss;
        *(.bss)
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
        __bss_end__ = _ebss;
    } >RAM

    .ret_ram_bss :
    {
        . = ALIGN(4);
        __bss_start_ret_ram__ = .;
        *(.ret_ram_bss)
        *(.ret_ram_bss*)
        . = ALIGN(4);
        __bss_end_ret_ram__ = .;
    } >RET_RAM

    .heap_stack (COPY) :
    {
        . = ALIGN(8);
        __end__ = .;
        PROVIDE(end = .);
        PROVIDE(_end = .);

        __HeapBase = .;
        . += DDL_HEAP_SIZE;
        . = ALIGN(8);
        __HeapLimit = .;

        __StackLimit = .;
        . += DDL_STACK_SIZE;
        . = ALIGN(8);
        __StackTop = .;
    } >RAM

    /DISCARD/ :
    {
        libc.a (*)
        libm.a (*)
        libgcc.a (*)
    }

    .ARM.attributes 0 : { *(.ARM.attributes) }

    PROVIDE(_stack = __StackTop);
    PROVIDE(_Min_Heap_Size = __HeapLimit - __HeapBase);
    PROVIDE(_Min_Stack_Size = __StackTop - __StackLimit);

    __RamEnd = ORIGIN(RAM) + LENGTH(RAM);
    ASSERT(__StackTop <= __RamEnd, "region RAM overflowed with stack")

    /* ensure that HEAP and STACK don't overlap with each other OR bss */
    ASSERT(__HeapBase <= __HeapLimit, "HEAP is broken")
    ASSERT(__StackLimit <= __StackTop, "STACK is broken")
    ASSERT(__bss_end__ <= __HeapBase, "HEAP should follow BSS in memory")
    ASSERT(__HeapLimit <= __StackLimit, "STACK should follow HEAP in memory")
}
