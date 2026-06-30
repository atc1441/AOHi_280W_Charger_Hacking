/* display.c - GC9x-class 200x480 LCD over SPI3, rewritten on the HC32F460 DDL.
 *
 * Replaces the hand-rolled register-poke display HAL: GPIO via PORT_*, SPI via
 * SPI_Init/SPI_SendData8 (3-wire master, write-only), clock gate via PWC FCG.
 * The panel init command sequence + window/fill are 1:1 with the stock LCD_Init. */
#include "display.h"
#include "board.h"
#include "wdt.h"
#include <hc32_ddl.h>
#include <hc32f460_gpio.h>
#include <hc32f460_spi.h>
#include <hc32f460_pwc.h>
#include <hc32f460_utility.h>

/* SWDT-safe delay: refresh the watchdog every millisecond. */
static void delay_ms(uint32_t ms) { while (ms--) { wdt_feed(); Ddl_Delay1ms(1); } }

/* ---- low-level SPI3 byte stream ---- */
#define SPI3_SR_TDEF   (1u << 5)
#define SPI3_SR_IDLE   (1u << 1)

/* --- DEBUG screenshot shadow: a grayscale mirror of the framebuffer, captured while RAMWR pixels
 * stream. Read over SWD (g_lcd_shadow, LCD_WIDTH*LCD_HEIGHT bytes) and render as an 'L' image. --- */
uint8_t g_lcd_shadow[LCD_WIDTH * LCD_HEIGHT];
volatile uint8_t g_shadow_en;                 /* OPT-IN: set =1 over SWD before a capture; default 0
                                               * so spi_byte does ZERO extra per-pixel work normally
                                               * (full-screen fire frames stay fast). */
static volatile int s_cap_on;
static int s_win_x, s_win_y, s_win_w;
static uint32_t s_cap_idx;
static uint8_t s_cap_b0; static int s_cap_phase;

/* 1:1 with stock spi_send_txonly (sub_EE00): send-only (TXMDS=1) has no RX, so
 * write the byte to DR and wait for TDEF (SR bit5 = TX buffer empty). */
static void spi_byte(uint8_t b)
{
    static uint32_t fed;
    if ((++fed & 0x3FFu) == 0u) wdt_feed();   /* feed SWDT every 1024 bytes */
    M4_SPI3->DR = b;                          /* stock: *a1 = byte */
    uint32_t g = 2000000u;
    while ((M4_SPI3->SR & SPI3_SR_TDEF) != SPI3_SR_TDEF && g) { g--; }  /* wait TDEF */
    if (s_cap_on) {                            /* mirror the pixel into g_lcd_shadow (2 bytes/pixel) */
        if (!s_cap_phase) { s_cap_b0 = b; s_cap_phase = 1; }
        else {
            s_cap_phase = 0;
            uint32_t w = (uint32_t)s_win_w, k = s_cap_idx++;
            if (w) {
                int x = s_win_x + (int)(k % w), y = s_win_y + (int)(k / w);
                if ((unsigned)x < LCD_WIDTH && (unsigned)y < LCD_HEIGHT)
                    g_lcd_shadow[y * LCD_WIDTH + x] = (uint8_t)(((uint16_t)s_cap_b0 + b) >> 1);
            }
        }
    }
}
static void spi_wait_idle(void)
{
    uint32_t g = 2000000u;
    while ((M4_SPI3->SR & SPI3_SR_IDLE) && g--) { }
}

/* SPE = SPI enable (CR1 bit6). 1:1 with stock spi_set_dma_req: SPE is turned on
 * only for the duration of a transfer and left off at idle, so CR1 reads 0x0A
 * (idle) / 0x4A (active) exactly like stock. */
#define SPE_ON()   (M4_SPI3->CR1 |= 0x40u)
#define SPE_OFF()  (M4_SPI3->CR1 &= ~0x40u)

#define CS_LOW()   PORT_ResetBits(LCD_CS_PORT, LCD_CS_PIN)
#define CS_HIGH()  PORT_SetBits(LCD_CS_PORT, LCD_CS_PIN)
#define DC_CMD()   PORT_ResetBits(LCD_DC_PORT, LCD_DC_PIN)   /* DC low  = command */
#define DC_DATA()  PORT_SetBits(LCD_DC_PORT, LCD_DC_PIN)     /* DC high = data    */

