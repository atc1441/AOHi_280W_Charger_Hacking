/* fw_main.c - stock-faithful top-level bring-up and main loop.
 *
 * Mirrors stock main() (0x17A74) and Inits() (0x19B98). Inits() programs the
 * USB-PD / charge controller over I2C as a sequence of single-byte register
 * writes and read-modify-writes; the register/value pairs below are the values
 * the stock firmware writes. main() brings up the peripherals in stock order
 * then runs the event loop (UI animation, WLAN RX, two buttons, watchdog feed).
 */
#include <stdint.h>
#include <hc32_ddl.h>
#include <hc32f460_gpio.h>
#include "uart.h"
#include "i2c.h"
#include "power.h"
#include "charge.h"
#include "display.h"
#include "render.h"
#include "clockfont.h"
#include "spi.h"
#include "extflash.h"
#include "buttons.h"
#include "dbgmenu.h"
#include "graphmenu.h"
#include "tick.h"
#include "ui.h"
#include "anim.h"
#include "wlan.h"
#include "pwm.h"
#include "gpio.h"
#include "rtc.h"
#include "temp.h"
#include "wdt.h"
#include <hc32f460_utility.h>

extern uint32_t SystemCoreClock;
extern volatile uint32_t g_total_power;

/* SWDT-safe delay: ms milliseconds, refreshing the watchdog each ms. */
static void delay_feed_ms(uint32_t ms) { while (ms--) { wdt_feed(); Ddl_Delay1ms(1); } }

/* ---- PD/charge controller single-byte register access (port 0) ---- */
/* sub_176FC writes reg<-val; sub_1740C reads reg. Both are 1-byte transfers to
 * the controller at g_i2c_ports[0].addr7. Implemented via the bit-bang bus. */
extern int  pd_reg_write8(uint8_t reg, uint8_t val);
extern int  pd_reg_read8(uint8_t reg, uint8_t *val);

static void pd_w(uint8_t reg, uint8_t val) { pd_reg_write8(reg, val); }
static uint8_t pd_rmw(uint8_t reg, uint8_t and_mask, uint8_t or_mask)
{
    uint8_t v = 0;
    pd_reg_read8(reg, &v);
    v = (uint8_t)((v & and_mask) | or_mask);
    pd_reg_write8(reg, v);
    return v;
}

/* stock 0x1a61c (the FIRST thing Inits/0x19B98 calls, which my transcription
 * dropped): enable the 240W USB-C controller's output channels. Each write goes to
 * the 0x78 controller via 0x176fc. Without this prologue the 240W port stays dark
 * (the 3x SW3556 + USB-A work via their own enables; only the 240W needs this). */
static void pd_init_240w_channels(void)
{
    pd_w(0x10, 0x20);
    pd_w(0x10, 0x20);
    pd_w(0x10, 0x40);
    pd_w(0x10, 0x80);
    pd_w(0x15, 0x20);
    pd_w(0x15, 0x40);
    pd_w(0x15, 0x80);
}

/* Faithful transcription of stock Inits() register programming. */
static void pd_charger_init(void)
{
    pd_init_240w_channels();        /* stock: bl 0x1a61c (first call in Inits) */
    pd_w(0x16, 0x04);
    pd_w(0x11, 0x02);
    pd_w(0x10, 0x81);
    pd_rmw(0x20, 0x81, 0x10);
    pd_rmw(0x13, 0xFF, 0x40); pd_rmw(0x13, ~0x04 & 0xFF, 0x00);
    pd_w(0x15, 0x6F);
    pd_rmw(0x14, 0x17, 0xA0);
    pd_rmw(0x19, 0xFF, 0x47);
    pd_w(0x1A, 0xFF);
    pd_rmw(0x1B, 0xFF, 0xC0);
    pd_w(0x2A, 0x51);
    pd_w(0x2B, 0x7F);
    pd_w(0x2C, 0x6E);
    pd_w(0x2E, 0x3C);
    pd_w(0x2F, 0xD2);
    pd_w(0x31, 0x3C);
    pd_rmw(0x33, 0xFE, 0x00);                 /* v = 2*(v>>1) : clear bit0 */
    pd_rmw(0x17, 0x01, 0xAC);
    pd_rmw(0x17, 0x01, 0x00);
    pd_rmw(0x18, 0x3F, 0x40);
    pd_rmw(0x16, 0xFF, 0x9B); pd_rmw(0x16, ~0x08 & 0xFF, 0x00);
    pd_rmw(0x0C, 0xFF, 0x80);
    pd_w(0x80, 0x00);
}

/* ---- Main-loop tasks (1:1 with stock main's gated block) + the per-task flag
 * mechanism (stock: sw-timer table in SysTick sets byte_2000F3C9[]/byte_2000F3D6).
 * Each task acts only when its periodic flag is set. ---- */
/* ---- stock main() init tree (1:1 call sequence + names). Bodies wrap the
 * faithful HAL or transcribe the IDA decompile. ---- */
/* 0xD5F0 (1:1 stock): keyed UNLOCK chain for the write-protected EFM / FCG0 /
 * PORT / clock registers. Each set bit of `mode` unlocks one protected block. */
static void clk_D5F0(uint8_t mode)
{
    if (mode & 0x01u) { *(volatile uint32_t *)0x40010400u = 0x123u;      /* EFM FAPRT unlock */
                        *(volatile uint32_t *)0x40010400u = 0x3210u; }
    if (mode & 0x02u)   *(volatile uint32_t *)0x40048010u = 0xA5A50001u; /* FCG0 write enable */
    if (mode & 0x04u)   *(volatile uint16_t *)0x40053BFCu = 0xA501u;     /* PORT PWPR unlock  */
    if (mode & 0x10u)   *(volatile uint16_t *)0x400543FEu |= 0xA508u;
    if (mode & 0x40u)   *(volatile uint16_t *)0x400543FEu |= 0xA503u;
    if (mode & 0x80u) { *(volatile uint32_t *)0x40050804u = 0x77u;       /* bit7: unlock 0x4005080x */
                        *(volatile uint32_t *)0x4005080Cu = 0x77u; }
}

static void clk_DCAC(void)                 /* 0xDCAC: clock_pll_bringup + sub_F854.
   VERIFIED no-op: a live read of PLLCFGR (0x40054100) shows the bootloader has
   already set it to 0x22201980 - the EXACT value stock's clock_pll_bringup
   writes. The clock state is therefore byte-identical to running clk_DCAC, so
   re-running the PLL bring-up (and risking a glitch on the live clock) would
   change nothing. Functionally 1:1 with stock, proven by register read. */
{ }

static void lcd_setup(void)                /* 0xED90: sub_D4F0 + sub_D3A0 + sub_D44C + LCD_Init */
{
    spi0_init();              /* sub_D4F0: LCD SPI0 pins + controller */
    lcd_dma_setup();          /* sub_D3A0: DMA ch0 RAM->SPI0 */
    pwm_backlight_init();     /* sub_D44C: backlight PWM */
    lcd_init();               /* LCD_Init: panel reset + init sequence */
}

