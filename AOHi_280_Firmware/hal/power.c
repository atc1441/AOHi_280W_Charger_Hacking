/* power.c - PD/charge power monitoring (clean-room from stock). */
#include "power.h"
#include "i2c.h"
#include "gpio.h"
#include "charge.h"
#include "wlan.h"      /* g_telemetry (display fan-out target) */
#include "wdt.h"       /* SWDT feed via DDL */
#include "tick.h"      /* get_tick_ms */
#include "menu_state.h"/* g_menu_state live per-port telemetry (Smart distribution) */
#include <hc32f460_gpio.h>   /* DDL PORT_Init/SetBits/ResetBits/GetBit - stock uses these */

volatile uint32_t g_total_power;

/* ===== PD-config bus: dedicated bit-bang on GPIO port 1 =====
 * SCL = port1 bit3 (0x08), SDA = port1 bit4 (0x10). Distinct from the power-read
 * bus. Faithful to stock sub_C648/C680/C674/175E8/17784/174E8/1748C/17618. */
#define PDCFG_PORT   1u
#define PDCFG_SCL    0x08u
#define PDCFG_SDA    0x10u
#define PDCFG_ADDR_W 0x78u
#define PDCFG_ADDR_R 0x79u

/* Match stock's PDC-bus delay (0x134f0: n = us<<5, ~6-cycle loop ~= 4.6us for us=4).
 * My old us*8 with a ~3-cycle loop was ~8x too FAST (~0.57us) -> the PD controller
 * could not keep up and never ACKed (bus looked dead). us*64 here ~= stock's cycles. */
/* Match stock's delay (0x134f0): n = us<<5. My old us*64 ran the bus ~2x slower than
 * stock, which can time the PD controller out mid-read -> marginal/empty packets. */
/* us*64 empirically gives the correct bit time on this board: the released-SDA pull-up
 * rise needs this long to settle before sampling - faster (us<<5) sampled too early and
 * every released '1'/NACK read 0 (decisive scan went 5 ACKs -> 123 false ACKs). */
/* Match stock delayus (0x134f0) WALL-CLOCK: stock = us*32 iterations of a ~10-cycle loop =
 * ~320 cycles/us. My loop is ~8 cycles, so us*40 gives the same ~320 cycles/us. (Was us*64 =
 * ~512 cycles/us = 1.6x SLOWER than stock - the PD bit-bang serviced the chips too slowly during
 * the fast 20V PD ramp, contributing to the SW3566 output tripping.) */
static void udelay(int us) { volatile int n = us * 40; while (n--) { } }
static void pdc_scl(int hi) { hi ? gpio_set_pins(PDCFG_PORT, PDCFG_SCL) : gpio_clear_pins(PDCFG_PORT, PDCFG_SCL); }
static void pdc_sda(int hi) { hi ? gpio_set_pins(PDCFG_PORT, PDCFG_SDA) : gpio_clear_pins(PDCFG_PORT, PDCFG_SDA); }
static int  pdc_sda_rd(void) { return gpio_read(PDCFG_PORT, PDCFG_SDA); }

/* PortB PCR (pin control) registers, NOD = bit2 = open-drain. THE FIX: a live diff
 * showed stock drives PB3/PB4 OPEN-DRAIN (NOD=1) while my push-pull (NOD=0) fought
 * the slave -> reads returned 0 (writes still worked). I2C must be open-drain. */
#define PDC_PCR_SCL (*(volatile uint16_t *)0x40053C4Cu)   /* PCRB3 (SCL) */
#define PDC_PCR_SDA (*(volatile uint16_t *)0x40053C50u)   /* PCRB4 (SDA) */
#define PDC_NOD     0x0004u

#define PORT_PWPR   (*(volatile uint16_t *)0x40053BFCu)   /* port write-protect (WE) */
#define PDC_POER    (*(volatile uint16_t *)0x40053816u)   /* POERB output-enable */
#define PDC_SDA_BIT 0x0010u                               /* PB4 */

static void pdc_sda_dir(int input)   /* stock 0xEB08 (in) / 0xEB50 (out) */
{
    /* EXACT 1:1 with stock. Stock's input(0xeb08) and output(0xeb50) configs are IDENTICAL
     * except w1 (the output-enable): w0=1, w2=4, w3=16 are the SAME in both - i.e. stock
     * only toggles the drive enable and KEEPS the pin's mode/drive/pull (w2=4) intact.
     * My old input used a plain digital-input (w0=0, w2=2, +pull) which DROPPED the w2=4
     * mode and added a wrong pull -> the slave's data after byte[0] was misread/aborted.
     *   input : {w0=1, w1=0, w2=4, w3=16}
     *   output: {w0=1, w1=2, w2=4, w3=16}  */
    /* THE READ-IS-ALWAYS-ZERO BUG: this project's gpio_config (hal/gpio.c) decides the pin
     * direction from cfg.dir (w2): dir==2 -> INPUT, anything else -> OUTPUT. The earlier
     * "1:1 with stock" rewrite set dir=4 for BOTH input and output (to mirror stock's raw
     * w2=4) - but stock's OWN gpio_config encodes direction in a DIFFERENT field (w1), so
     * copying its raw words made MY gpio_config keep SDA as OUTPUT in both cases. SDA was
     * never released, so every read returned the master-driven level (0), never the slave's
     * data. Fix: drive direction through cfg.dir the way THIS gpio_config reads it. */
    gpio_cfg_t cfg = {0};
    cfg.speed = 1;                /* OPEN-DRAIN (NOD) - prevents the STOP glitch on output-high */
    if (input) {
        cfg.dir = 2;              /* dir==2 => Pin_Mode_In (release SDA so the slave drives) */
        cfg.pull = 1;             /* internal pull-up: released line reads high reliably */
        cfg.w3 = 16;
    } else {
        cfg.mode = 1; cfg.w1 = 2; cfg.dir = 4; cfg.w3 = 16; cfg.pull = 1;  /* open-drain output */
    }
    gpio_config(PDCFG_PORT, PDCFG_SDA, &cfg);
}

static void pdc_start(void);
static void pdc_stop(void);
static int  pdc_write_byte(uint8_t v);

