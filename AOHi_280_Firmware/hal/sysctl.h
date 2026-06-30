/* sysctl.h - peripheral clock-gate and reset control (stock sub_A5EC/sub_A5B0).
 * Clock-gate register 0x40048004, reset register 0x40048000. The bit `mask`
 * identifies the peripheral; `en`=1 enables clock / releases reset. */
#ifndef HAL_SYSCTL_H
#define HAL_SYSCTL_H
#include <stdint.h>

void sysctl_unlock(void);                        /* reset_ctrl_unlock: 0x40048010=0xA5A5A5C1 */
void periph_clk_enable(uint32_t mask, int en);   /* sub_A5EC */
void periph_reset(uint32_t mask, int assert_);    /* sub_A5B0 */

#endif /* HAL_SYSCTL_H */