static void gpio_DC64(void)                /* 0xDC64: port1 pins 12/2/10 = cfg 0x22 */
{
    gpio_cfg_t c = {0}; c.w1 = 2; c.w3 = 32;
    gpio_config(1, 0x1000, &c);
    gpio_config(1, 0x0004, &c);
    gpio_config(1, 0x0400, &c);
}

/* 0x8CA0 (1:1 stock): bring up the 0x40040000 (ADC1) controller + port0 pins 0/1.
 * Transcribed verbatim from the stock decompile. */
static void gpio_8CA0(void)
{
    *(volatile uint32_t *)0x4004800Cu &= ~1u;
    *(volatile uint16_t *)0x40040002u = 0;
    gpio_cfg_t c = {0}; c.w3 = 0x8000;
    gpio_config(0, 0x0001, &c);                   /* port0 pin0 (PA0) */
    gpio_config(0, 0x0002, &c);                   /* port0 pin1 (PA1) */
    *(volatile uint32_t *)0x4004000Cu |= 0x03u;
    *(volatile uint8_t  *)0x40040001u = 64;
    *(volatile uint8_t  *)0x40040000u = 64;
    *(volatile uint16_t *)0x40040002u =
        (uint16_t)((*(volatile uint16_t *)0x40040002u & 0xF8FFu) + 0x300u);
    *(volatile uint32_t *)0x40040014u |= 0x03u;
}

/* 0xEA60 (clean, DDL gpio only - raw analog-mux pokes dropped): power up the
 * port1 p3/p4 rails (drive high) and leave them enabled. */
static void gpio_EA60(void)
{
    /* ROOT-CAUSE FIX for "I2C reads return 0": PB3=JTDO and PB4=NJTRST are SWJ/JTAG debug
     * pins by default. Until they are released from the debug port, the internal JTAG
     * controller (and any attached debugger) drive them, overriding our GPIO - a GPIO
     * self-test showed PB3 stuck reading 0 and PB4 stuck reading 1 (the JTAG idle levels),
     * so the bit-banged I2C on these pins could never read the slave. Stock frees them via
     * util_B1D4 (clears PSPCR @0x40053BF4 bits 0x10+0x04) + gpio_set_altfunc(Func_Gpio).
     * SWD (PA13/PA14) is unaffected, so the J-Link stays usable. */
    *(volatile uint16_t *)0x40053BFCu = 0xA501u;            /* PWPR: unlock PORT regs */
    *(volatile uint16_t *)0x40053BF4u &= (uint16_t)~0x14u;  /* PSPCR: release PB3/PB4 from SWJ */
    *(volatile uint16_t *)0x40053BFCu = 0xA500u;            /* PWPR: re-lock */
    gpio_set_altfunc(1, 0x08, 0);                /* PB3 -> Func_Gpio (off JTDO) */
    gpio_set_altfunc(1, 0x10, 0);                /* PB4 -> Func_Gpio (off NJTRST) */

    gpio_cfg_t c = {0}; c.mode = 1; c.w1 = 2; c.dir = 32; c.w3 = 64;
    gpio_config(1, 0x0008, &c);
    gpio_config(1, 0x0010, &c);
    gpio_set_pins(1, 0x0008);                   /* PB3 high */
    gpio_set_pins(1, 0x0010);                   /* PB4 high */
    delay_feed_ms(200);                          /* settle */
}

/* gpio_C5E0 (0xC5E0): configures PortC pins PC13 (0x2000) and PC14 (0x4000) as
 * outputs. PC13 is the shared i2c SCL (also set by i2c_init); PC14 is a separate
 * control line stock drives LOW (live A/B: stock POERC=0x6000 PC13+PC14 output,
 * PODRC=0x2000 -> PC14 low). My gpio_17CBC stub dropped this -> PC14 undriven. */
static void gpio_C5E0(void)
{
    gpio_cfg_t o = {0}; o.mode = 1; o.w1 = 2; o.dir = 0; o.w3 = 4; /* dir=0 = output (verified) */
    gpio_config(2, 0x2000, &o);                 /* PC13 (SCL) output */
    gpio_set_pins(2, 0x2000);                   /* PC13 idle high */
    gpio_config(2, 0x4000, &o);                 /* PC14 output */
    gpio_clear_pins(2, 0x4000);                 /* PC14 low (stock) */
}

static void gpio_17CBC(void)               /* 0x17CBC: gpio_C5E0 + fn_17898 + fn_18078 + i2c_DAB0(1) */
{ gpio_C5E0(); charge_set_enable(1); /* i2c_DAB0(1): enable charging */ }

/* 0xD66C (1:1 stock): keyed LOCK chain - re-protects the registers clk_D5F0
 * unlocked. Each set bit of `mode` re-locks one protected block. */
static void clk_D66C(uint8_t mode)
{
    if (mode & 0x01u)   *(volatile uint32_t *)0x40010400u = 0u;          /* EFM FAPRT lock    */
    if (mode & 0x02u)   *(volatile uint32_t *)0x40048010u = 0xA5A50000u; /* FCG0 write disable */
    if (mode & 0x04u)   *(volatile uint16_t *)0x40053BFCu = 0xA500u;     /* PORT PWPR lock    */
    if (mode & 0x10u) { uint16_t v = *(volatile uint16_t *)0x400543FEu;
                        *(volatile uint16_t *)0x400543FEu = (uint16_t)((v & ~0xA508u) | 0xA500u); }
    if (mode & 0x40u) { uint16_t v = *(volatile uint16_t *)0x400543FEu;
                        *(volatile uint16_t *)0x400543FEu = (uint16_t)((v & ~0xA503u) | 0xA500u); }
    if (mode & 0x80u) { *(volatile uint32_t *)0x40050804u = 0x76u;       /* bit7: re-lock 0x4005080x */
                        *(volatile uint32_t *)0x4005080Cu = 0x76u; }
}

static void load_port_config(void)         /* 0x179B4: cfg_read + apply_images; +i2c pins */
{ i2c_init(); apply_images(); }

static void Inits(void)        { pd_charger_init(); }   /* 0x19B98 */

/* Per-port telemetry data block (stock byte_1FFF8390, 24 bytes/port x5). Holds
 * live V/I/P + flags read from the port controllers; power_calc reads it. */
uint8_t g_port_data[120];

/* 0x19B50 (1:1): two PD-config writes (regs 17,22 = 0) then clear g_port_data
 * and set the per-port "present" default byte (+2) for ports 0,1,2. */