void pdc_init(void)
{
    /* ENABLE PIN (user's hunch, confirmed by a live PODR diff): stock drives PH2 HIGH
     * while my firmware never touched PortH. PH2 likely enables the per-port PD
     * telemetry chips / their level-shifter - without it reg 0x88 reads back 0. */
    { stc_port_init_t h; MEM_ZERO_STRUCT(h); h.enPinMode = Pin_Mode_Out; h.enPinDrv = Pin_Drv_H;
      PORT_Init(PortH, Pin02, &h); PORT_SetBits(PortH, Pin02); }

    gpio_cfg_t cfg = {0}; cfg.mode = 1; cfg.w1 = 2; cfg.dir = 4; cfg.w3 = 16; cfg.pull = 1; cfg.speed = 1; /* open-drain output + pull-up */
    gpio_config(PDCFG_PORT, PDCFG_SCL, &cfg);
    gpio_config(PDCFG_PORT, PDCFG_SDA, &cfg);
    /* OPEN-DRAIN (NOD) - correct I2C: master releases the line, an external pull-up
     * brings it high. (A live PIDRB test showed PB3/PB4 can't be pulled high here even
     * by a push-pull driver = a heavy load / dead pull-up rail on this bus; that is the
     * remaining electrical blocker for the telemetry READ - writes still work.) */
    PORT_PWPR = 0xA501u;
    PDC_PCR_SCL |= PDC_NOD;
    PDC_PCR_SDA |= PDC_NOD;
    PORT_PWPR = 0xA500u;
    gpio_set_pins(PDCFG_PORT, (uint16_t)(PDCFG_SCL | PDCFG_SDA));   /* release high */

    /* ROBUST I2C BUS RECOVERY: PIDRB proved a slave HOLDS SDA low at rest (hung chip
     * waiting for clocks). Release SDA, then pulse SCL (open-drain) up to 18 times until
     * SDA goes high; finish with a clean STOP so the freed slave is idle. */
    pdc_sda_dir(1);                                    /* release SDA (input) */
    for (int i = 0; i < 18; i++) {
        if (pdc_sda_rd()) break;                       /* SDA freed */
        gpio_clear_pins(PDCFG_PORT, PDCFG_SCL); udelay(10);
        gpio_set_pins(PDCFG_PORT, PDCFG_SCL);   udelay(10);
    }
    pdc_sda_dir(0);
    /* STOP: SDA low -> high while SCL high */
    gpio_clear_pins(PDCFG_PORT, PDCFG_SDA); udelay(10);
    gpio_set_pins(PDCFG_PORT, PDCFG_SCL);   udelay(10);
    gpio_set_pins(PDCFG_PORT, PDCFG_SDA);   udelay(10);

    /* NOTE: the I2C general-call SOFTWARE RESET (addr 0x00, cmd 0x06) that used to be here
     * was REMOVED. Stock never resets the PD chips. The SW3556 controllers run PD/sourcing
     * autonomously; a general-call reset wiped their negotiated PDO/source state (chips then
     * reported caps=0, volt=0 and never sourced Vbus). Bus recovery (above) is enough. */
    gpio_set_pins(PDCFG_PORT, (uint16_t)(PDCFG_SCL | PDCFG_SDA));   /* leave bus released-high */
}

static void pdc_start(void)          /* sub_175E8 */
{
    pdc_sda(1); udelay(4); pdc_scl(1); udelay(4);
    pdc_sda(0); udelay(4); pdc_scl(0);
}
static void pdc_stop(void)           /* sub_17618 */
{
    pdc_sda(0); pdc_scl(0); udelay(4);
    pdc_scl(1); udelay(4); pdc_sda(1); udelay(4);
}

static int pdc_write_byte(uint8_t v) /* sub_17784 + ack sub_1748C; 0=ACK,-1=NACK */
{
    for (int b = 8; b; --b) {
        pdc_scl(0); udelay(2);
        pdc_sda((v >> (b - 1)) & 1); udelay(2);
        pdc_scl(1); udelay(4);
    }
    /* ack */
    pdc_scl(0); pdc_sda(1); pdc_sda_dir(1); udelay(4);
    pdc_scl(1); udelay(6);
    int nack = pdc_sda_rd();
    if (!nack) { pdc_scl(0); udelay(4); pdc_sda_dir(0); }
    return nack ? -1 : 0;
}

static uint8_t pdc_read_byte(void)   /* sub_174E8 */
{
    uint8_t v = 0;
    pdc_sda_dir(1);
    pdc_sda(1);
    for (int b = 8; b; --b) {
        pdc_scl(0); udelay(4);
        pdc_scl(1); udelay(2);
        v = (uint8_t)((v << 1) | (pdc_sda_rd() ? 1 : 0));
        udelay(2);
    }
    pdc_sda_dir(0);
    return v;
}

static void pdc_mack(int nack)       /* master ACK(0)/NACK(1) - EXACT stock 0x175c0/0x17590 */
{
    /* THE BUG was: the old order set SDA *before* pulling SCL low; since read_byte ends
     * with SCL HIGH, that changed SDA while SCL high = a spurious START/STOP that aborted
     * the read. Stock: SCL(0),d4,SDA,SCL(1),d4,SCL(0). SDA is already an output from
     * read_byte's final sda_dir(0), so no direction switch here (1:1 with stock). */
    pdc_scl(0); udelay(4);
    pdc_sda(nack ? 1 : 0);
    pdc_scl(1); udelay(4);
    pdc_scl(0); udelay(4);
}

/* Probe every 7-bit address on the PDC bus (confirmed working). out[a]=1 = ACK. */
void pdc_scan(volatile uint8_t *out)
{
    extern void WDT_Feed(void);
    for (uint8_t a = 0; a < 0x80u; a++) {
        pdc_sda_dir(0);
        pdc_start();
        int ack = pdc_write_byte((uint8_t)(a << 1));
        pdc_stop();
        out[a] = (ack >= 0) ? 1u : 0u;
        WDT_Feed();   /* per-probe feed: a full scan must not starve the watchdog */
    }
}

/* TEMP DIAG: dump raw reg-0x88 packets from the candidate telemetry addresses plus a
 * register sweep of 0x70, into 0x1FFF8780, so we can see (over J-Link, with a load
 * plugged) which address/register actually carries the live V/I telemetry. */
/* selectable diag target address (7-bit, used with addr<<1) */
volatile uint8_t g_diag_addr = 0x70u;
void pdc_diag_dump(void)
{
    extern void WDT_Feed(void);
    volatile uint8_t *o = (volatile uint8_t *)0x1FFF8780u;
    static uint8_t seq = 0;
    /* INSTRUMENTED single read of 0x3d reg 0x88: capture the ACK at every step so we see
     * exactly where it fails. Layout: [0]=seq(pre) [1]=wa_ack [2]=reg_ack [3]=ra_ack
     * [4..9]=6 data bytes [10]=seq(post). If seq(pre)==seq(post) the snapshot is coherent
     * (not torn). ack: 0=ACK, 0xFF=NACK. */
    /* SEQ-GUARDED 0x78 read-back of reg 0x2B (fixed write 0x7F in pd_charger_init, no RMW).
     * o[0]=seq_pre [1]=ack [2]=reg0x2B_value [3]=ack0x2A [4]=reg0x2A(exp0x51) [15]=seq_post.
     * If o[2]==0x7F the 0x78 read works; if 0 it's broken. seq match => not torn. */
    o[0] = seq;
    { uint8_t v = 0; o[1] = (uint8_t)pd_reg_read8(0x2Bu, &v); o[2] = v; }
    { uint8_t v = 0; o[3] = (uint8_t)pd_reg_read8(0x2Au, &v); o[4] = v; }
    { uint8_t v = 0; o[5] = (uint8_t)pd_reg_read8(0x15u, &v); o[6] = v; }
    o[15] = seq; seq++;
    WDT_Feed();
}

