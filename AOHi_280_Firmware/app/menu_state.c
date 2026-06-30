/* menu_state.c - populate the unified stock control-state struct (backend 1:1, task #29).
 *
 * Step 1 of the FSM reconstruction: build/keep g_menu_state correct from the live telemetry,
 * exactly as stock power_calc fills g_menu_page. This is ADDITIVE - the existing control paths
 * still run; subsequent steps migrate each FSM transition (over-temp, high-power fire, idle
 * dispatch, page-transition) to read/write g_menu_state instead of the scattered globals, until
 * power_temp_task is byte-for-byte the stock FSM. */
#include "menu_state.h"
#include "../hal/power.h"
#include "ui.h"          /* g_menu_page macro (ui_state.h) */
#include "wlan.h"        /* g_telemetry */
#include <string.h>

/* compile-time guarantee the layout matches stock's 124-byte g_menu_page */
typedef char _menu_state_size_check[(sizeof(menu_state_t) == 124) ? 1 : -1];

menu_state_t g_menu_state;
menu_state_t g_menu_state_snap;

extern volatile uint32_t g_total_power;   /* C1/240W packed V(LOWORD mV)+I(HIWORD mA) */
extern float    g_power_w;                /* total apparent power (W) */
extern float    g_temp_sensor[2];
/* g_menu_page is a volatile-memory accessor MACRO from ui_state.h (not a plain global). */

/* stock power_calc tail: snapshot previous frame, then recompute the live state. */
void menu_state_update(void)
{
    memcpy(&g_menu_state_snap, &g_menu_state, sizeof(g_menu_state));
    menu_state_t *s = &g_menu_state;

    s->page    = g_menu_page;
    s->total_w = (uint16_t)(g_power_w + 0.5f);

    /* C1 = 240W chip (g_total_power packs V in the low word [mV], I in the high word [mA]). */
    uint32_t gtp = g_total_power;
    s->outlet[0].volt   = (float)(gtp & 0xFFFFu) / 1000.0f;
    s->outlet[0].cur    = (float)(gtp >> 16)     / 1000.0f;
    s->outlet[0].power  = s->outlet[0].volt * s->outlet[0].cur;
    s->outlet[0].attach = (gtp != 0u);

    /* C2/C3/C4 + USB-A1/A2 = g_telemetry[0..4] (x100 fixed-point). */
    for (int p = 0; p < 5; p++) {
        menu_outlet_t *o = &s->outlet[p + 1];
        o->volt   = (float)g_telemetry.v_x100[p] / 100.0f;
        o->cur    = (float)g_telemetry.i_x100[p] / 100.0f;
        o->power  = (float)g_telemetry.p_x100[p] / 100.0f;
        o->attach = g_telemetry.port_flag[p];
    }

    /* temps (stock [112]/[116] = the two NTCs, used by the over-temp FSM). */
    s->tempA = g_temp_sensor[1];
    s->tempB = g_temp_sensor[0];

    /* power level 0/1/2 (stock power_calc tail: watts >=13 ->0, 6..12 ->1, <=5 ->2). */
    uint16_t w = s->total_w;
    s->level   = (w >= 13u) ? 0u : (w >= 6u) ? 1u : 2u;
    s->hi_power = (w >= 100u);              /* stock g_menu_page[120] hi-power flag (100/95 W) */
}
