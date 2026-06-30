/* mcu_clk.h - core clock bring-up (MPLL).
 *
 * The stock bootloader raised the core clock to ~138.67 MHz (MPLL) before the
 * app ran; a from-scratch bootloader may leave the chip on HRC (~16 MHz), which
 * makes the whole firmware run ~9x too slow. mcu_clk_init() brings the MPLL up
 * to the exact stock configuration. It is idempotent: if a bootloader already
 * selected MPLL it just refreshes SystemCoreClock. Called from SystemInit(). */
#ifndef MCU_CLK_H
#define MCU_CLK_H

void mcu_clk_init(void);

#endif /* MCU_CLK_H */