static void fn_19B50(void)
{
    pd_reg_write8(17, 0);                       /* unk_1FFFC610=17; fn_176FC() */
    pd_reg_write8(22, 0);                       /* unk_1FFFC610=22; fn_176FC() */
    for (int i = 0; i < 120; i++) g_port_data[i] = 0;   /* memclrr(byte_1FFF8390,120) */
    g_port_data[2]  = 0x8C;                     /* port0 +2 */
    g_port_data[26] = 0x8C;                     /* port1 +2 (24+2) */
    g_port_data[50] = 0x8C;                     /* port2 +2 (48+2) */
}
static void gpio_B048(void)    { buttons_init(); }      /* 0xB048: button pins */
static void UART_Init(void)                              /* 0x12344 */
{
    uart_init(115200);
    uart_set_rx_callback(wlan_rx_isr_byte);
    uart_enable_rx_irq();
    /* IMPORTANT: do NOT send cmd 0x04 here. Stock UART_Init (0x12344..0x12400) sends
     * NOTHING - it ends with a plain pop and never bl's a frame builder. cmd 0x04 is
     * "Reset Wi-Fi" in the Tuya MCU protocol: my earlier (wrong) reconstruction sent it
     * on every boot, which dropped the module's cloud connection to state 3 each time
     * (verified on-device: module cloud-connected under stock, fell to state 3 under
     * mine; TX ring showed the lone cmd 0x04). Removing it lets the cloud stay up. */
}
/* 0xE9A8: the stock early ext-flash bring-up = IRQ-DRIVEN controller setup
 * (registers the ext-flash controller IRQ handler 0xE761 + a ready handshake on
 * the bit-band region 0x4298xxxx). This firmware drives the ext-flash by CPU
 * POLLING (extflash_init / extflash_EBF8), which is verified working (a read of
 * offset 0 matches the flash dump exactly). The IRQ/async path is therefore
 * unused; registering its vector without porting the whole handler tree would
 * only add fault risk for no functional gain. Functionally covered by polling. */
static void extflash_E9A8(void){ }
static void extflash_EBF8(void){ extflash_init(); }     /* 0xEBF8: extflash_clk_init + dma_setup + probe */

static int  app_1204C(void);
static void flags_tick(void);
static void rtc_18448(void);
static void fn_158CC(void);
static void ui_draw_status_icon(void);
static void fn_17CD4(void);
static void power_temp_task(void);
static void ui_11CA8(void);
static void ui_show_page14(void);
static void fn_10F24(void);
static void ui_124F0(void);
int g_wlan_suspend;                 /* stock byte_2000F3DF (suspend WLAN RX) */

/* Low-latency UART servicing: drain RX + parse + send any reply. Called several times
 * per main-loop pass so a module frame (heartbeat / product query) is answered promptly
 * even while the display or I2C is busy. RX bytes themselves are captured in the UART
 * ISR; this does the parse + TX which must not lag behind the module's reply timeout. */
static void wlan_pump(void)
{
    if (!g_wlan_suspend) { uart_poll(); wlan_parse_rx(); }
}

