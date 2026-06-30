/* charge.h - charge-output enable gating (clean-room from stock sub_DAB0).
 * Writes PD register 1 to enable/disable charging and tracks the active flag. */
#ifndef HAL_CHARGE_H
#define HAL_CHARGE_H
#include <stdint.h>

/* Enable(1)/disable(0) charge output. Returns 1 on success, 0 on bus error.
 * No-op (returns 1) if already in the requested state. */
int charge_set_enable(uint8_t on);
void charge_keepalive(void);       /* re-assert 0x16 reg1=1 every loop (stock i2c_DAB0 keepalive) */

extern uint8_t g_charge_enabled;   /* mirror of stock byte_1FFF80A0 */

#endif /* HAL_CHARGE_H */