/* Read n LE bytes from reg of controller addr7 on the PDC bus. */
int pd_port_read(uint8_t addr7, uint8_t reg, uint32_t *out, int n)
{
    uint32_t v = 0;
    pdc_sda_dir(0); pdc_start();
    if (pdc_write_byte((uint8_t)(addr7 << 1)) < 0)       goto fail;
    if (pdc_write_byte(reg) < 0)                         goto fail;
    pdc_start();
    if (pdc_write_byte((uint8_t)((addr7 << 1) | 1)) < 0) goto fail;
    for (int j = 0; j < n; j++) { v |= (uint32_t)pdc_read_byte() << (8 * j); pdc_mack(j < n - 1 ? 0 : 1); }
    pdc_stop(); *out = v; return 1;
fail:
    pdc_stop(); return 0;
}

/* Read n bytes from reg of controller addr7 on the PDC bus into a byte buffer
 * (stock port_io_xfer reads 6 bytes; pd_port_read tops out at 4). */
int pd_port_read_buf(uint8_t addr7, uint8_t reg, uint8_t *buf, int n)
{
    /* EXACT 1:1 with stock port_io_xfer (0x17314), confirmed by the IDA decompiler:
     *   gpio_17784(2 * v2);      write-addr = addr7<<1   (v2=0x3d -> 0x7a)
     *   gpio_17784(0x88);        register
     *   delayus(10); restart;
     *   gpio_17784(2 * v2 + 1);  read-addr  = (addr7<<1)|1 (0x7b)
     * Stock DOES shift the 7-bit address. (My earlier "raw byte" change was a regression.) */
    pdc_sda_dir(0); pdc_start();
    if (pdc_write_byte((uint8_t)(addr7 << 1)) < 0)       goto fail;   /* write-addr 0x7a */
    if (pdc_write_byte(reg) < 0)                         goto fail;   /* register 0x88 */
    udelay(10);   /* stock 0x1737a: delayus(10) so the chip latches reg 0x88 before read */
    pdc_start();
    if (pdc_write_byte((uint8_t)((addr7 << 1) | 1)) < 0) goto fail;   /* read-addr 0x7b */
    for (int j = 0; j < n; j++) { buf[j] = pdc_read_byte(); pdc_mack(j < n - 1 ? 0 : 1); }
    pdc_stop(); return 1;
fail:
    pdc_stop(); for (int j = 0; j < n; j++) buf[j] = 0; return 0;
}

int pd_reg_write8(uint8_t reg, uint8_t val)
{
    pdc_sda_dir(0);
    pdc_start();
    if (pdc_write_byte(PDCFG_ADDR_W) < 0) goto fail;
    if (pdc_write_byte(reg) < 0)          goto fail;
    if (pdc_write_byte(val) < 0)          goto fail;
    pdc_stop();
    return 1;
fail:
    pdc_stop();
    return 0;
}

int pd_reg_read8(uint8_t reg, uint8_t *val)
{
    /* EXACT 1:1 with stock fn_1740C (0x1740C): START, write(0x78), write(reg), delayus(4),
     * restart, write(0x79), read_byte, NACK, STOP. The old version dropped BOTH the
     * delayus(4) (chip needs it to latch the reg pointer) and the master NACK after the
     * read byte - so the RMW init reads returned garbage and mis-programmed the 0x78 PD
     * controller (the root cause of no telemetry). */
    pdc_sda_dir(0);
    pdc_start();
    if (pdc_write_byte(PDCFG_ADDR_W) < 0) goto fail;   /* 0x78 */
    if (pdc_write_byte(reg) < 0)          goto fail;
    udelay(4);                                         /* fn_1740C: delayus(4) - matches stock + Kopie */
    pdc_start();                                       /* REPEATED-START. This was accidentally dropped
                                                        * this session (when the latch delay was bumped),
                                                        * which broke EVERY 0x78 read - and worse, without
                                                        * the restart the read-address 0x79 is sent as a
                                                        * DATA byte written to the reg pointer, so each
                                                        * "read" silently WROTE garbage into the 0x78's
                                                        * config -> the chip got mis-programmed into its
                                                        * current NACK-everything state (needs a power-
                                                        * cycle to recover). The working SDK Kopie always
                                                        * had this restart - hence A1/A2 worked there. */
    if (pdc_write_byte(PDCFG_ADDR_R) < 0) goto fail;   /* 0x79 */
    *val = pdc_read_byte();
    pdc_mack(1);                                        /* master NACK (gpio_17590) */
    pdc_stop();
    return 1;
fail:
    pdc_stop();
    return 0;
}

void WDT_Feed(void)
{
    /* SWDT refresh via the DDL (writes the 0x0123 then 0x3210 key sequence). */
    wdt_feed();
}

int pd_read(uint8_t port, uint8_t reg, uint32_t *out)
{
    return i2c_read_reg32(port, reg, out);
}

/* soft_1A880 (0x1A880): CRC-8, poly 0x07, init 0, MSB-first, no reflection.
 * Validates the 5-byte PD-config packet in apply_port_cfg (byte[5] == crc(byte[0..4])). */
uint8_t pd_crc8(const uint8_t *buf, int len)
{
    uint8_t c = 0;
    for (int i = 0; i < len; i++) {
        c ^= buf[i];
        for (int j = 0; j < 8; j++)
            c = (c & 0x80u) ? (uint8_t)((c << 1) ^ 0x07u) : (uint8_t)(c << 1);
    }
    return c;
}

adc_readings_t g_adc;

/* fn_1A8B8 (0x1A8B8): read a 15-bit ADC channel over the 0x78 soft-I2C bus.
 * reg 64 = config (low nibble = channel select); reg 66 = hi byte, 65 = lo byte.
 * Returns 0 if the device NACKs (HW absent). */
int adc_read_channel(uint8_t ch)
{
    uint8_t cfg = 0, hi = 0, lo = 0;
    pd_reg_read8(64, &cfg);                       /* fn_1740C: read cfg into 611 */
    cfg = (uint8_t)((cfg & 0xF0u) | (ch & 0x0Fu));
    pd_reg_write8(64, cfg);                       /* fn_176FC: select channel    */
    pd_reg_read8(66, &hi);                        /* high byte                   */
    pd_reg_read8(65, &lo);                        /* low byte                    */
    return (int)((((uint16_t)hi << 8) & 0x7FFFu) | lo);
}

/* update_readings (0x198D8): refresh the main-rail V/I from ADC ch1/ch2/ch5.
 * fn_82F8(x,100) is an integer divide; scales match stock exactly. The per-rail
 * debounce counters / unk_1FFF840A status bitmap are tracked in the RE map (memory)
 * and omitted here (purely connection-state bookkeeping, all HW-gated). */
