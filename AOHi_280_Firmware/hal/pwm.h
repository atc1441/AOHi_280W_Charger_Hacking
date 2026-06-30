/* pwm.h - hardware PWM timer @0x40018000 (clean-room from stock backlight_pwm_init
 * / sub_D44C and its helpers pwm_clk_enable/pwm_apply_timer_cfg/pwm_apply_chan_cfg/
 * pwm_set_reload/pwm_chan_output_enable/pwm_timer_start).
 *
 * The stock backlight runs on this hardware PWM (port0 pin7 / 0x80, AF3, ~200kHz,
 * ~50% duty) rather than the SysTick GPIO toggle the early SDK used. */
#ifndef HAL_PWM_H
#define HAL_PWM_H
#include <stdint.h>

/* Bring up the backlight PWM exactly like stock backlight_pwm_init (sub_D44C):
 * enable the timer clock, route port0 pin7 to AF3, program a ~200kHz period at
 * 50% duty, enable the channel output and start the timer. */
void pwm_backlight_init(void);

/* Set backlight duty 0..255 (0=off, 255=full) by adjusting the compare value. */
void pwm_backlight_set(uint8_t level);

/* Turn the backlight on at the init-configured duty (~62%, stock brightness).
 * pwm_backlight_init leaves PA7 GPIO-high (off, active-low driver); call this after the boot black-fill. */
void pwm_backlight_on(void);

/* setBlPWM: backlight brightness 0..100 % via the PWM reload/period. pct==0 also detaches PA7 to a
 * GPIO driven HIGH (backlight fully off, active-low driver); pct>0 re-routes PA7 to the running PWM. */
void setBlPWM(uint8_t pct);

#endif /* HAL_PWM_H */
