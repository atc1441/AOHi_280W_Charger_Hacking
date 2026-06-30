/* tick.h - millisecond system tick + software timers (from stock SysTick). */
#ifndef LIB_TICK_H
#define LIB_TICK_H
#include <stdint.h>

#define TICK_NUM_TIMERS 16

/* Configure SysTick for a 1 ms tick. core_clock_hz = SystemCoreClock. */
void     tick_init(uint32_t core_clock_hz);
uint32_t get_tick_ms(void);

/* Software countdown timer: when it reaches 0, *flag is set to 1 and (if
 * periodic) it reloads. Mirrors the stock 16-slot timer table. */
int  timer_start(uint8_t slot, uint32_t ms, uint32_t reload, volatile uint8_t *flag);
void timer_stop(uint8_t slot);

extern volatile uint32_t g_tick_ms;
extern volatile uint32_t g_tick_inc;
extern volatile uint8_t  g_one_second_flag;   /* set once per second */
extern volatile uint32_t g_idle_count;        /* stock dword_20014774: ++/second */

#endif /* LIB_TICK_H */
