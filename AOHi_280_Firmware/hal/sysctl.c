/* sysctl.c - clock/reset control on the HC32F460 DDL.
 *
 * The old chip gated peripherals through 0x40048004/0x40048000. On the HC32F460
 * clock gating is the PWC FCG registers, driven per-peripheral by each DDL driver
 * (display gates SPI3, extflash gates SPI1, uart gates USART1). The bootloader +
 * SystemInit() already bring the PLL/bus clocks up before main(), so the stock
 * "unlock + enable" chain is a no-op here. These keep the old API for callers. */
#include "sysctl.h"

void sysctl_unlock(void) { }
void periph_clk_enable(uint32_t mask, int en) { (void)mask; (void)en; }
void periph_reset(uint32_t mask, int assert_) { (void)mask; (void)assert_; }

/* stock clk_D5F0 -> clock unlock/enable chain; clocks already set by the
 * bootloader + DDL SystemInit, so nothing to do. */
void sysctl_unlock_chain(void) { }