void fw_main(void)
{
    /* ITEM 5 (boot noise) - THE ROOT CAUSE: the stock bootloader hands us the
     * panel powered with the backlight (PA7) already LIT. Our whole bring-up below
     * (clock/extflash/anim_load_all/uart/PD programming) takes a few hundred ms,
     * during which the panel scans out stale/garbage GRAM under a lit backlight ->
     * the "noise before the black image". Kill the backlight as the very FIRST
     * instruction, before any other init, so the entire bring-up runs DARK. It is
     * re-enabled (line ~309) only after the screen has been filled black. */
    {
        stc_port_init_t bl_off = { 0 };
        bl_off.enPinMode  = Pin_Mode_Out;
        bl_off.enPinDrv   = Pin_Drv_H;
        bl_off.enPinOType = Pin_OType_Cmos;
        PORT_Init(PortA, Pin07, &bl_off);
        PORT_SetBits(PortA, Pin07);     /* PA7 high = backlight OFF immediately (active-low driver) */
    }

    /* Peripheral bring-up (stock main() prologue order). Clock was already set
     * by SystemInit()->clock_update() before main; SystemCoreClock is valid. */
    /* Feed the WDT between every bring-up step: the bootloader leaves the
     * watchdog running and the full init (uart/wlan/spi/extflash/lcd/render)
     * exceeds its timeout, causing a reset loop mid-init. */
    /* ==== 1:1 transcription of stock main() prologue (0x17A74) ==== */
    clk_D5F0(0xC7);        WDT_Feed();   /* clock unlock/enable */
    /* Match stock FCG0 exactly (live A/B: stock=0xFFFD3AEE, mine was 0xFFFFFAEE):
     * clear bits 14/15/17 = enable DMA1+DMA2+AOS clocks. Stock drives the SPI3
     * display transfers via DMA (spi_set_dma_req) routed through the AOS event
     * router; FCG0 is write-unlocked here by clk_D5F0. */
    *(volatile uint32_t *)0x40048000u &= ~0x0002C000u;   WDT_Feed();
    clk_DCAC();            WDT_Feed();   /* PLL bring-up (no-op: bootloader set it) */
    extflash_E9A8();       WDT_Feed();   /* ext-flash controller IRQ + ready */
    rtc_init();            WDT_Feed();   /* configure + START the RTC (part of stock 0xE9A8;
                                          * my E9A8 stub dropped it -> RTC was frozen at 12:00) */
    lcd_setup();           WDT_Feed();   /* SPI0 pins + DMA + backlight PWM + LCD_Init */
    extflash_EBF8();       WDT_Feed();   /* ext-flash SPI/DMA + probe */
    anim_load_all();       WDT_Feed();   /* load the 4 screensaver animations (pages 10-13)
                                          * from flash 0x1000000+ (stock anim_load_all 0x11d1c) */
    gpio_B048();           WDT_Feed();   /* button pins */
    gpio_DC64();           WDT_Feed();   /* port1 control pins (DDL gpio) */
    gpio_8CA0();           WDT_Feed();   /* 0x40040000 (ADC1) + PA0/PA1 (1:1 stock) */
    wlan_init();                          /* ringbuf for the UART link (stock: in UART_Init) */
    UART_Init(); __enable_irq(); WDT_Feed();
    gpio_EA60();           WDT_Feed();    /* port1 p3/p4 power-up (DDL gpio) */
    /* CRITICAL ORDER FIX: pdc_init does an I2C general-call reset (0x00,0x06) of ALL PD chips
     * + bus recovery. It MUST run BEFORE Inits() configures the 0x78 controller - otherwise it
     * reset the chips AFTER Inits and wiped their PDO/source config (chips reported caps=0,
     * volt=0, never sourced Vbus). Recover/reset the bus first, THEN program the controller. */
    { extern void pdc_init(void); pdc_init(); }   WDT_Feed();
    { extern void i2c_init(void); i2c_init(); }   /* set up the telemetry I2C pins BEFORE
                                                   * gpio_17CBC's charge_set_enable(1) -
                                                   * else the enable wrote to un-configured
                                                   * pins -> never reached the chip -> no
                                                   * USB voltage. (load_port_config re-inits
                                                   * harmlessly.) */
    gpio_17CBC();          WDT_Feed();
    clk_D66C(0x83);        WDT_Feed();
    load_port_config();    WDT_Feed();
    Inits();               WDT_Feed();   /* PD/charge controller programming */
    fn_19B50();            WDT_Feed();   /* stock Write_0x78_17to0and22to0_and_clear_buff (0x19b50):
                                          * reg17=0/reg22=0 = the boot default channel config. */
    /* HW-CONFIRMED: pd_channel_cfg_hi (reg22=4/reg17=2 on the 0x78 controller) at boot KILLS all
     * per-port telemetry (USB-A 5.1V -> 0, C2-4 reads 0) - it reconfigures the 0x78 channel routing
     * incompatibly with the per-port packet/ADC reads on this unit. So it is NOT the lever for C1's
     * 20V; left out. C1 high-voltage must come from the 0x16 chip's own PDO config / the sink's
     * request, not this global HI config. */
    /* ITEM 7 (no USB voltage): nothing ever set g_channel_mask, so sync_channel_state
     * returned early and the per-channel power-enable (reg 0xA0) was never issued ->
     * the 3 USB-C ports stayed OFF. Default all 3 SW3556 channels ON at boot; the
     * loop's sync_channel_state then drives reg 0xA0=1 on each (now at the correct
     * 0x70/0x71/0x74 addresses). */
    /* THE FIX (bisect-proven): do NOT force the channel mask at boot. The SW3556 PD controllers
     * source Vbus + measure V/I AUTONOMOUSLY after a sink attaches. Forcing g_channel_mask=0x0F
     * made sync_channel_state write reg 0xA0=1 to them, which OVERRODE/killed their autonomous
     * sourcing (per-port volt/cur read back 0). Stock leaves the mask at 0 at boot; reg 0xA0 is
     * only written when the app explicitly toggles a port (DP24-29). Leaving mask=0 -> the ports
     * source on their own and apply_port_cfg reads real V/I (o0=4.9V, o2=9.0V, caps=1). */
    /* HW-CONFIRMED: g_channel_mask must stay 0 at boot. Setting 0x07 (reg 0xA0=1 + LOW config)
     * KILLED the USB-C ports (user: "C1 5V, others 0V, no power") - so reg 0xA0=1 IS a sourcing
     * killer on this unit, NOT "full on" as stock's code suggested. With mask=0 the SW3556 source
     * autonomously (confirmed earlier: C2=4.9V, C4=9.0V). reg 0xA0 / per-port enable stays untouched. */
    { extern uint8_t g_channel_mask; g_channel_mask = 0x00u; }
    /* NOTE: do NOT write the C1 reg3 limit here at boot - it destabilised a 20V contract (C1
     * oscillated 20V<->0V). It is written once from update_outputs (Smart/Raserei) instead, which
     * gave a SOLID 20.0V/3.0A hold. */
    tick_init(SystemCoreClock); WDT_Feed();  /* SysTick (get_tick_ms / sw timers) */
    /* (pdc_init now runs BEFORE Inits - see above; the chip-reset must precede config) */
    /* STOCK main 0x17A74 exactly: after the PD init (fn_19B50 above = Write_0x78_17to0and22to0_and_
     * _clear_buff), wait 200 ms then fill the screen black (LCD_setFullColor(0)). Backlight + DISPON
     * are already on from lcd_setup (backlight_pwm_init + LCD_Init) - the panel GRAM is black at
     * bootloader handoff, so this is noise-free 1:1. (Reverted the custom anti-noise dance per the
     * byte-1:1 goal.) */
    delay_feed_ms(200);                  /* stock delay(200) */
    lcd_fill(0x0000);      WDT_Feed();   /* stock LCD_setFullColor(0) */
    /* Backlight was held OFF through the whole bring-up (pwm_backlight_init leaves
     * PA7 GPIO-low) so the stale GRAM never showed as noise; the panel is black now,
     * so it is safe to light it. */
    lcd_backlight(1);      WDT_Feed();
    g_idle_count = 0;                    /* stock 0x17AD0: dword_20014774 = 0 */

    /* Default power mode at boot = SMART (g_status_mode=1). The periodic Smart re-eval (every 2 s)
     * then distributes the budget by live demand. (User: boot should start in Smart, not the
     * power/Raserei mode that was the default.) */
    g_status_mode = 1u;

    btn_handlers_t menu = { on_menu_btn_click, on_menu_btn_longpress, on_menu_btn_doubleclick };
    btn_handlers_t wlan = { on_wlan_btn_click, on_wlan_btn_longpress, 0 };  /* no double-click (stock) */

    uint32_t wdt_last = get_tick_ms();
    uint32_t pwr_last = wdt_last;

    delay_feed_ms(200);   /* let the per-port PD chips settle/negotiate before the loop */

    /* Main loop = 1:1 transcription of stock main() (0x17A74). Each task internally
     * gates on its own periodic flag (set by the sw-timer table in SysTick_Handler,
     * stock 0xF770); the home/telemetry block is skipped when app_1204C() (page 15
     * or standby). flags_tick() drives the per-task flags from get_tick_ms(). */
    (void)pwr_last; (void)wdt_last;
    for (;;) {
        flags_tick();              /* SysTick timer-table equivalent: set task flags */
        wlan_pump();               /* process UART RX + send replies BEFORE the heavy
                                    * display/I2C work, so handshake responses (cmd 0x00/
                                    * 0x01) are never late - a late reply makes the module
                                    * time out and re-query product, so the handshake never
                                    * completes and it never answers weather/time. */
        ui_home_check();           /* boot animation -> home page */
        wlan_pump();

        extern int mode_anim_active(void);
        /* While a power-mode confirmation animation plays, SUPPRESS normal page/status rendering -
         * otherwise power_temp_task/status-icon/ui_animate_page draw over the fire frames and it
         * FLICKERS (stock gates the same way via the g_menu_page[4] animation-active flag). */
        if (!app_1204C() && !mode_anim_active()) {
            rtc_18448();           /* refresh date/time (flag byte 0) */
            fn_158CC();            /* channel/connection sync (flags 1,2) */
            ui_draw_status_icon(); /* port status icons (flag 4) */
            fn_17CD4();            /* max_of_channels (flag 5) */
            power_temp_task();     /* power_calc -> total watts + temp (byte_2000F3D6) */
            wlan_pump();           /* (power_temp_task does bit-banged I2C - can block) */
            ui_11CA8();            /* page-14 digit (byte_2000F3D6[1]) */
            ui_show_page14();      /* page-14 content */
            fn_10F24();            /* misc page refresh */
        }

        /* Debug page (30): app_1204C() gates the home rendering off, but we still run
         * the full telemetry acquisition so EVERY value is live: fn_158CC reads the
         * per-port V/I (apply_port_cfg) + keeps the ports managed, fn_17CD4 keeps the
         * power-stage pins (20V) alive, power_temp_task does the W + menu_state_update
         * (it only draws home content when page==0). Then redraw the changed values. */
        if (g_menu_page == DEBUG_PAGE) {
            rtc_18448();      /* refresh g_rtc (clock) - gated off on page 30 otherwise -> time froze */
            fn_158CC(); fn_17CD4(); power_temp_task(); ui_debug_tick();
            /* NO clock screensaver here (and no home-timeout in ui_124F0): the debug/graph pages stay
             * put while idle so they can be watched. */
        }
        /* History graph page (31): same live telemetry, then scroll + redraw the graph. */
        if (g_menu_page == GRAPH_PAGE) {
            fn_158CC(); fn_17CD4(); power_temp_task(); ui_graph_tick();
        }

        /* The Raserei fire now runs INSIDE ui_animate_page (anim_state[8]), so it must keep being
         * called while that animation plays - only the static Smart/Individual confirmation pages
         * suppress it. (power_temp_task above stays gated by mode_anim_active, which still covers
         * the fire, so the live render can't draw over it.) */
        extern int mode_static_active(void);
        if (!mode_static_active()) ui_animate_page();   /* page slide/element animation */
        anim_tick();               /* screensaver frame animation (eyes, theme B) */
        { extern void video_tick(void); video_tick(); }  /* internal video page (rickroll) */
        mode_tick();               /* power-mode confirmation animation + auto-return-home */
        { extern void pd_maintain_limits(void); pd_maintain_limits(); }  /* keep C2-C4 reg 0xA0 alive -> stable 20V */
        { extern void charge_keepalive(void); charge_keepalive(); }      /* re-assert 0x16 master enable every loop - REQUIRED so the master stays sourcing (reg1=1) while idle; without it a fresh plug has no source to attach to */
        wlan_pump();               /* (anim_tick blits full-screen frames - can block) */
        if (!g_wlan_suspend) {     /* stock: !(byte_2000F3DF<<31) */
            uart_poll();           /* drain USART1 RX (+ clear sticky error flags) */
            wlan_parse_rx();
            ui_124F0();            /* apply received UI updates */
        }

        button_poll(BTN_MENU, &menu);   /* stock button_fsm(0,...) */
        button_poll(BTN_WLAN, &wlan);   /* stock button_fsm(1,...) */

        uint32_t now = get_tick_ms();
        if (now - wdt_last > 500) { WDT_Feed(); wdt_last = now; }
    }
}