/* 1:1 with stock lcd_write_cmd (0x10371): CS goes high after the command byte,
 * then low again for the data (two separate CS transactions). */
void lcd_write_cmd(uint8_t cmd, const uint8_t *data, uint32_t n)
{
    s_cap_on = 0;                              /* command frame -> never captured */
    SPE_ON();                                  /* stock spi_set_dma_req(1) */
    CS_LOW();                                  /* gpio_clear_pins(1,2)    */
    DC_CMD();                                  /* gpio_clear_pins(1,0x8000) */
    spi_byte(cmd); spi_wait_idle();
    CS_HIGH();                                 /* gpio_set_pins(1,2)      */
    if (n) {
        CS_LOW();                              /* gpio_clear_pins(1,2)    */
        DC_DATA();                             /* gpio_set_pins(1,0x8000) */
        for (uint32_t i = 0; i < n; i++) spi_byte(data[i]);
        spi_wait_idle();
        CS_HIGH();                             /* gpio_set_pins(1,2)      */
    }
    SPE_OFF();                                 /* idle: SPE off (CR1=0x0A) */
}
static void cmd0(uint8_t c) { lcd_write_cmd(c, 0, 0); }
static void cmd1(uint8_t c, uint8_t a) { uint8_t d = a; lcd_write_cmd(c, &d, 1); }

void lcd_set_window(int x, int y, int w, int h)
{
    s_win_x = x; s_win_y = y; s_win_w = w;     /* remember for the shadow capture */
    int x0 = x + LCD_COL_OFFSET, x1 = x + w - 1 + LCD_COL_OFFSET;
    int y0 = y + LCD_ROW_OFFSET, y1 = y + h - 1 + LCD_ROW_OFFSET;
    uint8_t caset[4] = { (uint8_t)(x0 >> 8), (uint8_t)x0, (uint8_t)(x1 >> 8), (uint8_t)x1 };
    uint8_t raset[4] = { (uint8_t)(y0 >> 8), (uint8_t)y0, (uint8_t)(y1 >> 8), (uint8_t)y1 };
    lcd_write_cmd(0x2A, caset, 4);
    lcd_write_cmd(0x2B, raset, 4);
}

void lcd_start_ramwr(void)
{
    SPE_ON();
    CS_LOW(); DC_CMD(); spi_byte(0x2C); spi_wait_idle(); DC_DATA();
    s_cap_idx = 0; s_cap_phase = 0; s_cap_on = g_shadow_en;   /* capture only when opted in */
}
void lcd_end_ramwr(void) { s_cap_on = 0; spi_wait_idle(); CS_HIGH(); SPE_OFF(); }

/* Send `count` uint16 line-buffer entries as raw little-endian bytes (1:1 with the
 * stock spi0_write(linebuf, count*2)). */
void lcd_send_pixels(const uint16_t *px, uint32_t count)
{
    const uint8_t *b = (const uint8_t *)px;
    for (uint32_t i = 0; i < count; i++) {
        spi_byte(b[2 * i]);          /* low byte first (LE) */
        spi_byte(b[2 * i + 1]);
    }
}

void lcd_fill(uint16_t rgb565)
{
    lcd_set_window(0, 0, LCD_WIDTH, LCD_HEIGHT);
    lcd_start_ramwr();
    uint8_t hi = (uint8_t)(rgb565 >> 8), lo = (uint8_t)rgb565;
    for (uint32_t i = 0; i < (uint32_t)LCD_WIDTH * LCD_HEIGHT; i++) { spi_byte(hi); spi_byte(lo); }
    lcd_end_ramwr();
}

void lcd_fill_rect(int x, int y, int w, int h, uint16_t rgb565)
{
    if (w <= 0 || h <= 0) return;
    lcd_set_window(x, y, w, h);
    lcd_start_ramwr();
    uint8_t hi = (uint8_t)(rgb565 >> 8), lo = (uint8_t)rgb565;
    for (uint32_t i = 0; i < (uint32_t)w * (uint32_t)h; i++) { spi_byte(hi); spi_byte(lo); }
    lcd_end_ramwr();
}

void lcd_backlight(int on)
{
    /* Backlight is the Timer6 PWM on PA7 (pwm.c). on -> route PA7 to the PWM at the
     * init-configured ~62% (stock brightness); off -> detach PA7 to GPIO-high (active-low driver). */
    extern void pwm_backlight_on(void);
    extern void pwm_backlight_set(uint8_t level);
    if (on) pwm_backlight_on(); else pwm_backlight_set(0);
}

