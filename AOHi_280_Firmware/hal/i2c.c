/* i2c.c - bit-banged I2C master implementation (clean-room from stock i2c_*). */
#include "i2c.h"
#include "gpio.h"

static uint8_t s_active;          /* g_i2c_active_port */
static int     s_last_ack;        /* g_i2c_ack: 0 = ACK, -1/0xFF = NACK */

static void sda_set_dir(int input);
void i2c_select_port(uint8_t port) { s_active = port; }

/* Configure the bit-bang I2C pins: SCL = port2 pin13 (shared), and each port's
 * SDA, as push-pull outputs. Without this the telemetry bus can't drive. */
void i2c_init(void)
{
    extern const uint8_t g_i2c_port_count;
    /* SCL is a push-pull OUTPUT. gpio_config maps dir==2 -> input, else output;
     * dir=2 here previously made SCL an INPUT (no clock driver -> i2c dead, and
     * POERC bit13=0 vs stock's stable 1). dir=0 = output. */
    gpio_cfg_t scl = {0}; scl.mode = 1; scl.dir = 0; scl.w3 = 4; scl.pull = 32;
    gpio_config(I2C_SCL_PORT, I2C_SCL_MASK, &scl);   /* SCL output */
    /* OPEN-DRAIN (NOD): a full-port PCR diff vs stock showed stock drives PC13 (this
     * bus's SCL) open-drain (NOD=1) while mine was push-pull - the same I2C bug the PDC
     * bus had. PCRC13 @0x40053CB4 bit2; PCR writes need PWPR (0x40053BFC) unlocked. */
    *(volatile uint16_t *)0x40053BFCu = 0xA501u;     /* unlock */
    *(volatile uint16_t *)0x40053CB4u |= 0x0004u;    /* PC13 SCL open-drain */
    *(volatile uint16_t *)0x40053BFCu = 0xA500u;     /* re-lock */
    gpio_set_pins(I2C_SCL_PORT, I2C_SCL_MASK);
    for (uint8_t p = 0; p < g_i2c_port_count; p++) {
        s_active = p;
        gpio_set_pins(g_i2c_ports[p].sda_port, g_i2c_ports[p].sda_mask);
        sda_set_dir(0);                              /* SDA output, idle high */
    }
    s_active = 0;
}

/* Debug: probe every 7-bit address on `port`'s bus; out[a]=1 if the slave ACKs.
 * Used to discover the telemetry controller address. */
void i2c_scan(uint8_t port, volatile uint8_t *out)
{
    s_active = port;
    for (uint8_t a = 0; a < 0x80u; a++) {
        sda_set_dir(0);
        i2c_start();
        int ack = i2c_write_byte((uint8_t)(a << 1));   /* 0 = ACK */
        i2c_stop();
        out[a] = (ack == 0) ? 1u : 0u;
    }
}

/* Stock i2c_delay: busy loop of (n<<6) iterations. */
static void i2c_delay(int n)
{
    volatile int c = n << 6;
    while (c--) { }
}

static void scl_set(int level)
{
    if (level) gpio_set_pins(I2C_SCL_PORT, I2C_SCL_MASK);
    else       gpio_clear_pins(I2C_SCL_PORT, I2C_SCL_MASK);
}

static void sda_set(int level)
{
    const i2c_port_cfg_t *p = &g_i2c_ports[s_active];
    if (level) gpio_set_pins(p->sda_port, p->sda_mask);
    else       gpio_clear_pins(p->sda_port, p->sda_mask);
}

static int sda_get(void)
{
    const i2c_port_cfg_t *p = &g_i2c_ports[s_active];
    return gpio_read(p->sda_port, p->sda_mask);
}

/* Configure SDA pin as input(1) or output(0). Stock builds a fixed cfg
 * descriptor and toggles the direction word; the written value is the OR of
 * all words masked by GPIO_CFG_VAL_MASK. */
static void sda_set_dir(int input)
{
    const i2c_port_cfg_t *p = &g_i2c_ports[s_active];
    gpio_cfg_t cfg = {0};
    cfg.mode = 1;          /* w0 */
    /* gpio_config maps dir==2 -> Pin_Mode_In, else Pin_Mode_Out. So input=2,
     * output=0. The previous `input ? 0 : 2` was inverted (drove SDA during
     * reads, released during writes -> broken bus). */
    cfg.dir  = input ? 2 : 0;
    cfg.w3   = 4;          /* w2 */
    cfg.pull = 32;         /* w3 */
    cfg.speed = 1;         /* OPEN-DRAIN (same fix as the PDC bus): push-pull SDA caused a
                            * STOP glitch when switching to output-high after a 0 data bit,
                            * so reg-0x21 (total power) read garbage (g_total_power ~14M).
                            * Open-drain makes "high" a release -> clean multi-byte reads. */
    gpio_config(p->sda_port, p->sda_mask, &cfg);
}

void i2c_start(void)
{
    sda_set(1); i2c_delay(6);
    scl_set(1); i2c_delay(6);
    sda_set(0); i2c_delay(6);
    scl_set(0);
}

static void i2c_stop_setup(void)
{
    sda_set(0); scl_set(0); i2c_delay(6);
    scl_set(1); i2c_delay(6);
    sda_set(1);
}

void i2c_stop(void)
{
    i2c_stop_setup();
    sda_set(1);
    scl_set(1);
    i2c_delay(64);   /* stock delay(1) ~ coarse */
}