/* stock app_1204C (0x1204C) 1:1: gate the home/telemetry block off when on page
 * 15 OR when the module-driven blank flag byte_2000F3E1 bit0 is set (the cmd 0x20
 * handler @0x16BA8 sets/clears it). My version previously only checked page 15, so
 * the home block kept rendering/polling while the module had blanked it. g_app_blank
 * defaults 0 -> no behaviour change until cmd 0x20 is reconstructed to drive it. */
uint8_t g_app_blank;                 /* byte_2000F3E1 */
static int app_1204C(void) { return (g_menu_page == 15) || (g_menu_page == DEBUG_PAGE) || (g_menu_page == GRAPH_PAGE) || (g_app_blank & 1u); }

/* ---- per-task periodic flags (stock byte_2000F3C9[]/byte_2000F3D6, set by the
 * SysTick sw-timer table). flags_tick() pulses them from get_tick_ms(). ---- */
/* One flag per distinct stock task flag byte:
 *  0 rtc  1 158CC-a  2 158CC-b  3 power  4 status-icon  5 17CD4
 *  6 11CA8  7 10F24 (3D8[2])  8 124F0 (3D8[1]) */
#define NUM_TASK_FLAGS 9
static volatile uint8_t s_flag[NUM_TASK_FLAGS];
static uint8_t tcf(int i) { uint8_t v = s_flag[i]; s_flag[i] = 0; return v; }
/* Per-flag periods (ms) from the stock sw-timer table @0x1FFF8194 (dumped live;
 * struct {u32 count, u32 reload_period, u8* flag, u8 active} x16). Each of my 9
 * task flags maps to a stock flag byte byte_2000F3C9[+off] with its own interval;
 * the previous uniform 250 ms made everything (esp. the +14 clock at 500 ms) run
 * at the wrong rate -> clock/colon ran exactly 2x too fast. Offsets confirmed by
 * disassembly (each task does movw/movt 0x2000F3C9 + adds r0,#off):
 *   0 rtc(+0,no timer/stub) 1 158CC-a(+1,10) 2 158CC-b(+2,100) 3 power(+13,350)
 *   4 status-icon sub_15810(+4,50) 5 17CD4(+5,10) 6 clock 11CA8(+14,500)
 *   7 10F24(+17,300000)     8 124F0(+16,1000)
 * (offsets 3,4 confirmed by disasm: sub_10F54 reads +13, sub_15810 reads +4.) */
static const uint32_t s_flag_period_ms[NUM_TASK_FLAGS] =
    { 1000u, 10u, 100u, 350u, 50u, 10u, 500u, 300000u, 1000u };
static void flags_tick(void)
{
    static uint32_t last[NUM_TASK_FLAGS];
    uint32_t now = get_tick_ms();
    for (int i = 0; i < NUM_TASK_FLAGS; i++) {
        if (now - last[i] >= s_flag_period_ms[i]) { last[i] = now; s_flag[i] = 1; }
    }
}

rtc_time_t g_rtc;
/* g_clock_mode -> stock RAM 0x1FFF8298 (ui_state.h) */
uint8_t g_clock_alt;                  /* stock unk_1FFF8158: alt clock page select   */
uint8_t g_page_group;                 /* stock byte_1FFF830E: page-group mode 0/1/2  */
uint8_t g_page_alt[3];                /* stock byte_1FFF818F/8190/8191: per-group alt */
/* g_page_anim -> stock RAM 0x1FFF829A (ui_state.h) */

/* ui_18CE4 (0x18CE4): enter a page-group page - latch index + transition flag and
 * clear the screen. Sets the clock-suppress flag (unk_1FFF8298).
 *
 * Stock latches byte_1FFF8299 (=idx) + the transition flag and lets the page-slide
 * engine (sub_18D28/sub_18DDC + frame table @0x1FFF8048) animate the target page in.
 * That slide table is not reconstructed here, so after the latch+clear nothing drew
 * the target -> the second clock theme (and menu page 8) showed a BLANK screen. We
 * render the target page directly (ui_set_page) so it actually appears; the only
 * thing skipped vs stock is the cosmetic slide-in animation. */
void ui_18CE4(uint8_t idx, uint8_t anim)
{
    __disable_irq();
    g_clock_mode = 1;                 /* unk_1FFF8298 = 1: suppress clock screensaver */
    g_page_idx   = idx;
    g_page_anim  = (uint8_t)(anim & 1);
    __enable_irq();
    ui_set_page(idx);                 /* draw the target page (slide engine end-state) */
}

/* ui_select_page (0x18BF0): pick page 4/5/6 (or 10/11/12 when the group's alt flag is
 * set) for the current page-group mode. 1:1. */
void ui_select_page(void)
{
    if (g_page_group == 0)      ui_18CE4(g_page_alt[0] == 1 ? 10 : 4, 1);
    else if (g_page_group == 1) ui_18CE4(g_page_alt[1] == 1 ? 11 : 5, 1);
    else if (g_page_group == 2) ui_18CE4(g_page_alt[2] == 1 ? 12 : 6, 1);
}
static void rtc_18448(void)        { if (tcf(0)) rtc_read(&g_rtc); }

/* fn_158CC (0x158CC): two flag-gated PD/port maintenance jobs.
 *   flag byte_2000F3C9[1] -> app_198BC(): apply_port_cfg(0..2) + update_readings()
 *   flag byte_2000F3C9[2] -> sync_channel_state() + fn_18234()
 * Both operate on the per-outlet PD telemetry (HW-gated by the PSU); the periodic
 * read happens via power_temp_task(). Gating kept 1:1; bodies map to power_poll. */