/* Pixels are streamed by the CPU (lcd_send_pixels/lcd_push_bytes), not DMA. */
void lcd_dma_setup(void) { }

/* Push raw bytes into an already-open RAMWR window (stock spi0_write). CS/DC are
 * already set by lcd_start_ramwr(); just stream the bytes. */
void lcd_push_bytes(const void *buf, uint32_t len)
{
    const uint8_t *b = (const uint8_t *)buf;
    for (uint32_t i = 0; i < len; i++) spi_byte(b[i]);
}

/* Display sleep/wake - byte-1:1 with stock EnableDisableDisplay (0x10320):
 *   OFF: SLPIN(0x10) + setBlPWM(0) + delay(10) + PB0 high  (panel-enable off = "LCD aus")
 *   ON : PB0 low + delay(10) + SLPOUT(0x11) + setBlPWM(20) (wake)
 * PB0 (PortB Pin0) is the panel-enable line (low = on), set up in lcd_spi_pin_init. */
void lcd_sleep(int on)
{
    extern void setBlPWM(uint8_t pct);
    if (on) {                                   /* EnableDisableDisplay(1): turn the display OFF */
        cmd0(0x10);                             /* SLPIN */
        setBlPWM(0);
        delay_ms(10);
        PORT_SetBits(PortB, Pin00);             /* PB0 high = panel enable OFF */
    } else {                                    /* EnableDisableDisplay(0): turn the display ON */
        PORT_ResetBits(PortB, Pin00);           /* PB0 low = panel enable ON */
        delay_ms(10);
        cmd0(0x11);                             /* SLPOUT */
        setBlPWM(20);
    }
}

/* GC9x panel init (1:1 with stock LCD_Init). */
static const uint8_t lcd_gamma[32] = {
    0x07,0x01,0x1A,0x00,0x0A,0x09,0x0C,0x0B,0x03,0x19,0x1E,0x1D,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x1D,0x1E,0x19,0x05,0x13,0x14,0x11,0x12,0x00,0x1A,0x02,0x08
};

