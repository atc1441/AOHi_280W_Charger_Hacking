/* charge.c - charge-output gating (clean-room from stock sub_DAB0). */
#include "charge.h"
#include "i2c.h"
#include "power.h"

uint8_t g_charge_enabled;
static uint8_t s_charge_state;   /* mirror of stock byte_2000F61A current state */

/* charge_keepalive: re-assert the 0x16 master-charger enable (reg 1 = 1) UNCONDITIONALLY.
 * Stock's i2c_DAB0 never updates its gate byte (byte_2000F61A), so stock re-writes reg 1 = 1 on
 * EVERY main-loop pass (HW-confirmed: 250/250 captured writes were reg1=1). The 0x16 controller
 * treats this as a keepalive - it holds the high-voltage bus up only while the host keeps poking it;
 * a single one-shot enable lets it time back down to the 5V USB-default, which is exactly why C2-C4
 * could never hold a 20V PD contract. Call this every loop when the charger should be sourcing. */
volatile uint32_t g_ka_count;   /* DIAG: counts reg1=1 keepalive writes (measure real rate via SWD) */
void charge_keepalive(void)
{
    if (g_charge_enabled) {
        i2c_write_reg32(0, 0x01, 1);
        g_ka_count++;
    }
}

int charge_set_enable(uint8_t on)
{
    on &= 1;
    if (on == s_charge_state)
        return 1;                       /* already in requested state */

    /* stock sub_DAB0 (0xdab0) -> i2c_write_port_reg(reg=1, &state32): the 0x16 master-charger
     * controller's HW-I2C write ALWAYS uses the format [addr][reg][LEN=4][val32]. i2c_DAB0 passes a
     * 32-bit `state` (= on&1), so the wire bytes are [addr][0x01][0x04][on][0][0][0]. Writing reg 1 = 1
     * is the MASTER CHARGER ENABLE - it brings up the high-voltage bus that feeds the SW3566 bucks.
     * The earlier "1-byte" transcription (i2c_write_reg8 -> LEN=1) sent a SHORT frame the 0x16 ignored,
     * so the master stayed in its 5V-only default and C2-C4 could never negotiate >5V (HW-confirmed:
     * stock writes reg1=1 continuously; SDK was sending the wrong length). Use the 4-byte form. */
    if (!i2c_write_reg32(0, 0x01, on))
        return 0;

    if (on) {
        g_charge_enabled = 1;
    } else {
        g_charge_enabled = 0;
        g_total_power = 0;
    }
    s_charge_state = on;
    return 1;
}