static void fn_158CC(void)
{
    if (tcf(1)) {                        /* app_198BC (0x198BC): 1:1 */
        apply_port_cfg(0);
        apply_port_cfg(1);
        apply_port_cfg(2);
        update_readings();              /* main-rail ADC V/I (stock update_readings) */
    }
    if (tcf(2)) {                        /* stock flag byte_2000F3C9[2] */
        sync_channel_state();           /* drive channel on/off (i2c_1A5F4) */
        pd_settle_tick();               /* fn_18234: channels-ready latch   */
        /* Smart mode re-distributes the shared budget by LIVE demand (draw changes as devices
         * charge), so re-run it periodically (rate-limited; pd_set_port_flag only writes on change). */
        { extern void update_outputs(void);
          static uint32_t s_sm_t;
          if (g_status_mode == 1u && (uint32_t)(get_tick_ms() - s_sm_t) >= 2000u) {
              s_sm_t = get_tick_ms(); update_outputs();
          } }
    }
}

/* ui_draw_status_icon (0x15810): flag-gated port-status refresh + status-bar icon.
 * Stock: (!(byte_2000F3DD bit0) || byte_2000F61A) && flag(3C9[4]) -> poll port
 * status, fn_17CF0()/fn_17920() pick ui_draw_icon() vs util_16D64(). Gating + the
 * status poll are 1:1 here; the icon glyph draw is asset-dependent (TODO). */
static uint8_t s_icon_count;            /* stock unk_2000F3C0: last attached-port count */
static void ui_draw_status_icon(void)
{
    if (!tcf(4)) return;
    for (uint8_t p = 0; p < g_i2c_port_count && p < WLAN_NUM_PORTS; p++) {
        pd_status_t st;
        if (power_poll(p, &st)) g_telemetry.port_flag[p] = st.attached;
    }
    uint8_t cnt = pd_attached_count();   /* fn_17CF0: # attached outlets */
    if (s_icon_count != cnt) s_icon_count = cnt;   /* count change -> redraw (stock resets byte_2000F3DB[0]) */
    /* fn_17920() ? ui_draw_icon() [charging-bolt animation, sub_133E4 + frame tables]
     *            : util_16D64() [state reset] -- icon glyph draw is asset TODO */
}

/* fn_17CD4 (0x17CD4): flag byte_2000F3CE -> max_of_channels(): max() over the 6
 * per-outlet readings (stock @0x1FFF8398, stride 0x18). All zero while HW-gated. */