static void panel_init_seq(void)
{
    cmd0(0xFE); cmd0(0xEF);
    cmd1(0x80,0x11); cmd1(0x81,0x30); cmd1(0x82,0x09); cmd1(0x83,0x03);
    cmd1(0x84,0x30); cmd1(0x89,0x18); cmd1(0x8A,0x40); cmd1(0x8B,0x0A);
    cmd1(0x3A,0x05); cmd1(0x36,0x00); cmd1(0xEC,0x00);
    { static const uint8_t d[6]={0x01,0xBF,0x00,0x00,0x00,0x00}; lcd_write_cmd(0x74,d,6); }
    cmd1(0x98,0x3E); cmd1(0x99,0x3E);
    { static const uint8_t d[2]={0x01,0x04}; lcd_write_cmd(0xA1,d,2); lcd_write_cmd(0xA2,d,2); }
    cmd1(0xCB,0x02);
    { static const uint8_t d[2]={0xB6,0x24}; lcd_write_cmd(0x7C,d,2); }
    cmd1(0xAC,0x44); cmd1(0xF6,0x80);
    { static const uint8_t d[2]={0x0C,0x0C}; lcd_write_cmd(0xB5,d,2); }
    { static const uint8_t d[2]={0x01,0xDF}; lcd_write_cmd(0xEB,d,2); }
    { static const uint8_t d[4]={0x38,0x0C,0x13,0x56}; lcd_write_cmd(0x60,d,4); }
    { static const uint8_t d[4]={0x38,0x0E,0x13,0x56}; lcd_write_cmd(0x63,d,4); }
    { static const uint8_t d[4]={0x3B,0xDA,0x58,0x38}; lcd_write_cmd(0x61,d,4); }
    { static const uint8_t d[4]={0x3B,0xDA,0x58,0x38}; lcd_write_cmd(0x62,d,4); }
    { static const uint8_t d[6]={0x38,0x10,0x73,0xD6,0x13,0x56}; lcd_write_cmd(0x64,d,6); }
    { static const uint8_t d[6]={0x38,0x11,0x73,0xD7,0x13,0x56}; lcd_write_cmd(0x66,d,6); }
    { static const uint8_t d[7]={0x00,0x0B,0xDA,0x0B,0xDA,0x1C,0x1C}; lcd_write_cmd(0x68,d,7); }
    { static const uint8_t d[7]={0x00,0x0B,0xE6,0x0B,0xE6,0x1C,0x1C}; lcd_write_cmd(0x69,d,7); }
    { static const uint8_t d[2]={0x15,0x00}; lcd_write_cmd(0x6A,d,2); }
    lcd_write_cmd(0x6E, lcd_gamma, 32);
    { static const uint8_t d[7]={0xCC,0x0C,0xCC,0x84,0xCC,0x04,0x5F}; lcd_write_cmd(0x6C,d,7); }
    cmd1(0x7D,0x72);
    { static const uint8_t d[10]={0x02,0x03,0x09,0x07,0x09,0x03,0x09,0x07,0x09,0x03}; lcd_write_cmd(0x70,d,10); }
    { static const uint8_t d[4]={0x06,0x06,0x05,0x06}; lcd_write_cmd(0x90,d,4); }
    { static const uint8_t d[3]={0x45,0xFF,0x00}; lcd_write_cmd(0x93,d,3); }
    cmd1(0xC3,0x15); cmd1(0xC4,0x36); cmd1(0xC9,0x3D);
    { static const uint8_t d[6]={0x4D,0x10,0x0A,0x0A,0x06,0x34}; lcd_write_cmd(0xF0,d,6); }
    { static const uint8_t d[6]={0x4D,0x10,0x0A,0x0A,0x06,0x33}; lcd_write_cmd(0xF2,d,6); }
    { static const uint8_t d[6]={0x48,0x90,0x93,0x2D,0x2F,0x4F}; lcd_write_cmd(0xF1,d,6); }
    { static const uint8_t d[6]={0x48,0x70,0x73,0x2D,0x2F,0x4F}; lcd_write_cmd(0xF3,d,6); }
    cmd1(0xF9,0x20); cmd1(0xBE,0x11);
    { static const uint8_t d[2]={0x00,0x00}; lcd_write_cmd(0xFB,d,2); }
    cmd1(0xB4,0x0A); cmd1(0x35,0x00);
    cmd0(0xFE); cmd0(0xEE);
    cmd0(0x11);                 /* sleep out */
    delay_ms(120);
    /* Clear GRAM to black BEFORE enabling the display. The stock bootloader handed
     * the panel over with GRAM already black; our from-scratch bootloader does not,
     * so without this the panel powers on showing stale/garbage GRAM ("noise") under
     * the backlight for the whole bring-up. Filling here makes DISPON show black
     * regardless of the backlight state/polarity. */
    lcd_fill(0x0000);
    cmd0(0x29);                 /* display on (stock LCD_Init issues DISPON here - byte-1:1) */
    delay_ms(20);
}

/* DISPON helper retained for compatibility; lcd_init now issues DISPON inline (stock 1:1). */
void lcd_display_on(void) { cmd0(0x29); delay_ms(20); }

