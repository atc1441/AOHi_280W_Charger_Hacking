/* ui.h - menu/page UI engine (clean-room from stock ui_set_page / on_*_btn_*).
 *
 * 21 pages (0..20). Each page clears the screen and draws a list of images
 * streamed from external flash. Navigation is a button-driven state machine. */
#ifndef APP_UI_H
#define APP_UI_H
#include <stdint.h>
#include "ui_state.h"   /* g_menu_page lives at stock addr 0x1FFF8294 */

/* Packed draw descriptor exactly as passed to stock lcd_draw_element:
 *   dim_wh = (h << 16) | w ,  pos_xy = (y << 16) | x  */
typedef struct {
    uint32_t flash_off;
    uint32_t size;
    uint32_t dim_wh;
    uint32_t pos_xy;
} ui_draw_t;

/* g_menu_page is now a fixed-address macro at stock 0x1FFF8294 (see ui_state.h) */
extern uint8_t g_prev_page;   /* page before this one  */
extern uint8_t g_page_idx;    /* sub-index within page */
extern uint8_t g_wlan_conn;   /* stock unk_1FFF818E: WLAN module ready/connected */

void ui_set_page(uint8_t page);
void draw_num2digit(uint8_t a1, uint8_t a2);   /* stock 0x13A64: HH:MM clock draw */
void ui_188DC(void);                           /* stock 0x188DC: 20-segment power bar */
void ui_draw_temp_table(void);                 /* power-page NTC temperature (degC) */
void ui_mark_initialised(void);   /* mark UI initialised at boot (enables button nav) */
int  ui_boot_done(void);          /* 1 once the boot animation has finished */
int  ui_is_standby(void);         /* 1 = charging off (stock byte_2000F3DD) */
void ui_set_charge(int on);       /* set charge on/off (single source of truth) */
void ui_home_check(void);         /* boot animation state machine (stock ui_home_check) */
void apply_images(void);
void ui_draw_watts(uint32_t watts); /* draw total-watts number on the home screen */
void ui_draw_clock_extras(void);    /* clock-page location text + temperature digits */
void ui_draw_clock_date(uint8_t year_byte, uint8_t month, uint8_t day);
void ui_draw_weather_temp(void);    /* outdoor temp digits @176,10 (home + clock pages) */
void ui_draw_menu(void);
void ui_FC80(int y);
void ui_draw_static(void);
void ui_draw_page1_values(void);
void ui_draw_page2_values(void);
void ui_draw_list(const ui_draw_t *list, int n);
void ui_draw_img(uint16_t idx);   /* draw asset g_img_table[idx] at g_img_pos[idx] */
void ui_animate_page(void);       /* per-tick page slide/element animation */
void ui_home_ring_update(uint16_t watts, int restart); /* fill to power-proportional level; restart=1 =
                                                        * fresh empty->level sweep (new device/tier) */
void mode_select(uint8_t mode);   /* select power mode 0/1/2 (Raserei/Smart/Individual) */
void mode_tick(void);             /* per-tick: drive the mode animation + auto-home */
void mode_stop(void);             /* cancel the mode display (called on nav) */

/* Button event handlers (wire to hal/buttons callbacks). */
void on_menu_btn_click(void);
void on_menu_btn_doubleclick(void);
void on_menu_btn_longpress(void);
void on_wlan_btn_click(void);
void on_wlan_btn_longpress(void);

#endif /* APP_UI_H */