void update_readings(void)
{
    uint32_t v0 = (uint32_t)adc_read_channel(1);   /* ch1 = USB-A port3 current sense */
    uint32_t v1 = (uint32_t)adc_read_channel(2);   /* ch2 = USB-A port4 current sense */
    uint32_t v2 = (uint32_t)adc_read_channel(5);   /* ch5 = USB-A shared rail voltage */
    { extern volatile uint32_t g_adc_raw[3]; g_adc_raw[0]=v0; g_adc_raw[1]=v1; g_adc_raw[2]=v2; }  /* DIAG */
    g_adc.v1_x100 = (uint16_t)(((5u * v0) >> 1) / 100u);
    g_adc.v2_x100 = (uint16_t)(((5u * v1) >> 1) / 100u);
    g_adc.i_x100  = (uint16_t)((6u * v2) / 100u);

    /* 1:1 with stock update_readings (0x198D8): ports 3 & 4 are the two USB-A ports, whose
     * V/I come from the ADC (NOT the PDC per-port packets). Both share the rail voltage
     * (6*v2)/100; port3 current = ((5*ch1)>>1)/100, port4 current = ((5*ch2)>>1)/100. This
     * was the MISSING telemetry: apply_port_cfg only fills ports 0/1/2 (USB-C), so a load on
     * a USB-A port showed nothing. (g_port_data[80]/[92] = port3 V/I, [104]/[116] = port4.) */
    g_outlet[3].volt_raw = (6u * v2) / 100u;
    g_outlet[3].cur_raw  = ((5u * v0) >> 1) / 100u;
    g_outlet[4].volt_raw = (6u * v2) / 100u;
    g_outlet[4].cur_raw  = ((5u * v1) >> 1) / 100u;
    /* attach/detach debounce (stock thresholds: >0x78 attach, <0x0C detach, dead-zone between) */
    if (v0 > 0x78u)      { if (++g_outlet[3].attach_cnt > 100u) g_outlet[3].attach_cnt = 100u; g_outlet[3].detach_cnt = 0u; }
    else if (v0 < 0x0Cu) { if (++g_outlet[3].detach_cnt > 100u) g_outlet[3].detach_cnt = 100u; g_outlet[3].attach_cnt = 0u; }
    if (v1 > 0x78u)      { if (++g_outlet[4].attach_cnt > 100u) g_outlet[4].attach_cnt = 100u; g_outlet[4].detach_cnt = 0u; }
    else if (v1 < 0x0Cu) { if (++g_outlet[4].detach_cnt > 100u) g_outlet[4].detach_cnt = 100u; g_outlet[4].attach_cnt = 0u; }
    if (g_outlet[3].attach_cnt >= 100u) g_outlet_status |=  (uint16_t)(1u << 3);
    if (g_outlet[3].detach_cnt >= 100u) g_outlet_status &= (uint16_t)~(1u << 3);
    if (g_outlet[4].attach_cnt >= 100u) g_outlet_status |=  (uint16_t)(1u << 4);
    if (g_outlet[4].detach_cnt >= 100u) g_outlet_status &= (uint16_t)~(1u << 4);
}

/* Write one byte to reg of controller addr7 on the PDC bus (stock port_dev_read). */
int pd_port_write(uint8_t addr7, uint8_t reg, uint8_t val)
{
    pdc_sda_dir(0); pdc_start();
    if (pdc_write_byte((uint8_t)(addr7 << 1)) < 0) goto fail;
    if (pdc_write_byte(reg) < 0)                   goto fail;
    if (pdc_write_byte(val) < 0)                   goto fail;
    pdc_stop(); return 1;
fail:
    pdc_stop(); return 0;
}

/* Over-TEMPERATURE protection FSM - stock power_temp_task head (0x10FC2..0x11072), now 1:1.
 * Stock latches on the two NTC temps g_menu_page[112]/[116] (= flt_1FFF8304/8308, the case
 * temps) with 100/95 C hysteresis - NOT on power. The earlier code latched on g_power_w (watts),
 * a backend divergence. unk_1FFF840F = tempA latch; word_2000F620 = tempB latch -> the over-temp
 * UI indicator (ui_DB38 / ui_show_temp_ok). Does NOT cut charging. temp_update() must run BEFORE
 * this each tick (stock order: power_calc writes the temps, then power_temp_task tests them). */
float   g_power_w;                    /* total apparent power (watts), set by power_calc */
uint8_t g_hipwr1, g_hipwr2;          /* unk_1FFF840F / byte_2000F620[0] over-temp latches */
volatile uint8_t g_port_capw[6];     /* current per-port max (W): C1,C2,C3,C4,A1,A2 (0 = no SW cap)
                                      * - updated by pd_maintain_limits, shown on the debug page */

void ui_DB38(void);                /* defined below (thermal recover / step-down) */
void ui_show_temp_ok(void);

void pd_power_level_check(void)
{
    extern float g_temp_sensor[2];
    float tA = g_temp_sensor[1];                 /* stock g_menu_page[112] */
    float tB = g_temp_sensor[0];                 /* stock g_menu_page[116] */
    if (g_hipwr1 || tA < 100.0f) {               /* tempA latch, 100/95 C hysteresis */
        if (g_hipwr1 && tA < 95.0f) g_hipwr1 = 0;
    } else {
        g_hipwr1 = 1;
    }
    if (g_hipwr2 || tB < 100.0f) {               /* tempB: recover -> ui_DB38() (stock 1:1) */
        if (g_hipwr2 && tB < 95.0f) ui_DB38();
    } else {                                     /* trip -> ui_show_temp_ok() steps the 240W limit down */
        ui_show_temp_ok();
    }
}

/* 240W THERMAL THROTTLE (stock read_port_status 0x133e4 / ui_DB38 0xdb38 / ui_show_temp_ok 0xdb58) - 1:1.
 * Writes the C1 power limit to reg 3 of the 0x16 chip (the SAFE path - NOT reg 0xA0). Dormant unless a
 * case temp exceeds 100 C, then steps the 240W cap down 240->140->100 W; restores on cooldown. */
static uint8_t  s_thr_done;          /* byte_2000F3DB[0]  */
static uint8_t  s_thr_cls;           /* byte_20013624[0]: 0=240W 1=140W 2=100W 3=other */
static uint32_t s_thr_lim;           /* unk_2001362C: current limit (mW) */
static uint8_t  s_thr_step;          /* byte_20013628[0] */
extern int i2c_write_reg32(uint8_t port, uint8_t reg, uint32_t value);
extern void ui_draw_icon(void);      /* over-temp icon (ui.c) */

void read_port_status(uint32_t lim_mW)   /* stock 0x133e4: set C1 limit + classify */
{
    /* Gated by the done-latch ONLY, exactly like stock (byte_2000F3DB[0]): write reg 3 once per
     * event, never per loop. The latch is cleared on real events (boot, fresh C1 attach, thermal
     * step, Individual-mode set) - so this writes reg 3 = lim_mW exactly when the 0x16 actually needs
     * its budget (re)asserted, and is silent otherwise. (An earlier s_last_lim "unchanged -> skip"
     * guard BROKE the fresh-attach re-assert: after a detach the 0x16 drops its 240W budget, so it
     * MUST be re-written with the SAME 240000 value - skipping it left C1 stuck at the 5V default.) */
    if (s_thr_done) return;
    if (i2c_write_reg32(0u, 3u, lim_mW)) {       /* reg 3 of 0x16 = power budget (mW); 240000 => 20V/240W */
        uint8_t cls;
        if      (lim_mW == 100000u) cls = 2;
        else if (lim_mW == 140000u) cls = 1;
        else if (lim_mW == 240000u) cls = 0;
        else { cls = 3; if (lim_mW >= 100001u) g_hipwr2 = 0; }
        s_thr_lim = lim_mW; s_thr_cls = cls; s_thr_done = 1;
    }
}