void lcd_init(void)
{
    /* peripheral clock gate for SPI3 */
    PWC_Fcg1PeriphClockCmd(PWC_FCG1_PERIPH_SPI3, Enable);

    /* control lines as push-pull outputs (NOT backlight PA7 - owned by the
     * Timer6 PWM; stock LCD_Init does not touch it). */
    stc_port_init_t o = { .enPinMode = Pin_Mode_Out, .enPinDrv = Pin_Drv_H, .enPinOType = Pin_OType_Cmos };
    PORT_Init(LCD_CS_PORT,  LCD_CS_PIN,  &o);
    PORT_Init(LCD_DC_PORT,  LCD_DC_PIN,  &o);
    PORT_Init(LCD_RST_PORT, LCD_RST_PIN, &o);
    /* Panel ENABLE = PB0 (1:1 with stock lcd_spi_pin_init): configure it as a
     * push-pull OUTPUT (POUTE=1) and drive it LOW (active-low enable). Without
     * the PORT_Init PB0 stays POUTE=0 and gpio_clear_pins never drives the pad,
     * so the panel stays disabled regardless of the SPI data. */
    PORT_Init(PortB, Pin00, &o);
    PORT_ResetBits(PortB, Pin00);          /* assert panel enable (low) */
    CS_HIGH();

    /* SPI3 SCK/MOSI: stock drives these with HIGH drive strength (live read of
     * the stock app: PCR low byte 0x21 = DRV bit5 set). At ClkDiv2 (~pclk/2) the
     * default low drive can't switch the line fast enough -> degraded signal ->
     * black panel. PORT_Init with Pin_Drv_H sets the high drive (+ POUTE, which
     * the SPI ignores), then route to the SPI3 function. */
    PORT_Init(LCD_SPI_SCK_PORT,  LCD_SPI_SCK_PIN,  &o);   /* o = Pin_Drv_H (high drive) */
    PORT_Init(LCD_SPI_MOSI_PORT, LCD_SPI_MOSI_PIN, &o);
    PORT_SetFunc(LCD_SPI_SCK_PORT,  LCD_SPI_SCK_PIN,  LCD_SPI_SCK_FUNC,  Disable);
    PORT_SetFunc(LCD_SPI_MOSI_PORT, LCD_SPI_MOSI_PIN, LCD_SPI_MOSI_FUNC, Disable);
    /* Match stock EXACTLY on the SPI AF pins (live A/B: stock PCR low byte 0x21 =
     * POUT=1, DRV=high, POUTE=0). PORT_Init left POUTE=1, which enables the GPIO
     * output and makes it CONTEND with the SPI peripheral on the data lines ->
     * corrupted MOSI/SCK -> black. Clear POUTE (and set POUT=1) so only the SPI
     * drives the pads, byte-identical to stock. */
    PORT_Unlock();
    { volatile uint16_t *p13 = (uint16_t *)((uint32_t)&M4_PORT->PCRA0 + 1u*0x40u + 13u*4u);
      volatile uint16_t *p14 = (uint16_t *)((uint32_t)&M4_PORT->PCRA0 + 1u*0x40u + 14u*4u);
      *p13 = (uint16_t)((*p13 & ~0x2u) | 0x1u);   /* POUTE=0, POUT=1 */
      *p14 = (uint16_t)((*p14 & ~0x2u) | 0x1u); }
    PORT_Lock();

    /* SPI3 config = stock spi3_lcd_init (IDA: CR1=0x0A, CFG2=0x403):
     * MODE 3 (CPOL=1 idle-high, CPHA=1), MBR=0 (ClkDiv2), 8-bit, MSB,
     * send-only master, 3-line (write-only LCD, GPIO CS). */
    /* byte-identical to stock spi3_lcd_init -> CR1=0x0A, CFG2=0x403 */
    stc_spi_init_t spi = {0};
    spi.enClkDiv          = SpiClkDiv2;          /* CFG2 MBR=0 (stock) */
    spi.enFrameNumber     = SpiFrameNumber1;
    spi.enDataLength      = SpiDataLengthBit8;   /* CFG2 DSIZE=4 = 8-bit */
    spi.enFirstBitPosition= SpiFirstBitPositionMSB;
    spi.enSckPolarity     = SpiSckIdleLevelHigh; /* CFG2 CPOL=1 (mode 3) */
    spi.enSckPhase        = SpiSckOddChangeEvenSample; /* CFG2 CPHA=1 (mode 3) */
    spi.enReadBufferObject= SpiReadReceiverBuffer;
    spi.enWorkMode        = SpiWorkMode4Line;    /* CR1 SPIMDS=0 (stock) */
    spi.enTransMode       = SpiTransOnlySend;    /* CR1 TXMDS=1 (stock) */
    spi.enMasterSlaveMode = SpiModeMaster;
    spi.enCommAutoSuspendEn   = Disable;
    spi.enModeFaultErrorDetectEn = Disable;
    spi.enParitySelfDetectEn  = Disable;
    spi.enParityEn        = Disable;
    spi.enParity          = SpiParityEven;
    SPI_Init(LCD_SPI, &spi);
    SPI_Cmd(LCD_SPI, Disable);   /* SPE off at idle; toggled per transfer (stock) */

    /* hardware reset pulse, then the panel init sequence */
    PORT_SetBits(LCD_RST_PORT, LCD_RST_PIN);   delay_ms(50);
    PORT_ResetBits(LCD_RST_PORT, LCD_RST_PIN); delay_ms(50);
    PORT_SetBits(LCD_RST_PORT, LCD_RST_PIN);   delay_ms(120);
    panel_init_seq();
    /* backlight is brought up by pwm_backlight_init() (Timer6 PWM, called in
     * lcd_setup before lcd_init); do not touch PA7 here. */
}
