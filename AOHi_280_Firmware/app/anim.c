/* anim.c - clock-screensaver frame animation (theme B "2 eyes"). See anim.h.
 *
 * The eye frames are real g_img_table entries in the PRESENT image flash (0..6MB),
 * NOT the empty flash region at 0x1000000 (proven 0xFF on the live device). g_img_table
 * contains several full-screen animation runs (same w/h/pos, consecutive offsets):
 *   [799..837] 39 frames 200x480 @(0,0)  off 0x1CED61   <- primary "eyes" candidate
 *   [849..863] 15 frames 200x480 @(0,0)  off 0x4FFE34
 *   [779..798] 20 frames 100x230 @(50,126)
 * We play a range directly with ui_draw_img (g_img_table[idx] @ g_img_pos[idx]),
 * looping, the same way the stock animators (sub_18D28 / sub_18F84) blit g_img_table
 * frames. If the chosen range is the wrong animation, just change EYE_START/EYE_END. */
#include "anim.h"
#include "ui.h"           /* ui_draw_img */
#include "display.h"      /* lcd_fill_rect */
#include "tick.h"
#include "ui_state.h"     /* g_menu_page / g_clock_mode */

#define EYE_START   740u
#define EYE_END     766u
#define ANIM_FRAME_MS 70u

uint8_t g_anim_playing;
static uint16_t s_cur;
static uint32_t s_last_ms;

/* DIAG at a fixed unused .stock_ram address (zeroed at boot). Read over J-Link:
 * flash.ps1 -Action read -Addr 0x1FFF86C0 -Size 0x20. */
#define ANIM_DBG  ((volatile uint8_t *)0x1FFF86C0u)

void anim_load_all(void)
{
    ANIM_DBG[0] = 0xA5;      /* ran-marker (no flash load needed - frames are g_img_table) */
}

void anim_start(uint8_t slot)
{
    (void)slot;
    s_cur = EYE_START;
    s_last_ms = 0;
    g_anim_playing = 1;
    ANIM_DBG[16] = 1;        /* anim_start reached */
    /* claim the screen: page 14 + clock-suppress so per-tick home/clock drawers
     * (power_temp_task page-0 block, ui_11CA8 HH:MM) don't overdraw the animation. */
    g_menu_page  = 14;
    g_clock_mode = 1;
    lcd_fill_rect(0, 0, 200, 480, 0x0000);
}

void anim_stop(void) { g_anim_playing = 0; }

void anim_tick(void)
{
    if (!g_anim_playing) return;
    uint32_t now = get_tick_ms();
    if (now - s_last_ms < ANIM_FRAME_MS) return;
    s_last_ms = now;

    ui_draw_img(s_cur);                          /* g_img_table[s_cur] @ g_img_pos[s_cur] */
    *(volatile uint16_t *)(ANIM_DBG + 18) =
        (uint16_t)(*(volatile uint16_t *)(ANIM_DBG + 18) + 1);   /* draw count */

    if (++s_cur > EYE_END) s_cur = EYE_START;    /* loop */
}