/* pd_c1_budget_maintain - stock ui_draw_icon (0x16f0c), the C1 power-BUDGET manager (NOT the cosmetic
 * over-temp icon - that was a misidentification). Stock calls this from its main/UI loop: while the
 * done-latch is clear it (re)writes the 0x16's reg 3 budget so the chip advertises the full 20V/240W
 * PDO set, then read_port_status sets the latch (write-once-per-event). Default = 240000 mW (240W);
 * over-temp throttle uses 140000. THIS is what makes C1 negotiate 20V on a fresh attach: without a
 * reg 3 budget the 0x16 only offers the 5V Type-C default. Call every loop - it is idempotent (the
 * latch makes it a no-op once written) and only re-fires when power_poll clears the latch on a fresh
 * C1 attach (stock poll_active_port_status 0x181a0: byte_2000F3DB[0]=0 on the reg1==0 edge). */
void pd_c1_budget_maintain(void)
{
    extern uint8_t g_charge_enabled;
    if (!g_charge_enabled) return;
    read_port_status(g_hipwr2 ? 140000u : 240000u);   /* stock: over-temp ? 140W : 240W default */
}

/* fn_133C8 (0x133c8): set the C1 (240W) power limit in Individual mode - clear the throttle-done
 * latch, then write the limit (mW) to reg 3 of the 0x16 chip via read_port_status. */
void pd_apply_c1_limit(uint32_t mW)
{
    s_thr_done = 0;
    read_port_status(mW);
}

void ui_DB38(void)                        /* stock 0xdb38: recover - clear + icon */
{
    s_thr_done = 0;
    ui_draw_icon();
    g_hipwr2 = 0;                          /* word_2000F620 = 0 */
}

void ui_show_temp_ok(void)                /* stock 0xdb58: step the 240W limit DOWN */
{
    s_thr_done = 0;
    if (s_thr_cls == 0u) { s_thr_step = 1; read_port_status(140000u); g_hipwr2 = 1; return; }
    if (s_thr_cls == 1u) { s_thr_step = 2; read_port_status(100000u); g_hipwr2 = 1; return; }
    if (s_thr_cls != 3u) return;          /* cls==2: already at the 100W floor */
    if (s_thr_lim >= 140001u)             { s_thr_step = 1; read_port_status(140000u); g_hipwr2 = 1; }
    else if (s_thr_lim >= 100001u)        { s_thr_step = 2; read_port_status(100000u); g_hipwr2 = 1; }
}

/* Per-port V/I limits from WLAN SET_CONFIG (stock flt_1FFF809C..80D8, set by
 * config items id 5..0x10 = bswap32(value)/100). Consumed by the PD clamp (sub_DA78). */
float g_port_limit[12];

/* Per-outlet state record (stock byte_1FFF8390, 24-byte stride x 6 outlets). */
outlet_state_t g_outlet[6];
uint16_t       g_outlet_status;     /* stock unk_1FFF840A: per-outlet connected bitmap */

/* sub_13240 (0x13240): apparent power for one outlet = (field[+8]/10)*(field[+20]/10),
 * returns 0 if >500, clamps to 140. Operates on the same 24-byte record offsets. */
int pd_outlet_power(uint8_t ch)
{
    if (ch > 5) return 0;
    float p = ((float)g_outlet[ch].volt_raw / 10.0f) * ((float)g_outlet[ch].cur_raw / 10.0f);
    if (p > 500.0f) return 0;
    if (p > 140.0f) p = 140.0f;
    return (int)p;
}

/* Charging power class (stock byte_20013628, set by the charging-state classifier). */
uint8_t g_charge_class;

/* sub_133E4 classifier: a charge power limit (uW) maps to a class. The stock values
 * are immediates 0x3A980=240000, 0x222E0=140000, 0x186A0=100000 (240/140/100 W). The
 * full negotiation (i2c write of the limit to port reg 3 + when to pick which) is
 * hardware-interactive (ui_draw_icon/ui_show_temp_ok); this is the pure mapping. */
uint8_t pd_classify_limit(uint32_t limit_uw)
{
    if (limit_uw == 240000u) return 0;
    if (limit_uw == 140000u) return 1;
    if (limit_uw == 100000u) return 2;
    return 3;
}

/* sub_DA78 (0xDA78): apparent-power cap (W) by charging class: 0->240, 1->140, else 100.
 * power_calc clamps each outlet's V*I to this. (NB: NOT a g_port_limit consumer.) */
int pd_power_cap(void)
{
    if (g_charge_class == 0) return 240;
    if (g_charge_class == 1) return 140;
    return 100;
}

/* fn_17CF0 (0x17CF0): count of attached outlets (outlets 0..4 with attach_cnt != 0). */
uint8_t pd_attached_count(void)
{
    uint8_t n = 0;
    for (int i = 0; i < 5; i++) if (g_outlet[i].attach_cnt) n++;
    return n;
}

/* power_calc core (0x1098C): fan each outlet's voltage/current/apparent-power out to
 * the page display fields (stock unk_1FFF82BC.. -> our g_telemetry) and sum the
 * apparent powers into g_power_w (= flt_1FFF8304/8308). Stock reads volt at +8 and
 * current at +20 and scales /10 (V/A); v_x100/i_x100 are *100, so *10 from raw. */
extern uint8_t g_page_group;            /* byte_1FFF830E (fw_main.c) */

void pd_power_calc(void)
{
    int sum = 0;
    int cap = pd_power_cap();          /* sub_DA78: clamp outlet apparent power */
    for (int i = 0; i < 6; i++) {
        g_telemetry.v_x100[i] = (uint16_t)(g_outlet[i].volt_raw * 10u);  /* (raw/10)*100 */
        g_telemetry.i_x100[i] = (uint16_t)(g_outlet[i].cur_raw  * 10u);
        int w = pd_outlet_power((uint8_t)i);
        if (w > cap) w = cap;
        g_telemetry.p_x100[i] = (uint16_t)(w * 100);
        sum += w;
    }
    /* C1 = the 0x16 master (NOT in g_outlet), so the loop above missed it and the home ring/total
     * read 0 W while C1 was charging. Add C1's V*I (g_total_power packs V in mV low / I in mA high). */
    {
        float c1 = (float)(g_total_power & 0xFFFFu) / 1000.0f
                 * (float)(g_total_power >> 16)      / 1000.0f;
        if (c1 > 0.0f && c1 < 300.0f) sum += (int)(c1 + 0.5f);
    }
    g_power_w = (float)sum;

    /* Mirror per-port telemetry as floats into the stock UI-struct per-port records
     * so device RAM is byte-comparable with stock (stock writer @0x10918: record at
     * 0x1FFF8294+0x18 + i*0x10, stride 0x10; V at +0x0, I at +0x4, W=V*I at +0x8).
     * Values in natural units (V / A / W) derived from my x100 telemetry. */
    for (int i = 0; i < 5; i++) {
        volatile float *rec = (volatile float *)(0x1FFF82ACu + (uint32_t)i * 0x10u);
        float vv = (float)g_telemetry.v_x100[i] / 100.0f;
        float ii = (float)g_telemetry.i_x100[i] / 100.0f;
        rec[0] = vv;            /* +0x0  per-port voltage (V)  */
        rec[1] = ii;            /* +0x4  per-port current (A)  */
        rec[2] = vv * ii;       /* +0x8  per-port power (W)    */
    }

    /* Page group by total watts (stock power_calc 0x10D5E): >=13W->0, 6..12->1, <=5->2. */
    if      (sum >= 13) g_page_group = 0;
    else if (sum >= 6)  g_page_group = 1;
    else                g_page_group = 2;
}

