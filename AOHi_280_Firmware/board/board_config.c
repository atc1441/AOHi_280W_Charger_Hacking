/* board_config.c - per-board hardware tables for BWX468.
 *
 * The I2C/PD port table mirrors the stock g_port_cfg per-port entries
 * { sda gpio port, sda mask, 7-bit device address }. The stock values are
 * loaded at runtime from settings, so the concrete numbers below are the
 * RE'd defaults and must be confirmed against the live device's g_port_cfg.
 */
#include "i2c.h"
#include "config.h"

/* NV config field layout within the 0x68000 blob (offset, size).
 * RE'd defaults; confirm field offsets against the device's settings layout. */
const cfg_field_t g_cfg_fields[] = {
    {  0, 1 },   /* key 0: brightness        */
    {  1, 1 },   /* key 1: page/menu default */
    {  2, 4 },   /* key 2: timezone offset   */
    {  6, 2 },   /* key 3: flags             */
};
const uint8_t g_cfg_field_count = (uint8_t)(sizeof(g_cfg_fields)/sizeof(g_cfg_fields[0]));

const i2c_port_cfg_t g_i2c_ports[] = {
    /* Per-port telemetry I2C, now CONFIRMED via stock disassembly + live RAM dump.
     * SCL = port2 pin13 (0x2000, i2c_scl_set @0x18570).
     * SDA table = g_port_cfg+20 (0x1FFF8138), stride 6: [+0]=SDA port (byte),
     * [+2]=SDA mask (u16), [+4]=7-bit addr (i2c_sda_set @0x1864C; sub_171DC
     * @0x171DC sends addr<<1). Live g_port_cfg dump (0x1FFF8124) on stock:
     * port0 = {sdaport 0x05=PortF, mask 0x0004=PF2, addr 0x16}; ports1+ = zero,
     * so the stock device has ONE telemetry port. (Earlier PC11/PC12/0x3D/0x3E
     * were placeholders; this is the real config and explains the PortF PODR/POER
     * A/B diff - stock drives PF2 as the SDA, mine previously did not.) */
    { 5, 0x0004, 0x16 },   /* port 0: SDA = PortF pin2 (PF2), 7-bit addr 0x16 */
};

const uint8_t g_i2c_port_count = (uint8_t)(sizeof(g_i2c_ports) / sizeof(g_i2c_ports[0]));
