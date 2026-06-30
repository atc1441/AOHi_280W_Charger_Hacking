/* menu_state.h - the stock unified control-state struct g_menu_page[] (124 bytes).
 *
 * BACKEND 1:1 RECONSTRUCTION (task #29): stock drives the WHOLE control algorithm from this one
 * struct, snapshotted to byte_1FFF8310 each tick by power_calc (memmove 0x7C). My SDK had scattered
 * globals (g_power_w / g_telemetry / g_page_group / mode_*) which is why the FSM "didn't fit" stock.
 * Layout reverse-engineered from power_calc (0x1098c) + power_temp_task (0x10f54):
 *
 *   [0] page  [1] prev  [3] dirty  [4] anim_active  [5] anim_idx  [6] transition
 *   [8] (u16) total watts
 *   per outlet (16 bytes): +0 power(float)  +4 attach(u8)  +8 volt(float)  +12 cur(float)
 *     C1@16  C2@32  C3@48  C4@64  USB-A1@80  USB-A2@96
 *   [112] (float) temp A   [116] (float) temp B   (over-temp FSM 100/95C)
 *   [120] high-power flag   [121] fire-shown latch   [122] power level 0/1/2
 */
#ifndef MENU_STATE_H
#define MENU_STATE_H
#include <stdint.h>

typedef struct {            /* +0  */
    float    power;
    uint8_t  attach;        /* +4  */
    uint8_t  _p5, _p6, _p7;
    float    volt;          /* +8  */
    float    cur;           /* +12 */
} menu_outlet_t;            /* 16 bytes, matches stock per-outlet stride */

typedef struct {
    uint8_t  page;          /* +0   current page (ui_set_page a1)            */
    uint8_t  prev;          /* +1   previous page                            */
    uint8_t  _p2;
    uint8_t  dirty;         /* +3   set on ui_set_page                       */
    uint8_t  anim_active;   /* +4   transition/animation in progress         */
    uint8_t  anim_idx;      /* +5   animation / sub-page index               */
    uint8_t  transition;    /* +6   transition flag                          */
    uint8_t  _p7;
    uint16_t total_w;       /* +8   total watts (fn_18C8C source)            */
    uint8_t  _p10[6];
    menu_outlet_t outlet[6];/* +16  C1,C2,C3,C4,USB-A1,USB-A2                */
    float    tempA;         /* +112 NTC for over-temp FSM (100/95 C)         */
    float    tempB;         /* +116                                          */
    uint8_t  hi_power;      /* +120 high-power flag (drives fire ui_18CE4(7))*/
    uint8_t  fire_latch;    /* +121 fire-shown once latch                    */
    uint8_t  level;         /* +122 power level 0/1/2 (watts >=13/6-12/<=5)  */
    uint8_t  _p123;
} menu_state_t;             /* 124 bytes (0x7C) - matches stock g_menu_page  */

extern menu_state_t g_menu_state;        /* live working state (stock g_menu_page) */
extern menu_state_t g_menu_state_snap;   /* previous-frame snapshot (stock byte_1FFF8310) */

void menu_state_update(void);            /* populate g_menu_state from telemetry (stock power_calc tail) */

#endif