/* Channel power switching (stock i2c_1A5F4 0x1A5F4): write reg 0xA0 = on/off to the
 * per-channel controller (61+ch); stock issues it twice. */
void pd_channel_switch(uint8_t ch, uint8_t on)
{
    /* Use the addresses the controllers actually ACK (0x70/0x71/0x74) - NOT the old
     * 0x3d/0x3e/0x3f, which never responded, so the per-channel power-enable (reg
     * 0xA0) never reached the chip => no USB voltage. Same table as port_io_xfer. */
    static const uint8_t pdc_addr[3] = { 0x3du, 0x3eu, 0x3fu };  /* STOCK addrs (0x17648/0x17314):
        proven correct - stock reads real telemetry (5.01V/1A) from these on THIS unit. My
        earlier scan that "found" 0x70/0x71/0x74 was unreliable ACK noise (the same wire byte
        that pd_reg_write8 uses successfully showed no scan ACK). */
    uint8_t addr = pdc_addr[ch <= 2 ? ch : 0];
    pd_port_write(addr, 0xA0, on);
    pd_port_write(addr, 0xA0, on);
}

/* ---- demand-based SMART distribution (real, replaces the flat 140-to-all) -------------------
 * The 280 W supply must never be over-drawn. Only the three SW3566 C-ports (C2/C3/C4) have a
 * software lever (reg 0xA0 ~ per-port watt cap, 15..140). C1 (the 240 W 0x16 master) and the two
 * USB-A ports have no per-port reg here, so they are ACCOUNTED (their live draw is subtracted from
 * the budget, plus a small ramp reserve for C1) and the remainder is split across the active C-ports
 * proportional to their live demand. Plenty of budget -> full 140; contention -> proportional share.
 * (Stock's exact split is an unrecoverable jump-table fn_1A698; this is a clean reimplementation.) */
#define SMART_BUDGET_W  280      /* total power-supply ceiling (W)                 */
#define SMART_C_MAX     140      /* SW3566 EPR per-port max (reg 0xA0 ~ watts@20V) */
#define SMART_MIN_W     15       /* keep an attached port chargeable               */
#define SMART_HEADROOM  20       /* let a ramping device grow each cycle           */
#define SMART_C1_RESV   30       /* headroom reserved for C1 to ramp w/o overshoot */

static void pd_smart_alloc(uint8_t out[3])
{
    extern uint8_t g_port_enable[];
    const menu_state_t *m = &g_menu_state;

    /* budget left for C2/C3/C4 after the uncontrolled ports' live draw */
    float used = 0.0f;
    if (m->outlet[0].attach) used += m->outlet[0].power + (float)SMART_C1_RESV;   /* C1 + ramp */
    used += m->outlet[4].power + m->outlet[5].power;                              /* A1 + A2   */
    int avail = SMART_BUDGET_W - (int)(used + 0.5f);
    if (avail < 0) avail = 0;

    /* per-C-port demand = live draw + headroom (so a ramping device can grow) */
    int demand[3], sumd = 0;
    for (int i = 0; i < 3; i++) {
        const menu_outlet_t *o = &m->outlet[i + 1];           /* outlet[1..3] = C2/C3/C4 */
        if (g_port_enable[i + 1] && o->attach) {
            int d = (int)(o->power + 0.5f) + SMART_HEADROOM;
            if (d < SMART_MIN_W) d = SMART_MIN_W;
            if (d > SMART_C_MAX) d = SMART_C_MAX;
            demand[i] = d; sumd += d;
        } else {
            demand[i] = 0;
        }
    }

    for (int i = 0; i < 3; i++) {
        int a;
        if (demand[i] == 0)        a = SMART_C_MAX;            /* idle: full, ready for instant attach */
        else if (sumd <= avail)    a = SMART_C_MAX;            /* plenty: no cap        */
        else {                                                /* contention: share     */
            a = (int)((long)demand[i] * avail / sumd);
            if (a < SMART_MIN_W) a = SMART_MIN_W;
            if (a > SMART_C_MAX) a = SMART_C_MAX;
        }
        out[i] = (uint8_t)a;
    }
}

/* pd_maintain_limits: CONTINUOUSLY refresh each C-channel's reg 0xA0 (current limit). SWD sweep
 * proved that holding a non-zero reg 0xA0 keeps the SW3556/3566 at the negotiated 20V rock-stable,
 * while a single one-shot write let the port drift/trip with no recovery. reg 0xA0 is the per-port
 * CURRENT limit (the sweep showed any value 45..250 still negotiates the device-requested 20V; 0=off),
 * so this also enforces the per-mode watt budget. Called every main-loop iteration (self rate-limited
 * to ~10 Hz). off port -> reg 0xA0=1 (kill); on port -> the mode's current-limit value. */
void pd_maintain_limits(void)
{
    extern uint8_t g_port_enable[];
    extern float   g_port_maxw[];
    uint8_t mode = *(volatile uint8_t *)0x1FFF8159u;     /* g_status_mode (ui_state.h RAM macro) */
    static uint32_t s_t;
    if ((uint32_t)(get_tick_ms() - s_t) < 100u) return;
    s_t = get_tick_ms();
    static const uint8_t rage[3] = { 140u, 15u, 15u };   /* Raserei preset (stock byte_1FFF8390[2/26/50]) */
    /* Smart: real demand-based distribution. Re-evaluated only every ~1.5 s with an 8 W deadband so
     * the result is STABLE - recomputing the cap every 100 ms (it tracks live watt jitter) would
     * write reg 0xA0 constantly and glitch the shared bit-bang bus -> devices detach (see below). */
    static uint8_t smart[3] = { SMART_C_MAX, SMART_C_MAX, SMART_C_MAX };
    static uint32_t s_smart_t;
    if (mode == 1u && (uint32_t)(get_tick_ms() - s_smart_t) >= 1500u) {
        s_smart_t = get_tick_ms();
        uint8_t ns[3]; pd_smart_alloc(ns);
        for (int i = 0; i < 3; i++) {
            int d = (int)ns[i] - (int)smart[i]; if (d < 0) d = -d;
            if (d >= 8 || ns[i] == SMART_C_MAX || smart[i] == SMART_C_MAX) smart[i] = ns[i];
        }
    }
    /* EDGE-TRIGGERED, exactly like stock sync_channel_state: only push reg 0xA0 when the target value
     * CHANGES. Stock writes reg 0xA0 ONCE on a mode/enable change and then leaves it (a0trace measured
     * 0 writes in 12 s of steady state). Continuously re-writing reg 0xA0 here was CORRUPTING the
     * shared bit-bang PDC bus: a reg-0xA0 write interleaved with a telemetry read (reg 0x88) and the
     * read came back as the write bytes [0xA0,0xA0,..] -> all telemetry decoded to 0 and the port
     * never settled. Write-on-change removes the contention; the chip holds 20 V on its own. */
    /* EDGE-TRIGGERED (write reg 0xA0 only on change), like stock. Re-asserting it every 100 ms made
     * the bit-bang port_write glitch the port -> the device kept DETACHING (att bounced 0..60) ->
     * 0/5/20 V oscillation. Stock writes reg 0xA0 once and the device stays attached (att=100). */
    static uint8_t s_last[3] = { 0xFFu, 0xFFu, 0xFFu };
    for (uint8_t ch = 0; ch < 3u; ch++) {
        uint8_t v;
        if (!g_port_enable[ch + 1]) {
            v = 1u;                                       /* off -> kill */
        } else if (mode == 0u) {
            v = rage[ch];
        } else if (mode == 1u) {
            v = smart[ch];
        } else if (mode == 2u) {
            float w = g_port_maxw[ch + 1];
            v = (w > 0.5f && w < 250.0f) ? (uint8_t)w : 140u;
        } else {
            v = 140u;
        }
        g_port_capw[ch + 1] = v;                          /* current per-port cap (C2/C3/C4) */
        if (v != s_last[ch]) { s_last[ch] = v; pd_channel_switch(ch, v); }
    }
    g_port_capw[0] = g_port_enable[0] ? 240u : 0u;        /* C1 (0x16 master) nominal max */
    g_port_capw[4] = g_port_capw[5] = 0u;                 /* A1/A2: no SW cap -> shown as "--" */
}