static void fn_17CD4(void)
{
    if (!tcf(5)) return;
    uint16_t mx = 0;
    for (uint8_t p = 0; p < WLAN_NUM_PORTS; p++)
        if (g_telemetry.p_x100[p] > mx) mx = g_telemetry.p_x100[p];
    g_telemetry.max_power = mx;          /* stock stores the channel max for display */

    /* stock max_of_channels (0x17CD4) ALSO drives three PB power-stage range signals from the max
     * channel voltage/current. HW-confirmed MISSING in the SDK: stock leaves PB2+PB12 HIGH at 20V,
     * my SDK left them LOW -> the boost/power path never latched, so a commanded 20V sagged and the
     * SW3566 error-recovered (~3s detach cycle). v11 = max(voltage,current)/10 over outlets 0..4
     * (voltage = g_outlet[].aux12, current = g_outlet[].volt_raw, both raw x0.1).
     *   PB12 (0x1000): HIGH when v11 > 10  (clear only at low V *and* low power - dominant term V>10)
     *   PB10 (0x400) : HIGH when v11 < ~9.5 (i.e. v11 <= 9), else LOW
     *   PB2  (0x4)   : HIGH when 17 <= v11 <= 30  (the 20V band) */
    uint32_t v10 = 0;
    for (int i = 0; i < 5; i++) {
        if (g_outlet[i].aux12    > v10) v10 = g_outlet[i].aux12;     /* voltage x0.1 */
        if (g_outlet[i].volt_raw > v10) v10 = g_outlet[i].volt_raw;  /* current x0.1 */
    }
    uint32_t v11 = v10 / 10u;          /* SW3566 channel max (C2-C4/A1/A2), NOT C1 */
    /* C1 (0x16) BUS PRE-BOOST. SWD evidence: at 20V the 0x16's reg0x0A (input-rail sense) reads
     * ~0x420D (~16.9V) on stock but ~0x25B7 (~9.6V) on custom - i.e. the 0x16's INPUT rail is too
     * low on custom, so it can only advertise/grant 5V. The rail is the boost bus gated by PB12.
     * max_of_channels drives PB12 from the SW3566 max (v11) ONLY, which is 0 when just C1 is loaded
     * -> boost OFF -> input stays ~9.6V -> chicken-and-egg (PB12 needs C1 already >10V, C1 needs the
     * boost). Break it: keep the boost ON (PB12 high, PB10 low) whenever C1 is enabled, so the 0x16
     * sees a high input and can negotiate 20V on a FRESH attach. PB2 stays v11-only (stock holds it
     * LOW even at C1=20V). */
    { extern volatile uint32_t g_total_power; extern float g_power_w;
      extern uint8_t g_port_enable[]; extern uint8_t g_hipwr2;
      float c1v = (float)(g_total_power & 0xFFFFu) / 1000.0f;  /* C1 reg0x21 voltage = stock's v4 */
      int   c1_on = (g_port_enable[0] && !g_hipwr2);
      /* Stock max_of_channels (0x17D88) keeps PB12 (HV-boost enable) on when total>64W OR SW3566
       * max(v11)>10 OR C1 voltage(v4)>=11. PLUS a fresh-attach BOOTSTRAP: also keep the boost on
       * whenever C1 is enabled (c1_on), so the 0x16's input rail is already elevated when a device
       * attaches and it can advertise/grant 20V from the first negotiation (breaks the chicken-and-egg
       * where v4 only rises after the boost). Earlier "ruled out" tests were all confounded - by the
       * unreliable SWD hijack reads and by software re-attaches that never truly re-attach the sink -
       * so this needs a real PHYSICAL replug to judge. Always-on boost is safe (the SW3566 bucks step
       * down; it's stock's own state at 20V). */
      if (g_power_w <= 64.0f && v11 <= 10u && c1v < 11.0f && !c1_on)
           gpio_clear_pins(1, 0x1000); else gpio_set_pins(1, 0x1000);   /* PB12 boost   */
      if (v11 >= 10u || c1v >= 9.5f || c1_on)
           gpio_clear_pins(1, 0x0400); else gpio_set_pins(1, 0x0400);   /* PB10 low-path */
      if (v11 >= 0x11u && v11 <= 0x1Eu)
           gpio_set_pins(1, 0x0004); else gpio_clear_pins(1, 0x0004);   /* PB2 20V band  */
    }
}
static void power_temp_task(void)
{
    /* Keep the WLAN icon in sync with the connection state. Stock redraws it on each
     * cmd 0x03 (0xc4da); but cmd 0x03 may arrive once, before the home page exists,
     * leaving a stale icon. This runs every loop iteration: redraw whenever
     * g_wlan_conn changes and a page that shows the icon (0..4) is up. */
    static uint8_t s_last_conn = 0xFF;
    if (s_last_conn != g_wlan_conn) {
        s_last_conn = g_wlan_conn;
        /* only once the boot animation is done and a page that shows the icon is up -
         * otherwise the redraw painted the WLAN icon over the boot animation. */
        if (ui_boot_done() && g_menu_page <= 4u) ui_draw_menu();
    }
    if (!tcf(3)) return;
    uint32_t total = 0;            /* power_calc: sum per-port power = total watts */
    int any_ok = 0;                /* did at least one port read succeed this tick? */
    for (uint8_t p = 0; p < g_i2c_port_count && p < WLAN_NUM_PORTS; p++) {
        pd_status_t st;
        if (power_poll(p, &st)) {
            any_ok = 1;
            g_telemetry.port_flag[p] = st.attached;
            g_telemetry.p_x100[p]    = (uint16_t)st.power;
            total += st.power;
        }
    }
    /* Only refresh g_total_power when a read actually succeeded. On a transient all-fail (the bit-bang
     * 0x16 momentarily NACKed even after retries) keep the LAST good value instead of zeroing - this
     * is what was blanking C1 in the debug/home menus on a brief bus hiccup. A real detach reports
     * attached=0 via a SUCCESSFUL read, which sets total=0 here (so genuine 0 W still shows). */
    if (any_ok) g_total_power = total;
    { extern void pd_c1_budget_maintain(void);
      pd_c1_budget_maintain(); }          /* stock ui_draw_icon 0x16f0c: (re)assert 0x16 reg3=240W
                                           * budget so C1 advertises 20V on a fresh attach. Latched -
                                           * no-op unless power_poll cleared it on a C1 attach edge. */
    pd_power_calc();                      /* stock power_calc: g_power_w = sum outlet V*I */
    temp_update();                        /* stock 0x10d4a: NTCs via ADC1 -> g_temp_sensor + g_temp_display.
                                           * MUST precede the over-temp FSM (stock order: power_calc writes
                                           * the temps into g_menu_page[112]/[116], then they're tested). */
    { extern void menu_state_update(void); menu_state_update(); }  /* backend 1:1: g_menu_state incl fresh temps */
    pd_power_level_check();               /* stock: over-TEMP 100/95 C hysteresis on tempA/tempB */

    /* ITEM 6 + 9: keep the cloud/app values LIVE. wlan_dp_report() previously fired only
     * on state changes (DP1/DP2/per-port) + the cmd-0x08 query, so the app cached the
     * boot-time DP4 temperature (32, read while the NTC was still warm/uncalibrated) while
     * the menu shows the live calibrated value (27) - and per-port watts never refreshed.
     * Stock re-reports DPs on change; re-report the full set (rate-limited to 2 s) whenever
     * g_temp_display or the total power changes, once the module is cloud-connected. */
    {
        extern uint32_t g_temp_display; extern uint8_t g_wlan_conn;
        extern void wlan_dp_report(uint8_t);
        static uint32_t s_rep_t = 0xFFFFFFFFu, s_rep_ms = 0u;
        static uint16_t s_rep_w = 0xFFFFu;
        uint16_t pw = (uint16_t)g_power_w;
        uint32_t now = get_tick_ms();
        if (g_wlan_conn && (g_temp_display != s_rep_t || pw != s_rep_w)
            && (uint32_t)(now - s_rep_ms) >= 2000u) {
            s_rep_t = g_temp_display; s_rep_w = pw; s_rep_ms = now;
            wlan_dp_report(0);
        }
    }

    /* case 0 home (stock power_temp_task switch case 0): */
    static uint8_t  s_home_state = 0xFF;       /* g_port_data[0] */
    static uint16_t s_last_watts = 0xFFFF;     /* unk_1FFF8318 */
    static uint8_t  s_last_attach = 0;         /* prev attached-port count (new-device detect) */
    if (g_menu_page == 0 && ui_boot_done()) {   /* not during the boot animation
                                                 * (else the home ring bleeds through
                                                 * behind the boot logo) */
        uint8_t attach = pd_attached_count();
        int ring_restart = 0;                   /* 1 = play the empty->level fill animation ONCE */
        if (g_idle_count > 5u && attach == 0) {
            /* idle + nothing plugged in -> the "TO CHARGE / TO EXPLORE" hint
             * (stock draws g_img_table[34..41]); draw once (cache g_port_data[0]=2). */
            g_clock_mode = 0;     /* release the clock-screensaver suppress so the 60s-idle -> clock
                                   * face fires (was left =1 from a prior charging session). */
            if (s_home_state != 2) {
                for (uint16_t i = 34; i <= 41; i++) ui_draw_img(i);
                s_home_state = 2;
            }
        } else {
            if (s_home_state != g_page_group) {
                /* power-TIER change -> pick the tier colour (idx 1/2/3) + clock-suppress. The new
                 * colour gets a fresh fill (ui_home_ring_update restarts on the idx change). */
                g_clock_mode = 1;
                g_page_anim  = 0;
                g_page_idx   = (uint8_t)(g_page_group + 1);   /* tier 0/1/2 -> idx 1/2/3 */
                s_home_state = g_page_group;
            }
            /* a NEW power-taking device (attach count went UP) replays the fill animation ONCE, in
             * the colour for that device's power tier. (Coming from the idle hint also counts.) */
            if (attach > s_last_attach) ring_restart = 1;
        }
        s_last_attach = attach;
        /* watts NUMBER + ring fill track power: redraw the number; arm the fill. restart=1 only on a
         * new device (full empty->level sweep); else just nudge the level to the current watts. */
        uint16_t w = (uint16_t)g_power_w;
        if (ring_restart || w != s_last_watts) {
            ui_draw_watts(w);
            ui_home_ring_update(w, ring_restart);
            s_last_watts = w;
        }
        /* NOTE: weather temp/location are NOT drawn on the home page. In stock the
         * weather block (0x11b94 temp @176,10 + 0x11bd2 location @10,56) is reached
         * only via the page-14 (clock) dispatch (render fn 0x10f54: cmp g_menu_page,#14).
         * The home (page 0) draws the status bar at (174,10) - drawing the temp there
         * overlapped it. Weather stays on the clock page (ui_draw_clock_extras). */
    }
    if (g_menu_page == 3) {
        /* temp screen: redraw the bar ONLY when the temperature changes (stock gates
         * sub_188DC behind a vcmp of the cached temp). Redrawing every tick re-cleared
         * the tip column each frame -> visible flicker. Page entry (ui_set_page case 3)
         * draws it once; this keeps it live without flicker. */
        extern uint32_t g_temp_display;
        static uint32_t s_t3 = 0xFFFFFFFFu;
        if (g_temp_display != s_t3) { ui_188DC(); s_t3 = g_temp_display; }
    }

    /* case 1/2: live per-port V/I/W. The change-gate is now PER-SLOT inside the draw functions
     * (stock soft_12F98 model): a stable slot is skipped, so its lcd_fill_rect clear never runs
     * and there is no flicker. Just call every iteration; only changed slots actually redraw. */
    if (g_menu_page == 1)      ui_draw_page1_values();
    else if (g_menu_page == 2) ui_draw_page2_values();
}
/* ui_11CA8 (0x11CA8): on the clock page (14) draw HH:MM + the location text. The
 * weather/location text is drawn with the letter font @0x1A9D4 (clockfont.c, stock
 * 0x138E8/0x155E0) - NOT the 0x01000000 path (that was a wrong address; the real
 * font is internal-flash descriptors + ext-flash glyph pixels @0x108349+, all
 * present). g_loc_name comes from the cmd 0x14 reply ("Himmelpforten"). Position is
 * a first cut to refine against stock. */
