/* startup_boot.c - reset entry + vector table for the stage-1 bootloader.
 * Lives at flash 0x0. Cortex-M4F. Initialises the C runtime then runs bl_main,
 * which (optionally flashes a staged image and) jumps to the app at 0x8000. */
#include <stdint.h>
#include "bootloader.h"

extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss, _estack;
void Reset_Handler_Boot(void);
void Default_Handler(void);

#define ALIAS(x) __attribute__((weak, alias(#x)))
void NMI_Handler(void)        ALIAS(Default_Handler);
void HardFault_Handler(void)  ALIAS(Default_Handler);
void MemManage_Handler(void)  ALIAS(Default_Handler);
void BusFault_Handler(void)   ALIAS(Default_Handler);
void UsageFault_Handler(void) ALIAS(Default_Handler);
void SVC_Handler(void)        ALIAS(Default_Handler);
void PendSV_Handler(void)     ALIAS(Default_Handler);
void SysTick_Handler(void)    ALIAS(Default_Handler);

typedef void (*vector_t)(void);
__attribute__((section(".isr_vector"), used))
const vector_t g_boot_vectors[] = {
    (vector_t)&_estack,
    Reset_Handler_Boot, NMI_Handler, HardFault_Handler, MemManage_Handler,
    BusFault_Handler, UsageFault_Handler, 0, 0, 0, 0,
    SVC_Handler, 0, 0, PendSV_Handler, SysTick_Handler,
};

void Reset_Handler_Boot(void)
{
    /* enable single-precision FPU (CP10/CP11) */
    *(volatile uint32_t *)0xE000ED88u |= (0xFu << 20);
    __asm volatile ("dsb"); __asm volatile ("isb");

    uint32_t *s = &_sidata, *d = &_sdata;
    while (d < &_edata) *d++ = *s++;
    for (d = &_sbss; d < &_ebss; ) *d++ = 0;

    bl_main();                 /* never returns (jumps to app at 0x8000) */
    for (;;) { }
}

void Default_Handler(void) { for (;;) { } }
