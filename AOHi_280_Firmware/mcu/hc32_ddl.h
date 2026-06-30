/* hc32_ddl.h - umbrella header for the MCU support layer.
 *
 * Pulls in the register map, core, and every peripheral driver the firmware
 * uses. Keeps the historic include name so the HAL/app sources include it
 * unchanged; the contents are the self-contained mcu/ layer, not the vendor DDL. */
#ifndef HC32_DDL_H
#define HC32_DDL_H

#include "hc32_common.h"
#include "system_hc32f460.h"

#include "hc32f460_gpio.h"
#include "hc32f460_spi.h"
#include "hc32f460_usart.h"
#include "hc32f460_pwc.h"
#include "hc32f460_efm.h"
#include "hc32f460_rtc.h"
#include "hc32f460_swdt.h"
#include "hc32f460_clk.h"
#include "hc32f460_interrupts.h"
#include "hc32f460_utility.h"

#endif /* HC32_DDL_H */
