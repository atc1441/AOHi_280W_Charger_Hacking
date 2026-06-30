/* startup_hc32f460.c - C startup for HC32F460xE (rewritten for the HDSC DDL).
 * Mirrors the framework's startup.cpp/vector_table.cpp: SRAM wait-states, .data
 * copy, .bss clear, SystemInit, then main(). 16 system vectors + 144 INTC IRQs. */
#include <stdint.h>

extern uint32_t __etext, __data_start__, __data_end__;
extern uint32_t __bss_start__, __bss_end__;
extern uint32_t __StackTop;
extern int main(void);
extern void SystemInit(void);

void Reset_Handler(void);
void Default_Handler(void);

void NMI_Handler(void) __attribute__((weak, alias("Default_Handler")));
void HardFault_Handler(void) __attribute__((weak, alias("Default_Handler")));
void MemManage_Handler(void) __attribute__((weak, alias("Default_Handler")));
void BusFault_Handler(void) __attribute__((weak, alias("Default_Handler")));
void UsageFault_Handler(void) __attribute__((weak, alias("Default_Handler")));
void SVC_Handler(void) __attribute__((weak, alias("Default_Handler")));
void DebugMon_Handler(void) __attribute__((weak, alias("Default_Handler")));
void PendSV_Handler(void) __attribute__((weak, alias("Default_Handler")));
void SysTick_Handler(void) __attribute__((weak, alias("Default_Handler")));

void IRQ000_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ001_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ002_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ003_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ004_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ005_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ006_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ007_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ008_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ009_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ010_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ011_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ012_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ013_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ014_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ015_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ016_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ017_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ018_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ019_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ020_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ021_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ022_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ023_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ024_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ025_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ026_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ027_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ028_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ029_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ030_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ031_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ032_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ033_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ034_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ035_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ036_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ037_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ038_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ039_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ040_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ041_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ042_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ043_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ044_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ045_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ046_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ047_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ048_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ049_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ050_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ051_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ052_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ053_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ054_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ055_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ056_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ057_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ058_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ059_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ060_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ061_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ062_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ063_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ064_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ065_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ066_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ067_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ068_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ069_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ070_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ071_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ072_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ073_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ074_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ075_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ076_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ077_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ078_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ079_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ080_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ081_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ082_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ083_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ084_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ085_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ086_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ087_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ088_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ089_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ090_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ091_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ092_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ093_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ094_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ095_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ096_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ097_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ098_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ099_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ100_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ101_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ102_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ103_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ104_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ105_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ106_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ107_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ108_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ109_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ110_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ111_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ112_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ113_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ114_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ115_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ116_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ117_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ118_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ119_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ120_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ121_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ122_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ123_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ124_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ125_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ126_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ127_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ128_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ129_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ130_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ131_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ132_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ133_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ134_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ135_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ136_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ137_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ138_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ139_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ140_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ141_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ142_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ143_Handler(void) __attribute__((weak, alias("Default_Handler")));

/* Vector table (.vectors, placed first by the linker). */
__attribute__((section(".vectors"), used)) void (* const g_vectors[])(void) = {
    (void(*)(void))&__StackTop,
    Reset_Handler, NMI_Handler, HardFault_Handler, MemManage_Handler,
    BusFault_Handler, UsageFault_Handler, 0, 0, 0, 0,
    SVC_Handler, DebugMon_Handler, 0, PendSV_Handler, SysTick_Handler,
    IRQ000_Handler, IRQ001_Handler, IRQ002_Handler, IRQ003_Handler, IRQ004_Handler, IRQ005_Handler, IRQ006_Handler, IRQ007_Handler,
    IRQ008_Handler, IRQ009_Handler, IRQ010_Handler, IRQ011_Handler, IRQ012_Handler, IRQ013_Handler, IRQ014_Handler, IRQ015_Handler,
    IRQ016_Handler, IRQ017_Handler, IRQ018_Handler, IRQ019_Handler, IRQ020_Handler, IRQ021_Handler, IRQ022_Handler, IRQ023_Handler,
    IRQ024_Handler, IRQ025_Handler, IRQ026_Handler, IRQ027_Handler, IRQ028_Handler, IRQ029_Handler, IRQ030_Handler, IRQ031_Handler,
    IRQ032_Handler, IRQ033_Handler, IRQ034_Handler, IRQ035_Handler, IRQ036_Handler, IRQ037_Handler, IRQ038_Handler, IRQ039_Handler,
    IRQ040_Handler, IRQ041_Handler, IRQ042_Handler, IRQ043_Handler, IRQ044_Handler, IRQ045_Handler, IRQ046_Handler, IRQ047_Handler,
    IRQ048_Handler, IRQ049_Handler, IRQ050_Handler, IRQ051_Handler, IRQ052_Handler, IRQ053_Handler, IRQ054_Handler, IRQ055_Handler,
    IRQ056_Handler, IRQ057_Handler, IRQ058_Handler, IRQ059_Handler, IRQ060_Handler, IRQ061_Handler, IRQ062_Handler, IRQ063_Handler,
    IRQ064_Handler, IRQ065_Handler, IRQ066_Handler, IRQ067_Handler, IRQ068_Handler, IRQ069_Handler, IRQ070_Handler, IRQ071_Handler,
    IRQ072_Handler, IRQ073_Handler, IRQ074_Handler, IRQ075_Handler, IRQ076_Handler, IRQ077_Handler, IRQ078_Handler, IRQ079_Handler,
    IRQ080_Handler, IRQ081_Handler, IRQ082_Handler, IRQ083_Handler, IRQ084_Handler, IRQ085_Handler, IRQ086_Handler, IRQ087_Handler,
    IRQ088_Handler, IRQ089_Handler, IRQ090_Handler, IRQ091_Handler, IRQ092_Handler, IRQ093_Handler, IRQ094_Handler, IRQ095_Handler,
    IRQ096_Handler, IRQ097_Handler, IRQ098_Handler, IRQ099_Handler, IRQ100_Handler, IRQ101_Handler, IRQ102_Handler, IRQ103_Handler,
    IRQ104_Handler, IRQ105_Handler, IRQ106_Handler, IRQ107_Handler, IRQ108_Handler, IRQ109_Handler, IRQ110_Handler, IRQ111_Handler,
    IRQ112_Handler, IRQ113_Handler, IRQ114_Handler, IRQ115_Handler, IRQ116_Handler, IRQ117_Handler, IRQ118_Handler, IRQ119_Handler,
    IRQ120_Handler, IRQ121_Handler, IRQ122_Handler, IRQ123_Handler, IRQ124_Handler, IRQ125_Handler, IRQ126_Handler, IRQ127_Handler,
    IRQ128_Handler, IRQ129_Handler, IRQ130_Handler, IRQ131_Handler, IRQ132_Handler, IRQ133_Handler, IRQ134_Handler, IRQ135_Handler,
    IRQ136_Handler, IRQ137_Handler, IRQ138_Handler, IRQ139_Handler, IRQ140_Handler, IRQ141_Handler, IRQ142_Handler, IRQ143_Handler,
};

void Default_Handler(void) { for(;;){} }

void Reset_Handler(void)
{
    /* copy .data from flash to RAM */
    uint32_t *s=&__etext, *d=&__data_start__;
    while (d < &__data_end__) *d++ = *s++;
    /* clear .bss */
    for (d=&__bss_start__; d<&__bss_end__; ) *d++ = 0;
    SystemInit();
    main();
    for(;;){}
}
