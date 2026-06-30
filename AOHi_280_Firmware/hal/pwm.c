/* pwm.c - backlight = Timer6 unit-1 PWM on PA7 (AF3), HC32F460.
 *
 * Byte-exact replica of the stock backlight_pwm_init (0xD44C) and its helpers
 * (pwm_apply_timer_cfg/pwm_set_reload/pwm_apply_chan_cfg/pwm_chan_output_enable/
 * pwm_timer_start), reverse-engineered from the stock firmware in IDA. The LCD
 * backlight driver needs the switching PWM signal - a static GPIO high does not
 * light it. Clock-gate + pin-AF go through the DDL; the TMR61 register sequence
 * is replicated exactly (verified against the stock decompile). */
#include "pwm.h"
#include <hc32_ddl.h>
#include <hc32f460_gpio.h>
#include <hc32f460_pwc.h>

extern uint32_t SystemCoreClock;

#define TMR61_BASE   0x40018000u
#define T6(off)      (*(volatile uint32_t *)(TMR61_BASE + (off)))

void pwm_backlight_init(void)
{
    /* pwm_clk_enable: stock 0x40048008 &= ~0x10000 == PWC FCG2 TIM61 enable */
    PWC_Fcg2PeriphClockCmd(PWC_FCG2_PERIPH_TIM61, Enable);

    /* Configure the Timer6 PWM (~62% duty) but DO NOT route PA7 to it yet: keep the
     * backlight OFF (PA7 GPIO-low) through the whole bring-up. The stock bootloader
     * handed the panel over with GRAM already black, so stock could leave the light
     * on; our from-scratch bootloader does NOT pre-black the panel, so a lit backlight
     * during init shows the stale/garbage GRAM as noise. fw_main routes PA7 to the PWM
     * (pwm_backlight_on / lcd_backlight(1)) only AFTER the black fill.
     * The backlight driver is active-low, so OFF = PA7 GPIO driven HIGH. */
    PORT_SetFunc(PortA, Pin07, Func_Gpio, Disable);
    PORT_SetBits(PortA, Pin07);                       /* PA7 high = backlight OFF during init */

    uint32_t presc = (*(volatile uint32_t *)0x40054020u >> 24) & 7u;
    uint32_t v6 = (SystemCoreClock >> presc) / 200000u;
    if (!v6) v6 = 320u;

    /* pwm_apply_timer_cfg (defaults, byte0=0 -> ctrl branch): */
    T6(0x50) = (T6(0x50) & 0xFFFFFE81u) | 0x120u;   /* count mode/ctrl bits */
    T6(0x04) = v6 - 20u * v6 / 100u;                 /* stock pwm_set_reload(v3 - 20*v3/0x64) */
    T6(0x14) = v6 >> 1;                              /* stock duty: v0[0] = v3 >> 1 (~62% of PERAR) */
    /* pwm_apply_chan_cfg: channel mode bits (cfg {[1]=1,[2]=1,[3]=0,[4]=1,[5]=0} -> 0x460000) */
    T6(0x58) = (T6(0x58) & 0xFF00FFFFu) | 0x460000u;
    /* pwm_chan_disable then pwm_chan_output_enable (bit24): */
    T6(0x58) &= ~0x10000u;
    T6(0x58) = (T6(0x58) & 0xFEFFFFFFu) | 0x1000000u;
    /* pwm_timer_start: */
    T6(0x50) |= 1u;
}

/* setBlPWM: backlight brightness 0..100 % via the PWM RELOAD/period (pwm_set_reload(v6 - pct*v6/100));
 * higher pct = shorter period = brighter. lcd_sleep uses setBlPWM(0) on display-off, setBlPWM(20) on
 * wake. NOTE vs stock: changing only the period leaves the compare fixed, so setBlPWM(0) still left
 * PA7 toggling at ~50% duty -> the backlight kept glowing. So pct==0 now also detaches PA7 to a
 * GPIO driven HIGH (the driver is active-low, so high = electrically off), and pct>0 re-routes PA7
 * back to the running PWM. */
void setBlPWM(uint8_t pct)
{
    if (pct > 100u) pct = 100u;
    if (pct == 0u) {
        PORT_SetFunc(PortA, Pin07, Func_Gpio, Disable);  /* detach PA7 from the PWM */
        PORT_SetBits(PortA, Pin07);                       /* PA7 HIGH = backlight fully OFF (driver is active-low) */
        return;
    }
    uint32_t presc = (*(volatile uint32_t *)0x40054020u >> 24) & 7u;
    uint32_t v6 = (SystemCoreClock >> presc) / 200000u;
    if (!v6) v6 = 320u;
    T6(0x04) = v6 - (uint32_t)pct * v6 / 100u;            /* pwm_set_reload */
    PORT_SetFunc(PortA, Pin07, Func_Tim6, Disable);      /* route PA7 back to the PWM */
}

/* Duty 0..255 -> compare at +0x14 (period base). 0 = off, 255 = full.
 * PA7 is only handed to Timer6 when level>0 (after the boot black-fill); level==0 forces
 * PA7 back to a GPIO driven HIGH so the (active-low) backlight driver is electrically off (item 5). */
void pwm_backlight_set(uint8_t level)
{
    if (level == 0u) {
        PORT_SetFunc(PortA, Pin07, Func_Gpio, Disable);   /* detach from PWM */
        PORT_SetBits(PortA, Pin07);                       /* PA7 high = backlight OFF (active-low driver) */
        T6(0x14) = 0u;
        return;
    }
    uint32_t presc = (*(volatile uint32_t *)0x40054020u >> 24) & 7u;
    uint32_t v6 = (SystemCoreClock >> presc) / 200000u;
    if (!v6) v6 = 320u;
    T6(0x14) = (uint32_t)level * v6 / 255u;               /* duty first */
    PORT_SetFunc(PortA, Pin07, Func_Tim6, Disable);       /* THEN route PA7 to the PWM */
}

/* Turn the backlight on at the duty pwm_backlight_init already configured (~62%,
 * stock brightness): just route PA7 to the running Timer6 PWM. Called once after
 * the boot black-fill so the bring-up runs dark (no GRAM-noise). */
void pwm_backlight_on(void)
{
    PORT_SetFunc(PortA, Pin07, Func_Tim6, Disable);
}
