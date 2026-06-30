/* mcu_system.c - SystemInit / core-clock derivation / millisecond delay.
 *
 * SystemInit enables the FPU, brings the core clock up to the stock MPLL via
 * mcu_clk_init() (a from-scratch bootloader leaves the chip on HRC, so the app
 * must do it - mcu_clk_init is a no-op if a bootloader already did), then reads
 * the running clock back into SystemCoreClock by decoding the CMU clock-switch +
 * PLL registers. Ddl_Delay1ms is a calibrated busy loop. */
#include "hc32_common.h"
#include "system_hc32f460.h"
#include "mcu_clk.h"

uint32_t HRC_VALUE       = HRC_16MHz_VALUE;
uint32_t SystemCoreClock = MRC_VALUE;

void SystemCoreClockUpdate(void)
{
    uint32_t pllsource, plln, pllp, pllm;

    /* HRC trim select: ICG1.HRCFREQSEL bit0 (1 = 16 MHz, 0 = 20 MHz) */
    HRC_VALUE = (1u == (HRC_FREQ_MON() & 1u)) ? HRC_16MHz_VALUE : HRC_20MHz_VALUE;

    switch (M4_SYSREG->CMU_CKSWR_f.CKSW)
    {
        case 0x00: SystemCoreClock = HRC_VALUE;   break;   /* internal high-speed RC */
        case 0x01: SystemCoreClock = MRC_VALUE;   break;   /* internal middle-speed  */
        case 0x02: SystemCoreClock = LRC_VALUE;   break;   /* internal low-speed     */
        case 0x03: SystemCoreClock = XTAL_VALUE;  break;   /* external high-speed    */
        case 0x04: SystemCoreClock = XTAL32_VALUE;break;   /* external low-speed     */
        case 0x05:                                          /* MPLL                   */
            pllsource = M4_SYSREG->CMU_PLLCFGR_f.PLLSRC;
            plln      = M4_SYSREG->CMU_PLLCFGR_f.MPLLN;
            pllp      = M4_SYSREG->CMU_PLLCFGR_f.MPLLP;
            pllm      = M4_SYSREG->CMU_PLLCFGR_f.MPLLM;
            /* PLLCLK = (src / (pllm+1)) * (plln+1) / (pllp+1) */
            if (0u == pllsource)
                SystemCoreClock = XTAL_VALUE / (pllm + 1u) * (plln + 1u) / (pllp + 1u);
            else
                SystemCoreClock = HRC_VALUE  / (pllm + 1u) * (plln + 1u) / (pllp + 1u);
            break;
        default: break;
    }
}

void SystemInit(void)
{
#if (__FPU_PRESENT == 1) && (__FPU_USED == 1)
    SCB->CPACR |= ((3UL << 20) | (3UL << 22));   /* CP10/CP11 full access */
#endif
    mcu_clk_init();              /* raise core clock to the stock MPLL (~138.67 MHz) */
    SystemCoreClockUpdate();
}

void Ddl_Delay1ms(uint32_t u32Cnt)
{
    volatile uint32_t i;
    uint32_t u32Cyc = SystemCoreClock / 10000u;
    while (u32Cnt-- > 0u)
    {
        i = u32Cyc;
        while (i-- > 0u) { ; }
    }
}