static void ui_11CA8(void)
{
    if (tcf(6) && g_menu_page == 14 && !(g_clock_mode & 1)) {
        draw_num2digit(g_rtc.hour, g_rtc.min);   /* HH:MM with blinking colon */
        ui_draw_clock_extras();                  /* location text + temp (stock 0x138E8/0x13A14) */
    }
}

/* ui_show_page14 (0x10E88): clock screensaver. Once per second, after 60 s idle,
 * switch to page 14 (the time/clock display) - the "nach etwas warten die uhrzeit"
 * behaviour. unk_1FFF8298 (clock mode) / unk_1FFF8158 (alt) default 0 here. 1:1. */
static void ui_show_page14(void)
{
    extern uint8_t g_anim_active;     /* defined in ui.c */
    extern int ui_is_disp_off(void);
    if (ui_is_disp_off()) return;     /* Standby Mode: LCD asleep -> don't run the clock screensaver */
    if (!g_one_second_flag) return;   /* test_and_clear byte_2000F3D8[0] (1 Hz) */
    g_one_second_flag = 0;
    static uint8_t latch;             /* stock byte_1FFFA5B6[1] (one-shot) */
    if (g_idle_count < 0x3Cu) {       /* < 60 s idle: re-arm */
        latch = 0;
    } else if (!latch) {              /* first crossing of 60 s */
        latch = 1;
        /* Suppress the screensaver ONLY while an animation is genuinely running. Stock keys off
         * unk_1FFF8298 (g_clock_mode) alone, assuming it's reliably 0 when settled - but on a non-home
         * page my g_clock_mode could be left =1, which kept resetting g_idle_count so the clock face
         * only ever appeared from the home screen. Gating on g_anim_active lets it fire on ANY page. */
        if ((g_clock_mode & 1) && g_anim_active) {
            if (!app_1204C()) g_idle_count = 0;
        } else if (g_clock_alt) {
            /* theme B (stock sub_18BF0): play the flash-resident screensaver
             * animation ("2 eyes"). Stock cycles scenes 0/1/2 -> pages 10/11/12;
             * with nothing charging it lands on the animation (vs the static port
             * page). We play slot 0 (page 10) looping. */
            anim_start(0);
        } else {
            ui_set_page(14);          /* show the clock page (theme A) */
        }
    }
}

/* fn_10F24 (0x10F24): periodic keepalive to the WLAN module when it is up.
 * stock uart_12580() sends the 9-byte frame 55 AA 00 00 15 00 00 00 cksum. */
static void fn_10F24(void)
{
    if (!tcf(7)) return;                 /* stock test_and_clear_flag(&byte_2000F3D8[2]) */
    extern volatile uint8_t g_wlan_alive;
    if (g_wlan_conn == 1 || g_wlan_alive)  /* connected, OR module is at least talking */
        wlan_send(0x15, 0, 0, 0);        /* stock uart_12580() */
}

/* ui_124F0 (0x124F0): on page-15/standby, return to home after 120 s idle;
 * otherwise poll the module for status. 1:1 from stock. */
static void ui_124F0(void)
{
    extern volatile uint8_t g_wlan_alive;
    if (!tcf(8)) return;                 /* stock test_and_clear_flag(&byte_2000F3D8[1]) */
    if (g_menu_page == 16) {             /* WiFi-reset / pairing splash (on_wlan_btn_click):
                                          * auto-return home after a short idle (user: it
                                          * "shows way too long" because it had no timeout). */
        if (g_idle_count >= 0x0Au) { g_idle_count = 0; ui_set_page(0); }
        return;
    }
    if (app_1204C() && g_menu_page != DEBUG_PAGE && g_menu_page != GRAPH_PAGE) {
        /* page 15 / standby only - the debug/graph pages must NOT time out to home (stay to watch) */
        if (g_idle_count >= 0x78u) {     /* >= 120 s idle */
            g_idle_count = 0;
            ui_set_page(0);              /* time out back to home */
        }
    } else if (g_wlan_conn == 1 || g_wlan_alive) {
        /* PROACTIVE DP status report (stock ui_124F0 -> 0xbb80): the app only ever
         * learns on/off + mode + port status because the MCU reports it without being
         * asked. Send immediately when on/off or mode changes, and at least every 10 s. */
        extern void wlan_dp_report(uint8_t seq);
        extern int  ui_is_standby(void);
        { static uint8_t s_lo = 0xFF, s_lm = 0xFF; static uint32_t s_ldp;
          uint8_t lo = ui_is_standby() ? 0u : 1u, lm = (uint8_t)g_status_mode;
          if (lo != s_lo || lm != s_lm || (uint32_t)(get_tick_ms() - s_ldp) >= 10000u) {
              s_lo = lo; s_lm = lm; s_ldp = get_tick_ms();
              wlan_dp_report(0);
          } }
        /* Request weather/time whenever the module is at least alive (heartbeating),
         * not only once g_wlan_conn is set. The module only re-reports its wifi status
         * (cmd 0x03) on CHANGE, so a freshly-booted MCU joining an already-connected
         * module never learns it's online and (under the old g_wlan_conn gate) never
         * asked for data -> nothing synced. By requesting directly, the module (if
         * cloud-connected) replies with weather/time, and wlan_mark_connected() then
         * sets g_wlan_conn from the actual data. cmd 0x14 = weather request (stock
         * @0x1255E), cmd 0x1C = time request.
         * RATE-LIMIT to ~every 12 s: sending it every tick (tcf8 ~1/s) floods the
         * module so it can't answer (observed: 1.6/s requests -> module went silent). */
        extern volatile uint8_t g_net_time_valid;
        static uint32_t s_last_req;
        uint32_t now = get_tick_ms();
        if (now - s_last_req >= 12000u) {
            s_last_req = now;
            wlan_send(0x14, 0, 0, 0);                          /* weather */
            if (!g_net_time_valid) {
                wlan_send(WLAN_CMD_TIME_SYNC, 0, 0, 0);             /* 0x1C local time */
                wlan_send(0x15, 0, 0, 0);                      /* 0x15 data-sync (also carries
                                                               * the BE Unix timestamp - may
                                                               * arrive sooner than cmd 0x1C) */
            }
        }
    }
}