/* pd_set_port_flag (stock i2c_19DD4 0x19DD4) - 1:1. Sets g_port_flag_8415 (byte_1FFF8415); if enabled,
 * the port isn't already in the channel mask, and its applied flag changed, pushes the per-port flag
 * (reg 0xA0 via i2c_1A5F4). reg 0xA0 is HW-confirmed to kill sourcing on THIS unit, so this is only
 * ever invoked from the gated over-power/Individual path - the function matches stock, the FSM decides
 * whether to call it. */
uint8_t g_port_flag_8415;            /* stock byte_1FFF8415 */
void pd_set_port_flag(uint8_t ch, uint8_t val, uint8_t en)
{
    if (g_port_flag_8415 != en) g_port_flag_8415 = en;
    if (ch < 6u && en && !((g_channel_mask >> ch) & 1u) && g_outlet[ch].ch_applied != val) {
        g_outlet[ch].ch_applied = val;
        pd_channel_switch(ch, val);  /* i2c_1A5F4(ch, flag) -> reg 0xA0 */
    }
}

/* stock Write_0x78_22to4and17to2 (0x19864): hi-power channel config (mask bit 3/4 set). */
void pd_channel_cfg_hi(void)
{
    pd_reg_write8(22, 4);
    pd_reg_write8(17, 2);
}

/* stock Write_0x78_17to0and22to0 (0x19890): the NORMAL (low) channel config, applied when any
 * channel is enabled via mask bits 0-2. sync_channel_state issues this (the else of the hi case)
 * whenever the mask changes - my code was missing it, so reg 0xA0=1 did not fully enable. */
void pd_channel_cfg_lo(void)
{
    pd_reg_write8(17, 0);
    pd_reg_write8(22, 0);
}

/* channel-control shared state (stock byte_1FFF8413 / unk_1FFF8414 / byte_2000F3C2[6]
 * / byte_2000F3DB[1]). g_channel_mask is set by the WLAN set-config path. */
uint8_t  g_channel_mask;            /* byte_1FFF8413: desired channel on/off bits   */
uint8_t  g_channel_last;            /* unk_1FFF8414: last-applied mask / settle flag */
uint8_t  g_channels_ready;          /* byte_2000F3DB[1]: settle latch                */
static uint8_t s_settle_tick;       /* byte_2000F3C2[6]                              */

/* sync_channel_state (0x19E2C): on a channel-mask change, drive each channel on/off
 * via i2c_1A5F4 and run the hi-channel config. 1:1 (a1..a3 unused by callers). */
void sync_channel_state(void)
{
    /* NOTE: do NOT early-return on mask==0 - going back to all-on (mask 0x..->0) must still apply
     * so the previously-disabled channels get reg 0xA0=0 written (re-enable). Only skip when
     * unchanged. (This early-return was why a re-enabled port "never came back on".) */
    if (g_channel_last == g_channel_mask) return;
    g_channel_last = g_channel_mask;
    /* NOTE: the per-C-channel reg 0xA0 (on/off + limit) is now owned EXCLUSIVELY by
     * pd_maintain_limits() (continuous refresh). Having sync_channel_state ALSO write reg 0xA0 here
     * (the on/off 0/1) fought the maintainer's limit value and oscillated the port -> trip. So this
     * only handles the 0x78 USB-A channel-routing config now. */
    /* STOCK sync_channel_state always issues the channel config on a mask change: HI when a
     * 240W channel bit (3/4) is set, else the LOW/normal config. The missing LOW branch is why
     * enabling a USB-C port (reg 0xA0=1) did not source - the 0x78 controller stayed unconfigured. */
    if (g_channel_mask & 0x18u) pd_channel_cfg_hi();
    else                        pd_channel_cfg_lo();
}

/* fn_18234 (0x18234): once all 6 outlets' debounce counters have settled (==100),
 * after ~20 ticks latch "channels ready" and force a re-sync (g_channel_last=1). */
void pd_settle_tick(void)
{
    if (g_channels_ready) return;
    for (int i = 0; i < 6; i++)
        if (g_outlet[i].attach_cnt != 100 && g_outlet[i].detach_cnt != 100) return;
    if (s_settle_tick >= 20) { g_channel_last = 1; g_channels_ready = 1; }
    s_settle_tick++;
}

/* port_io_xfer (0x17314): read the 6-byte PD-status packet for outlet `ch` from
 * the per-channel controller (addr 61/62/63) reg 0x88 on the PDC bus. */
void port_io_xfer(uint8_t ch, uint8_t *pkt6)
{
    /* 1:1 with stock: ch0->0x3d, ch1->0x3e, ch2->0x3f (7-bit). pd_port_read_buf shifts
     * <<1 -> write-addr 0x7a/0x7c/0x7e, read-addr 0x7b/0x7d/0x7f (exactly stock's 2*v2). */
    static const uint8_t pdc_addr[3] = { 0x3du, 0x3eu, 0x3fu };
    pd_port_read_buf(pdc_addr[ch <= 2 ? ch : 0], 0x88u, pkt6, 6);
}

/* apply_port_cfg (0x19A24): read+validate the PD packet and decode caps/V/I plus
 * the attach/detach debounce into g_outlet[ch] / g_outlet_status. 1:1 from stock. */
