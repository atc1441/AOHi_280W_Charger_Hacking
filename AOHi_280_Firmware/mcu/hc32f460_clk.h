/* hc32f460_clk.h - clock API placeholder (clean-room).
 * The bootloader + SystemInit bring the PLL/bus clocks up; the firmware only
 * reads SystemCoreClock and never reconfigures the CMU, so no CLK_* entry
 * points are needed here. Kept so existing includes resolve. */
#ifndef HC32F460_CLK_H
#define HC32F460_CLK_H

#include "hc32_common.h"
#include "system_hc32f460.h"

#endif /* HC32F460_CLK_H */
