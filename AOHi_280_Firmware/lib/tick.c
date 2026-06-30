/* tick.c - SysTick-driven ms tick + software timers (clean-room from stock). */
#include "tick.h"
#include <hc32_ddl.h>

volatile uint32_t g_tick_ms;
volatile uint32_t g_tick_inc = 1;
volatile uint8_t  g_one_second_flag;
volatile uint32_t g_idle_count;          /* stock dword_20014774: ++ every second */

typedef struct {
    volatile uint32_t  count;     /* current countdown               */
    uint32_t           reload;    /* reload value (0 = one-shot)     */
    volatile uint8_t  *flag;      /* set to 1 on expiry              */
    uint8_t            active;
} sw_timer_t;

static sw_timer_t s_timers[TICK_NUM_TIMERS];
static volatile uint16_t s_sec_div = 1000;

void tick_init(uint32_t core_clock_hz)
{
    SysTick->LOAD = (core_clock_hz / 1000u) - 1u;   /* 1 ms */
    SysTick->VAL  = 0;
    SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_TICKINT_Msk | SysTick_CTRL_ENABLE_Msk;
}

uint32_t get_tick_ms(void) { return g_tick_ms; }

int timer_start(uint8_t slot, uint32_t ms, uint32_t reload, volatile uint8_t *flag)
{
    if (slot >= TICK_NUM_TIMERS) return -1;
    s_timers[slot].count  = ms;
    s_timers[slot].reload = reload;
    s_timers[slot].flag   = flag;
    s_timers[slot].active = 1;
    return 0;
}

void timer_stop(uint8_t slot)
{
    if (slot < TICK_NUM_TIMERS) s_timers[slot].active = 0;
}

/* Strong override of the weak SysTick_Handler ALIAS(Default_Handler) in
 * startup_hc32f460.c. MUST be strong: if left weak, the linker may bind the
 * vector to Default_Handler (an infinite loop), so the first SysTick tick after
 * tick_init() jumps into an endless loop and the whole firmware freezes. */
void SysTick_Handler(void)
{
    g_tick_ms += g_tick_inc;

    for (int i = 0; i < TICK_NUM_TIMERS; i++) {
        if (s_timers[i].active && s_timers[i].count) {
            if (--s_timers[i].count == 0) {
                if (s_timers[i].flag) *s_timers[i].flag = 1;
                if (s_timers[i].reload) s_timers[i].count = s_timers[i].reload;
                else s_timers[i].active = 0;
            }
        }
    }

    if (--s_sec_div == 0) {
        g_one_second_flag = 1;
        g_idle_count++;                  /* stock 0xF81E: ++dword_20014774 */
        s_sec_div = 1000;
    }

    /* (Removed) The 8 ms SysTick 0x16 keepalive: it was based on a "reg1 is a fast-decaying watchdog"
     * theory that was later DISPROVEN - the 0x16 holds its PD contract autonomously (SWD-verified
     * across a 3s CPU halt with the keepalive fully stopped). Worse, having the ISR drive bit-bang I2C
     * transactions on the same PC13/PF2 bus the main loop reads from intermittently corrupted/NACKed
     * the C1 status reads ("C1 data disappears"). C1 20V comes from the reg3=240000 budget write
     * (pd_c1_budget_maintain), not from keepalive rate; the main-loop charge_keepalive is sufficient. */
    __DSB();
}