unsigned apply_port_cfg(uint8_t ch)
{
    uint8_t b[6];
    if (ch > 5) return 0;
    port_io_xfer(ch, b);
    /* RAW packet capture (before CRC) via the clean loop read path - to see the ACTUAL
     * bytes the chip sends. Stock reads e.g. `81 00 32 31 00 2e` (attach,5V). If mine reads
     * `00 00 00 00 00 00` here, the chip sends zeros to me (state issue), not a bit-bang bug. */
    if (ch < 3) { extern volatile uint8_t g_last_pkt[3][6];
                  for (int qq = 0; qq < 6; qq++) g_last_pkt[ch][qq] = b[qq]; }
    /* Diagnostic counters (read over J-Link to confirm the PDC i2c read works at
     * protocol level - the on-board PD controllers ACK even with no load plugged). */
    extern volatile uint32_t g_pdc_crc_ok, g_pdc_crc_fail, g_port_crc_ok[6];
    if (pd_crc8(b, 5) != b[5]) { g_pdc_crc_fail++; return 0; }  /* byte[5]==crc(byte[0..4]) */
    g_pdc_crc_ok++;
    g_port_crc_ok[ch]++;                                  /* per-port: which ports respond */

    outlet_state_t *o = &g_outlet[ch];
    int attach = (b[0] >> 7) & 1;                       /* v3 */
    o->cur_raw  = (uint32_t)(b[0] & 0x3F);              /* [+20] current */
    o->f4       = (uint8_t)(b[1] >> 4);                 /* [+4]  */
    o->aux12    = (uint32_t)(((b[1] & 3) << 8) | b[2]); /* [+12] */
    o->volt_raw = (uint32_t)(((b[4] & 3) << 8) | b[3]); /* [+8]  voltage */

    if (attach) { if (++o->attach_cnt > 100) o->attach_cnt = 100; o->detach_cnt = 0; }
    else        { if (++o->detach_cnt > 100) o->detach_cnt = 100; o->attach_cnt = 0; }
    if (o->attach_cnt >= 100) g_outlet_status |=  (uint16_t)(1u << ch);
    if (o->detach_cnt >= 100) g_outlet_status &= (uint16_t)~(1u << ch);
    return o->detach_cnt;
}

/* pd_read_n: retry a C1/PD register read a few times. The bit-bang 0x16 occasionally NACKs a read
 * (it is busy servicing a PD event, or a read lands right after a write). A SINGLE transient NACK
 * must not blank the C1 telemetry, so retry before giving up. This is the core fix for "C1 sometimes
 * reads nothing / data disappears from the debug + menus". */
static int pd_read_n(uint8_t port, uint8_t reg, uint32_t *out)
{
    for (int t = 0; t < 3; t++)
        if (pd_read(port, reg, out)) return 1;
    return 0;
}

int power_poll(uint8_t port, pd_status_t *st)
{
    uint32_t conn, link, power;

    st->valid = 0;
    st->attached = st->conn_b1 = st->power_valid = 0;
    st->power = 0;

    if (!pd_read_n(port, PD_REG_CONN, &conn)) {
        /* Transient read failure (NACK x3): do NOT zero g_total_power - keep the last good value so
         * the debug/home menus don't blank C1 on a momentary bus hiccup; the next loop re-reads it. */
        return 0;
    }
    st->attached    = (conn & 1) != 0;
    st->conn_b1     = (conn & 2) != 0;
    st->power_valid = (conn & 4) != 0;

    if (!pd_read_n(port, PD_REG_LINK, &link))
        return 0;

    /* C1 (0x16) re-enable + budget re-assert, faithful to stock poll_active_port_status (0x181a0):
     * ONLY when the master reads DISABLED (reg1 & 1 == 0) do we re-write reg1=1 and clear the reg3-done
     * latch (so pd_c1_budget_maintain re-asserts the 240W budget). This replaces a blind pre-poll reg1
     * write that issued a write immediately before EVERY read - which made the 0x16 intermittently NACK
     * the read (the "C1 data disappears" bug). The 0x16 holds its contract autonomously (SWD-verified
     * across a 3s CPU halt), so no fast keepalive is needed; over-writing reg1 only adds bus traffic. */
    if (port == 0u) {
        extern uint8_t g_charge_enabled;
        static uint8_t s_c1_att_last;
        if (g_charge_enabled && (link & 1u) == 0u) {
            i2c_write_reg32(0u, PD_REG_LINK, 1u);
            s_thr_done = 0;                 /* disabled->enabled edge: re-assert reg3=240000 */
        }
        /* Also clear the latch on a fresh sink attach (reg2 bit0 0->1): the 0x16 can lose its budget on
         * a replug while reg1 stays enabled, so the reg1==0 trigger above would miss it. */
        if (st->attached && !s_c1_att_last) s_thr_done = 0;
        s_c1_att_last = st->attached;
    }

    /* C1 (port 0) display value uses LAST-GOOD hold + confirmed-detach debounce: the 0x16 occasionally
     * reports not-attached / power-invalid for a single poll while it is still sourcing, which made C1
     * flicker to 0V/0A. Only a VALID NON-ZERO read becomes the new value; a transient bad read keeps the
     * last value; and 0 is shown only after several consecutive not-attached reads (a real unplug). */
    static uint32_t s_c1_last;          /* last good C1 packed V/I */
    static uint8_t  s_c1_det;           /* consecutive not-attached count */

    if (!st->attached) {
        if (port == 0u) {
            if (++s_c1_det >= 5u) {     /* real unplug -> now show 0 */
                s_c1_det = 5u;
                s_c1_last = 0;
                g_total_power = 0;
            }
            st->power = s_c1_last;      /* transient flap -> keep last good */
        } else {
            g_total_power = 0;
        }
        st->valid = 1;
        return 1;
    }

    if (!pd_read_n(port, PD_REG_POWER, &power))
        return 0;                       /* keep last good (g_total_power untouched) */

    /* reg 0x21 PACKS two values (stock power_calc 0x1098c + fn_180A0 0x180A0): LOW word = voltage
     * in mV (LOWORD/1000 = V), HIGH word = current in mA (fn_180A0 returns HIWORD; /1000 = A). Keep
     * the FULL 32 bits - masking to 16 (an earlier mistake) threw away the current. e.g. 0xD6138D
     * = 5.005 V (0x138D) + 0.214 A (0x00D6); C1 power = V*I (~1.07 W). */
    if (port == 0u) {
        s_c1_det = 0;                   /* attached -> reset the unplug debounce */
        if (st->power_valid && (power & 0xFFFFu) != 0u)
            s_c1_last = power;          /* only a VALID, non-zero reading updates the held value */
        st->power = s_c1_last;          /* transient invalid/zero -> keep last good */
        g_total_power = s_c1_last;
    } else {
        st->power = st->power_valid ? power : 0;
        g_total_power = st->power;
    }
    st->valid = 1;
    return 1;
}

/* PDC i2c read health counters (diagnostic; read via J-Link to verify the SDA-dir fix). */
volatile uint32_t g_pdc_crc_ok, g_pdc_crc_fail;
volatile uint32_t g_port_crc_ok[6];
volatile uint8_t  g_last_pkt[3][6];     /* raw reg-0x88 packet per USB-C port (diag) */
volatile uint8_t  g_boot_pkt[3][6];     /* isolated post-init read (no loop contention) */
volatile uint32_t g_adc_raw[3];         /* raw ADC ch1/ch2/ch5 (diag for USB-A scaling) */