/* Read the slave's ACK bit. Sets s_last_ack (0 = ACK, -1 = NACK). */
static int read_ack(void)
{
    scl_set(0);
    sda_set(1);          /* drive SDA high first, THEN release (PDC bus approach) -
                          * avoids relying on the slow pull-up rise; without it the
                          * slave's ACK pull-low was misread -> no ACK ever seen. */
    sda_set_dir(1);
    i2c_delay(3);
    scl_set(1);
    i2c_delay(6);
    if (sda_get()) {
        s_last_ack = -1;
        return -1;
    }
    scl_set(0);
    i2c_delay(6);
    sda_set_dir(0);
    return 0;
}

int i2c_write_byte(uint8_t val)
{
    scl_set(0);
    for (int bit = 8; bit; --bit) {
        scl_set(0);
        i2c_delay(3);
        sda_set((val >> (bit - 1)) & 1);
        i2c_delay(3);
        scl_set(1);
        i2c_delay(6);
        scl_set(0);
    }
    return read_ack();
}

uint8_t i2c_read_byte(void)
{
    uint8_t v = 0;
    sda_set(1);          /* drive high then release (PDC approach) */
    sda_set_dir(1);
    for (int bit = 8; bit; --bit) {
        scl_set(0);
        i2c_delay(6);
        scl_set(1);
        i2c_delay(3);
        v = (uint8_t)((v << 1) | (sda_get() ? 1 : 0));
        i2c_delay(3);
        scl_set(0);
    }
    sda_set_dir(0);
    return v;
}

void i2c_master_ack(int nack)
{
    scl_set(0);
    i2c_delay(6);
    sda_set(nack);
    scl_set(1);
    i2c_delay(6);
    scl_set(0);
    i2c_delay(6);
}

/* TEMP DIAG: scan the 0x16 telemetry bus (the OTHER bus, PC13/PF2) - read regs
 * 0x00..0x3F from each configured port into 0x1FFF8780, to find where live V/I is
 * (the PDC bus reads returned 0; this is the second bus the stock uses). */
void i2c_diag_dump(void)
{
    extern const uint8_t g_i2c_port_count;
    volatile uint8_t *o = (volatile uint8_t *)0x1FFF8780u;
    /* [0..63] port0 (0x16) regs 0x00..0x3F low byte; [64..71] = full u32 of regs
     * 0x00..0x01 for value inspection; [72] = ack of a bare addr write to 0x16. */
    for (int r = 0; r < 0x40; r++) {
        uint32_t v = 0;
        o[r] = i2c_read_reg32(0, (uint8_t)r, &v) ? (uint8_t)v : 0u;
    }
    { uint32_t v = 0; i2c_read_reg32(0, 0x00, &v); for (int k = 0; k < 4; k++) o[64 + k] = (uint8_t)(v >> (8 * k)); }
    { uint32_t v = 0; i2c_read_reg32(0, 0x01, &v); for (int k = 0; k < 4; k++) o[68 + k] = (uint8_t)(v >> (8 * k)); }
    { i2c_select_port(0); sda_set_dir(0); i2c_start();
      int ack = i2c_write_byte((uint8_t)(g_i2c_ports[0].addr7 << 1)); i2c_stop();
      o[72] = (ack == 0); }
}

volatile uint8_t g_i2c_busy;   /* set while a bit-bang transaction is in progress, so the SysTick
                                * keepalive (SysTick_Handler) never reenters mid-transaction. */
int i2c_read_reg32(uint8_t port, uint8_t reg, uint32_t *out)
{
    g_i2c_busy = 1;
    i2c_select_port(port);
    const uint8_t addr = g_i2c_ports[port].addr7;

    sda_set_dir(0);
    i2c_start();
    if (i2c_write_byte((uint8_t)(addr << 1)) < 0) goto fail;        /* write addr */
    if (i2c_write_byte(reg) < 0)                  goto fail;        /* reg index  */
    i2c_start();
    if (i2c_write_byte((uint8_t)((addr << 1) | 1)) < 0) goto fail;  /* read addr  */

    uint32_t v = 0;
    for (int j = 0; j <= 3; j++) {
        uint8_t b = i2c_read_byte();
        if (s_last_ack == -1) goto fail;
        v |= (uint32_t)b << (8 * j);
        if (j <= 2) i2c_master_ack(0);   /* ACK all but last byte */
    }
    *out = v;
    i2c_master_ack(1);                   /* NACK final byte */
    i2c_stop_setup();
    g_i2c_busy = 0;
    return 1;

fail:
    i2c_stop();
    g_i2c_busy = 0;
    return 0;
}

/* Write `len` data bytes to `reg` of port's chip. Stock format (sub_171DC @0x171DC):
 * addr, reg, LEN, data[LEN]. */
int i2c_write_reg(uint8_t port, uint8_t reg, const uint8_t *data, uint8_t len)
{
    g_i2c_busy = 1;
    i2c_select_port(port);
    const uint8_t addr = g_i2c_ports[port].addr7;
    sda_set_dir(0);
    i2c_start();
    if (i2c_write_byte((uint8_t)(addr << 1)) < 0) goto fail;
    if (i2c_write_byte(reg) < 0)                  goto fail;
    if (i2c_write_byte(len) < 0)                  goto fail;
    for (uint8_t j = 0; j < len; j++)
        if (i2c_write_byte(data[j]) < 0)          goto fail;
    i2c_stop_setup();
    g_i2c_busy = 0;
    return 1;
fail:
    i2c_stop();
    g_i2c_busy = 0;
    return 0;
}

int i2c_write_reg8(uint8_t port, uint8_t reg, uint8_t value)
{ return i2c_write_reg(port, reg, &value, 1); }

int i2c_write_reg32(uint8_t port, uint8_t reg, uint32_t value)
{
    uint8_t d[4] = { (uint8_t)value, (uint8_t)(value >> 8),
                     (uint8_t)(value >> 16), (uint8_t)(value >> 24) };
    return i2c_write_reg(port, reg, d, 4);
}
