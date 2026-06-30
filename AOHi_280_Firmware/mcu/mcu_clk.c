/* mcu_clk.c - bring the core clock up to the stock 138.67 MHz MPLL.
 *
 * Values captured 1:1 from a stock-running device:
 *   CMU_PLLCFGR = 0x22201980  (src = HRC 16 MHz, M/1, N*26, P/3 -> 138.67 MHz)
 *   EFM_FRMC.FLWT = 5 + CACHE  (0x00010050)  flash latency for the higher clock
 *   SRAM WTCR = 0x11001111     (1 wait-state)  for the higher clock
 *   CMU_CKSWR.CKSW = 5         (MPLL as system clock)
 *
 * Sequence mirrors the DDL CLK driver: raise flash/SRAM latency first, configure
 * + enable MPLL and wait for it to lock, then switch the system clock with the
 * peripheral clock gates closed across the switch (FCG0..3 -> DEFAULT, restore).
 */
#include "hc32_ddl.h"      /* M4_MSTP, SystemCoreClockUpdate */
#include "mcu_clk.h"

/* M4_SYSREG (0x40054000) fields not in the minimal mcu struct - raw access. */
#define SYS_PWR_FPRC    (*(volatile uint16_t *)0x400543FEu)  /* reg-write protect (key 0xA5) */
#define SYS_CMU_CKSWR   (*(volatile uint8_t  *)0x40054026u)  /* [2:0] CKSW                   */
#define SYS_CMU_PLLCR   (*(volatile uint8_t  *)0x4005402Au)  /* bit0 MPLLOFF                 */
#define SYS_CMU_OSCSTB  (*(volatile uint8_t  *)0x4005403Cu)  /* bit5 MPLLSTBF                */
#define SYS_CMU_PLLCFGR (*(volatile uint32_t *)0x40054100u)

#define SRAMC_WTCR      (*(volatile uint32_t *)0x40050800u)
#define SRAMC_WTPR      (*(volatile uint32_t *)0x40050804u)  /* 0x77 unlock / 0x76 lock      */

#define EFM_FAPRT_R     (*(volatile uint32_t *)0x40010400u)  /* 0x123,0x3210 unlock          */
#define EFM_FRMC_R      (*(volatile uint32_t *)0x40010408u)
#define MSTP_FCG0PC     (*(volatile uint32_t *)0x40048010u)  /* FCG write protect (key A5A5) */

#define CKSW_MPLL       5u
#define FCG_STABLE      0x200u

static void spin(volatile uint32_t n) { while (n--) { __asm volatile (""); } }

void mcu_clk_init(void)
{
    if ((SYS_CMU_CKSWR & 7u) == CKSW_MPLL) {     /* a bootloader already raised it */
        SystemCoreClockUpdate();
        return;
    }

    /* 1. flash latency (FLWT=5) + cache on, behind the EFM unlock, BEFORE speed-up */
    EFM_FAPRT_R = 0x0123u; EFM_FAPRT_R = 0x3210u;
    EFM_FRMC_R  = 0x00010050u;
    EFM_FAPRT_R = 0x0000u;

    /* 2. high-speed SRAM 1 wait-state (WTPR 0x77 = unlock, 0x76 = lock) */
    SRAMC_WTPR = 0x77u; SRAMC_WTCR = 0x11001111u; SRAMC_WTPR = 0x76u;

    /* 3. configure + enable MPLL (clock registers unlocked via FPRC key 0xA5) */
    SYS_PWR_FPRC |= 0xA501u;
    SYS_CMU_PLLCFGR = 0x22201980u;
    SYS_CMU_PLLCR  &= ~1u;                       /* MPLLOFF = 0 -> enable */
    { volatile uint32_t t = 0; while (!(SYS_CMU_OSCSTB & (1u << 5)) && ++t < 0x100000u) {} }
    SYS_PWR_FPRC = (uint16_t)((SYS_PWR_FPRC & ~1u) | 0xA500u);

    /* 4. switch system clock to MPLL with all peripheral clocks gated across the
     *    switch (DDL CLK_SetSysClkSource). FCG0..3 are at reset (all-gated) here. */
    MSTP_FCG0PC = 0xA5A50001u;                   /* unlock FCG writes */
    uint32_t f0 = M4_MSTP->FCG0, f1 = M4_MSTP->FCG1, f2 = M4_MSTP->FCG2, f3 = M4_MSTP->FCG3;
    M4_MSTP->FCG0 = 0xFFFFFAEEu; M4_MSTP->FCG1 = 0xFFFFFFFFu;
    M4_MSTP->FCG2 = 0xFFFFFFFFu; M4_MSTP->FCG3 = 0xFFFFFFFFu;
    spin(FCG_STABLE);

    SYS_PWR_FPRC |= 0xA501u;
    SYS_CMU_CKSWR = (uint8_t)((SYS_CMU_CKSWR & ~7u) | CKSW_MPLL);
    SYS_PWR_FPRC = (uint16_t)((SYS_PWR_FPRC & ~1u) | 0xA500u);
    spin(FCG_STABLE);

    M4_MSTP->FCG0 = f0; M4_MSTP->FCG1 = f1; M4_MSTP->FCG2 = f2; M4_MSTP->FCG3 = f3;
    MSTP_FCG0PC = 0xA5A50000u;                   /* re-lock FCG writes */
    spin(FCG_STABLE);

    SystemCoreClockUpdate();
}
