/* power.h - USB-PD / charge-controller power monitoring for BWX468.
 *
 * Reconstructed from stock pd_read_reg / poll_active_port_status. Each port has
 * a PD/charge controller on the bit-banged I2C bus. Register map (recovered):
 *   reg 0x01 : link/CC status (bit0 = data-role/active)
 *   reg 0x02 : connection status (bit0 = attached, bit1, bit2 = power-valid)
 *   reg 0x21 : negotiated power reading
 */
#ifndef HAL_POWER_H
#define HAL_POWER_H
#include <stdint.h>

#define PD_REG_LINK     0x01
#define PD_REG_CONN     0x02
#define PD_REG_POWER    0x21

typedef struct {
    uint8_t  valid;       /* 1 = read succeeded                     */
    uint8_t  attached;    /* reg2 bit0                              */
    uint8_t  conn_b1;     /* reg2 bit1                              */
    uint8_t  power_valid; /* reg2 bit2                              */
    uint32_t power;       /* reg0x21, 0 unless attached && power_valid */
} pd_status_t;

/* Read a single PD register (4 bytes LE). Returns 1 on success. */
int  pd_read(uint8_t port, uint8_t reg, uint32_t *out);

/* Poll one port and update *st. Mirrors stock poll_active_port_status. */
int  power_poll(uint8_t port, pd_status_t *st);
void pdc_scan(volatile unsigned char *out);
void pdc_init(void);
int  pd_port_read(uint8_t addr7, uint8_t reg, uint32_t *out, int n);

/* Last total power latched by power_poll (stock g_total_power). */
extern volatile uint32_t g_total_power;

/* Single-byte PD-config register access (stock sub_176FC / sub_1740C, addr 0x78/0x79).
 * Used by the charge-controller init sequence. Returns 1 on success. */
int pd_reg_write8(uint8_t reg, uint8_t val);
int pd_reg_read8(uint8_t reg, uint8_t *val);

/* Main-rail ADC reads (stock fn_1A8B8 + update_readings @0x198D8). The ADC chip
 * sits on the same 0x78 bus; HW-gated (reads 0 with the PSU disconnected). */
typedef struct {
    uint16_t v1_x100;   /* ch1 scaled  ((5*adc)>>1)/100 */
    uint16_t v2_x100;   /* ch2 scaled */
    uint16_t i_x100;    /* ch5 scaled  (6*adc)/100 */
} adc_readings_t;
extern adc_readings_t g_adc;

int  adc_read_channel(uint8_t ch);   /* stock fn_1A8B8: 15-bit ADC channel read */
void update_readings(void);          /* stock update_readings: refresh g_adc     */

/* stock soft_1A880: CRC-8 (poly 0x07) over a PD-config packet. */
uint8_t pd_crc8(const uint8_t *buf, int len);

/* Per-outlet PD state (stock byte_1FFF8390, 24-byte record x 6 outlets). */
typedef struct {
    uint8_t  attach_cnt;   /* +0  attach debounce (->100) */
    uint8_t  detach_cnt;   /* +1  detach debounce (->100) */
    uint8_t  ch_target;    /* +2  desired channel on/off  */
    uint8_t  ch_applied;   /* +3  last applied state       */
    uint8_t  f4;           /* +4  (b1>>4)                  */
    uint8_t  pad5[3];
    uint32_t volt_raw;     /* +8  voltage ((b4&3)<<8|b3), /10 = V  (power_calc reads here) */
    uint32_t aux12;        /* +12 ((b1&3)<<8|b2)           */
    uint8_t  pad16[4];
    uint32_t cur_raw;      /* +20 current (b0 & 0x3F), /10 = A     */
} outlet_state_t;
extern outlet_state_t g_outlet[6];
extern uint16_t       g_outlet_status;   /* stock unk_1FFF840A connected bitmap */
extern float          g_port_limit[12];  /* WLAN SET_CONFIG per-port V/I limits  */

int  pd_port_read_buf(uint8_t addr7, uint8_t reg, uint8_t *buf, int n);
int  pd_port_write(uint8_t addr7, uint8_t reg, uint8_t val);
void port_io_xfer(uint8_t ch, uint8_t *pkt6);   /* stock 0x17314 */
unsigned apply_port_cfg(uint8_t ch);            /* stock 0x19A24 */
int  pd_outlet_power(uint8_t ch);               /* stock sub_13240: outlet apparent power */
void pd_power_calc(void);                        /* stock power_calc core: g_power_w sum    */
uint8_t pd_attached_count(void);                 /* stock fn_17CF0: # attached outlets      */
extern uint8_t g_charge_class;                   /* stock byte_20013628 charging class      */
int  pd_power_cap(void);                          /* stock sub_DA78: cap W by charge class    */
uint8_t pd_classify_limit(uint32_t limit_uw);     /* stock sub_133E4 classifier (limit->class)*/

/* Channel power switching / settle (stock i2c_1A5F4 / fn_19864 / sync_channel_state /
 * fn_18234). g_channel_mask = desired per-channel on/off (set by WLAN config). */
extern uint8_t g_channel_mask, g_channel_last, g_channels_ready;
void pd_channel_switch(uint8_t ch, uint8_t on);
void pd_channel_cfg_hi(void);
void pd_channel_cfg_lo(void);
void sync_channel_state(void);
void pd_settle_tick(void);

/* High-power threshold state (stock power_temp_task hysteresis on total watts;
 * flt_1FFF8304/8308 = sum of per-outlet V*I, NOT temperature). UI-indicator only. */
extern float   g_power_w;
extern uint8_t g_hipwr1, g_hipwr2;
void pd_power_level_check(void);

/* Watchdog feed (stock WDT_Feed). */
void WDT_Feed(void);

#endif /* HAL_POWER_H */
