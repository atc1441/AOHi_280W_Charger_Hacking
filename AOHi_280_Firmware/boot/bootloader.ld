/* bootloader.ld - standalone stage-1 bootloader at flash 0x0 (32 KiB region).
 * Mirrors the stock layout: bootloader occupies 0x0..0x8000, then jumps to the
 * application vector table at 0x8000. */
MEMORY
{
    FLASH (rx)  : ORIGIN = 0x00000000, LENGTH = 0x8000     /* 32 KiB */
    RAM   (rwx) : ORIGIN = 0x1FFFC610, LENGTH = 0x2A9F0     /* 0x1FFFC610..0x20027000 */
}

/* Initial MSP = stock vector[0] (0x1FFFC610); stack grows down. RAM is 188 KiB. */
_estack = 0x1FFFC610;

ENTRY(Reset_Handler_Boot)

SECTIONS
{
    .isr_vector : { KEEP(*(.isr_vector)) . = ALIGN(4); } > FLASH

    .text :
    {
        *(.text .text.*)
        *(.glue_7) *(.glue_7t)
        *(.rodata .rodata.*)
        . = ALIGN(4);
        _etext = .;
    } > FLASH

    .ARM.extab : { *(.ARM.extab*) } > FLASH
    .ARM       : { __exidx_start = .; *(.ARM.exidx*) __exidx_end = .; } > FLASH

    _sidata = LOADADDR(.data);
    .data :
    {
        . = ALIGN(4); _sdata = .;
        *(.data .data.*)
        . = ALIGN(4); _edata = .;
    } > RAM AT > FLASH

    .bss (NOLOAD) :
    {
        . = ALIGN(4); _sbss = .; __bss_start__ = _sbss;
        *(.bss .bss.*) *(COMMON)
        . = ALIGN(4); _ebss = .; __bss_end__ = _ebss;
    } > RAM

    /DISCARD/ : { *(.comment) *(.ARM.attributes) }
}
