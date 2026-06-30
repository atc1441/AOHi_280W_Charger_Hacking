/* ui.c - menu/page engine (clean-room from stock ui_set_page / on_menu_btn_*).
 *
 * Page rendering: each page fills the screen black then streams a fixed list of
 * images from external flash to absolute screen positions. The draw descriptors
 * (flash offset, byte size, packed w/h, packed x/y) are the values used by the
 * stock firmware. Navigation reproduces the stock button state machine.
 */
#include "ui.h"
#include "anim.h"
#include "render.h"
#include "display.h"
#include "buttons.h"
#include "assets.h"
#include "power.h"                     /* g_power_w, pd_attached_count */
#include "video.h"                     /* internal video page (rickroll) */
#include "dbgmenu.h"                    /* debug page (live telemetry, id 30) */
#include "graphmenu.h"                  /* history graph page (id 31)        */

/* page-group / clock-suppress / anim flags (defined in fw_main.c) */
extern uint8_t g_page_group;  /* g_clock_mode/g_page_anim now in ui_state.h */
#include "extflash.h"
#include "charge.h"
#include "wlan.h"
#include "uart.h"
#include "tick.h"
#include "wdt.h"                        /* wdt_feed for the splash-page blocking delays */
#include "rtc.h"
#include "spi.h"
#include "render_num.h"
#include "clockfont.h"

/* Power/UI state machine flags (stock byte_2000F3DD = standby, unk_2000F3E0 =
 * first-press/initialised). */
static uint8_t s_standby;
static uint8_t s_initialised;
static uint8_t s_disp_off;     /* Standby Mode: LCD asleep (ports keep running); next press wakes it */

/* Boot-animation state (stock anim_state[0] @0x1FFF8048): frames 0..ANIM_END. */
#define ANIM_END   33u
static uint16_t s_anim_frame;
static uint32_t s_anim_last;

/* Clear all per-port readings (stock sub_12E78). */
static void ports_clear(void)
{
    for (int p = 0; p < WLAN_NUM_PORTS; p++) {
        g_telemetry.port_flag[p] = 0;
        g_telemetry.v_x100[p] = 0;
        g_telemetry.i_x100[p] = 0;
        g_telemetry.p_x100[p] = 0;
    }
}

/* Image-set directory caches, one per upload slot (stock loads these from
 * ext-flash to fixed RAM at 0x1FFFC628.. on apply_images; we use managed
 * buffers instead). Size = stock inter-cache stride 0xF00. */
#define IMG_DIR_SIZE 0xF00
static uint8_t s_img_dir[4][IMG_DIR_SIZE];

void apply_images(void)
{
    static const uint32_t slot_base[4] = {
        EXTFLASH_IMG_SLOT1, EXTFLASH_IMG_SLOT2, EXTFLASH_IMG_SLOT3, EXTFLASH_IMG_SLOT4
    };
    for (int i = 0; i < 4; i++)
        extflash_read(slot_base[i], s_img_dir[i], IMG_DIR_SIZE);
}

/* Draw image asset `idx` from g_img_table at its g_img_pos position. */
void ui_draw_img(uint16_t idx)
{
    if (idx >= IMG_COUNT) return;
    const img_entry_t *e = &g_img_table[idx];
    uint32_t p = g_img_pos[idx];
    lcd_draw_element(e->flash_off, e->data_size, e->width, e->height,
                     (uint16_t)(p & 0xFFFF), (uint16_t)(p >> 16));
}

/* Big-digit clock font (stock descriptor table @flash 0x1E7FC: 10 glyphs, 88x50,
 * pixel data in external flash). Extracted verbatim from IDA. */
static const img_entry_t g_clock_font[10] = {
    { 0x00103B7D, 0x078A, 88, 50, 1 }, { 0x00104307, 0x0317, 88, 50, 1 },
    { 0x0010461E, 0x0777, 88, 50, 1 }, { 0x00104D95, 0x0791, 88, 50, 1 },
    { 0x00105526, 0x05AC, 88, 50, 1 }, { 0x00105AD2, 0x0757, 88, 50, 1 },
    { 0x00106229, 0x0868, 88, 50, 1 }, { 0x00106A91, 0x044A, 88, 50, 1 },
    { 0x00106EDB, 0x0955, 88, 50, 1 }, { 0x00107830, 0x073D, 88, 50, 1 },
};
#define CLOCK_COLON_IDX 662            /* g_img_table[662] = the ":" glyph (stock 0x2960/16) */

/* draw_num2digit (stock 0x13A64): render the HH:MM clock - two 2-digit groups
 * stacked along the (rotated) long axis with a blinking colon between them. 1:1.
 * a1 = top group (hours), a2 = bottom group (minutes). */
void draw_num2digit(uint8_t a1, uint8_t a2)
{
    static uint8_t colon_on;                 /* stock byte_1FFFC624 */
    const uint16_t X = 72, BASE = 62, DH = 50;
    const img_entry_t *col = &g_img_table[CLOCK_COLON_IDX];

    uint8_t g1[2] = { (uint8_t)(a1 / 10), (uint8_t)(a1 % 10) };
    for (int i = 0; i < 2; i++) {
        const img_entry_t *g = &g_clock_font[g1[i]];
        uint16_t y = (uint16_t)(BASE + DH * (i + 1) + (i ? 10 : 0));   /* 112, 172 */
        lcd_draw_element(g->flash_off, g->data_size, g->width, g->height, X, y);
    }

    /* blinking colon at y = BASE+170 (= 232) */
    if (colon_on == 1) {
        lcd_draw_element(col->flash_off, col->data_size, col->width, col->height,
                         X, (uint16_t)(BASE + 170));
        colon_on = 0;
    } else {
        lcd_fill_rect(X, BASE + 170, col->width, col->height, 0x0000);  /* sub_FBB4 */
        colon_on = 1;
    }

    uint8_t g2[2] = { (uint8_t)(a2 / 10), (uint8_t)(a2 % 10) };
    for (int j = 0; j < 2; j++) {
        const img_entry_t *g = &g_clock_font[g2[j]];
        uint16_t v11 = (uint16_t)(BASE + col->height + DH * (j + 1));
        uint16_t y = (uint16_t)(v11 + (j ? 140 : 130));               /* ~292, 352 */
        lcd_draw_element(g->flash_off, g->data_size, g->width, g->height, X, y);
    }
}

/* g_menu_page relocated to stock RAM @0x1FFF8294 (ui_state.h macro) */
uint8_t g_prev_page;
uint8_t g_page_idx;

/* Total power (watts) computed by the telemetry poll (power.c). */
extern volatile uint32_t g_total_power;

/* sub_188DC (ui_draw_main): the 20-segment TEMPERATURE bar-graph (NOT power - the
 * stock temp_update @0x10d4a writes the temperature float to [0x1FFF8304]/[0x8308],
 * which is exactly what this widget reads). Level = temp/5 (capped 20); segments
 * x=10, 70x16 at y=24*i+4 are filled up to the level with a colour band (green <=5,
 * yellow 6..15, red 16..20) and empty above. A level-tip indicator sits at x=80 and
 * the numeric degC readout at (108,470). Glyph offsets verbatim from IDA. 1:1. */
void ui_188DC(void)
{
    extern uint32_t g_temp_display;               /* calibrated NTC temperature (degC) */
    uint32_t val = g_temp_display;
    uint8_t lvl = (uint8_t)(val / 5u);            /* stock: umull 0x33333334 == /5 */
    if (lvl >= 21u) lvl = 20;

    lcd_fill_rect(80, 10, 16, 470, 0x0000);       /* sub_FBB4: clear the tip column */

    /* level-tip indicator (zone-dependent glyph), at x=80, y=24*lvl-14 */
    if (lvl >= 1) {
        uint16_t ty = (uint16_t)(24 * lvl - 14);
        if (lvl <= 5) {
            lcd_draw_element(0xE7758, 0x17C, 16, 18, 80, ty);
            lcd_draw_element(0xE8741, 0x2A9, 20, 20, 140, 10);
        } else if (lvl <= 15) {
            lcd_draw_element(0xE75DC, 0x17C, 16, 18, 80, ty);
            lcd_fill_rect(140, 10, 20, 20, 0x0000);
            lcd_draw_element(0xE836F, 0x169, 20, 18, 140, 10);
        } else {
            lcd_draw_element(0xE7470, 0x16C, 16, 18, 80, ty);
            lcd_draw_element(0xE84D8, 0x269, 22, 20, 140, 10);
        }
    }

    /* the 20 bar segments (filled up to lvl with the zone colour, empty above) */
    for (int i = 0; i <= 19; i++) {
        uint16_t y = (uint16_t)(24 * i + 4);
        if (i >= lvl)        lcd_draw_element(0xE78D4, 0x01B, 70, 16, 10, y);  /* empty   */
        else if (lvl <= 5)   lcd_draw_element(0xE7EEF, 0x480, 70, 16, 10, y);  /* green   */
        else if (lvl <= 15)  lcd_draw_element(0xE7B2F, 0x3C0, 70, 16, 10, y);  /* yellow  */
        else                 lcd_draw_element(0xE78EF, 0x240, 70, 16, 10, y);  /* red     */
    }

    /* numeric degC readout (stock 0x188dc -> sub_13A14 @0x18922): temp, 3 digits,
     * x=108,y=470, digTable=&g_img_table[904] (w42h28), unit=&g_img_table[593] (degC).
     * sub_13A14 swaps its glyph args, so the caller's [sp+0]=[904] is the digit font
     * and [sp+4]=[593] the unit (see ui_draw_weather_temp). */
    render_num_x((uint16_t)val, 3, 0, 0, 0, 0, 0, 108, 470,
                 &g_img_table[904], &g_img_table[593]);
}

/* Draw the home total-watts number. Stock (ui_set_page case 0):
 *   sub_13A14(value, 3, 86, 294, &g_img_table[874], &g_img_table[548])
 * -> render_num with digit glyphs g_img_table[874..883] (34x22 each), drawn at
 * x=86 stacked vertically (pitch = 22), bottom-aligned/centred near y=294 (the
 * UI is rotated 90deg). Leading zeros suppressed. */
/* WLAN connection flag (stock unk_1FFF818E, set by wlan_cmd_03). */
uint8_t g_wlan_conn;

/* sub_FC80 (ui_FC80): draw the 12x2 separator line at (36, y) between per-port
 * numbers on pages 1/2. Pixel data is flash 0x1E4AC: row0 = 0xB294 (grey) x12,
 * row1 = black x12. Bytes are sent in flash (wire) order. */
void ui_FC80(int y)
{
    static const uint8_t sep[48] = {
        0x94,0xB2, 0x94,0xB2, 0x94,0xB2, 0x94,0xB2, 0x94,0xB2, 0x94,0xB2,
        0x94,0xB2, 0x94,0xB2, 0x94,0xB2, 0x94,0xB2, 0x94,0xB2, 0x94,0xB2,
        0,0, 0,0, 0,0, 0,0, 0,0, 0,0, 0,0, 0,0, 0,0, 0,0, 0,0, 0,0
    };
    /* x=36 (stock); FB-x is the SCREEN-VERTICAL axis (raising it moved the line UP, wrong way).
     * "Right" on screen = the y argument (FB-y) - callers pass a y between the V and A baselines. */
    lcd_set_window(36, y, 12, 2);
    lcd_start_ramwr();
    spi0_write(sep, 48);
    lcd_end_ramwr();
}

/* Top status-bar mode (stock byte_1FFF8159: 0=normal, 1/2=alt states). */
/* g_status_mode -> stock RAM 0x1FFF8159 (ui_state.h) */

/* sub_11EE4 (ui_draw_static): top status-bar image at (174,10), by mode.
 * mode0 -> 0x5F7102 (18x26), mode1 -> 0x5F72FB 0x2AD (18x24),
 * mode2 -> 0x5F75A8 0x243 (18x22). (mode0 size is in external flash; use a
 * generous bound - the decoder stops at h rows so extra source is unused.) */
void ui_draw_static(void)
{
    if (g_status_mode == 1)
        lcd_draw_element(0x5F72FBu, 0x2ADu, 18, 24, 174, 10);
    else if (g_status_mode == 2)
        lcd_draw_element(0x5F75A8u, 0x243u, 18, 22, 174, 10);
    else
        lcd_draw_element(0x5F7102u, 0x300u, 18, 26, 174, 10);
}

/* sub_11FA4 (ui_draw_menu): WLAN signal icon at (174,446), 18x24 (pages <= 4).
 * Connected -> g_img 0xDA531; not connected -> 0xDA2BF (size 0x272). */
void ui_draw_menu(void)
{
    if (g_menu_page > 4) return;
    if (g_wlan_conn == 1)
        lcd_draw_element(0xDA531u, 0x219u, 18, 24, 174, 446);
    else
        lcd_draw_element(0xDA2BFu, 0x272u, 18, 24, 174, 446);
}

/* Home total-watts: stock sub_13A14(value, 3, 86, 294, &g_img_table[874],
 * &g_img_table[548]) -> render_num_x integer + unit glyph 548. */
void ui_draw_watts(uint32_t watts)
{
    if (watts > 999) watts = 999;
    render_num_x((uint16_t)watts, 3, 0, 0, 0, 0, 0, 86, 294,
                 &g_img_table[874], &g_img_table[548]);
}

/* Clock-page (14) extras drawn beside HH:MM: the location name (letter font) and
 * the outdoor temperature digits. 1:1 from stock ui_set_page case 14 (0x14A8C):
 *   - text:  0x138E8(g_loc_name, x=10, y=56)                 -> clock_draw_text
 *   - temp:  0x13A14(g_loc_temp, 2 digits, x=176, y=10,
 *                    digTable=&g_img_table[661], unit=&g_img_table[914])
 * g_loc_name/g_loc_temp come from the cmd 0x14 reply ("#Himmelpforten#24#1003"). */
/* Weather (outdoor) temperature digits at x=176,y=10. Stock sub_13A14 SWAPS its
 * two glyph args before calling 0x150b4: the caller's [sp+0] becomes the digit
 * table and [sp+4] the unit glyph. Stock passes [sp+0]=g_img_table[914] (the 0-9
 * temp font, w20h14) and [sp+4]=g_img_table[661] (the degC unit, w20h16). So the
 * real digTable=[914], unitGlyph=[661]. Drawn on BOTH the home (page 0, stock
 * 0x11b94) and the clock page (page 14, stock 0x14acc). */
void ui_draw_weather_temp(void)
{
    extern volatile uint8_t g_loc_valid;
    if (!g_loc_valid) return;                    /* g_loc_temp = ui_state.h macro */
    render_num_x((uint16_t)g_loc_temp, 2, 0, 0, 0, 0, 0, 176, 10,
                 &g_img_table[914], &g_img_table[661]);
}

/* Clock-page DATE block (stock 0x13528): YEAR(4)/MONTH(2)/DAY(2) as a vertical column
 * at x=16, y=200, pitch = date-glyph height. Date font = g_img_table[934..943] (w14h10),
 * separator glyph = g_img_table[659] between year|month and month|day. year = byte+2000.
 * Now that the RTC syncs over cmd 0x1C/0x15, g_rtc carries the real date. */
void ui_draw_clock_date(uint8_t year_byte, uint8_t month, uint8_t day)
{
    const img_entry_t *dig = &g_img_table[934];
    const img_entry_t *sep = &g_img_table[659];
    const uint16_t X = 16;
    uint16_t y = 200, pitch = dig[0].height;          /* 10 */
    uint16_t yr = (uint16_t)(year_byte + 2000u);
    uint8_t  d[10]; int k = 0;
    d[k++] = (uint8_t)(yr / 1000u); d[k++] = (uint8_t)((yr / 100u) % 10u);
    d[k++] = (uint8_t)((yr / 10u) % 10u); d[k++] = (uint8_t)(yr % 10u);   /* year 0..3 */
    d[k++] = 0xFF;                                                         /* sep */
    d[k++] = (uint8_t)(month / 10u); d[k++] = (uint8_t)(month % 10u);      /* month */
    d[k++] = 0xFF;                                                         /* sep */
    d[k++] = (uint8_t)(day / 10u);   d[k++] = (uint8_t)(day % 10u);        /* day */
    for (int i = 0; i < k; i++) {
        const img_entry_t *g = (d[i] == 0xFF) ? sep : &dig[d[i]];
        lcd_draw_element(g->flash_off, g->data_size, g->width, g->height, X, y);
        y = (uint16_t)(y + pitch);
    }
}

/* Weekday string (stock sub_14db8 @0x14db8): one of Sunday..Saturday drawn with the
 * letter font at (10,350). weekday 0=Sun..6=Sat (stock byte_1FFFC619+3). */
static const char *const k_weekday[7] = {
    "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"
};
void ui_draw_clock_weekday(uint8_t wd)
{
    if (wd > 6) return;
    clock_draw_text(k_weekday[wd], 10, 350);     /* stock sub_14db8: x=10, y=0x15e */
}

static uint8_t s_clock_force = 1;            /* set on page-14 entry: redraw everything once */
void ui_clock_force(void) { s_clock_force = 1; }

void ui_draw_clock_extras(void)
{
    extern volatile uint8_t g_loc_valid;         /* g_loc_name/g_loc_icon = ui_state.h macros */
    extern volatile uint8_t g_net_time_valid;
    extern rtc_time_t g_rtc;
    /* PER-ELEMENT change gate (stock power_temp_task case 0xE: each of temp/loc/date/weekday is
     * redrawn ONLY when its value changes - fn_836E/fn_1832C/util_18390 compares). Redrawing every
     * 500 ms tick (ui_11CA8) cleared+repainted the strips each time -> the location and temperature
     * FLICKERED. Now each is gated; s_clock_force (set on page entry) forces one full redraw. */
    uint8_t force = s_clock_force; s_clock_force = 0;
    static uint32_t c_date = 0xFFFFFFFFu; static uint8_t c_wd = 0xFF;
    static uint8_t  c_loc[24]; static uint16_t c_temp = 0xFFFFu; static uint8_t c_icon = 0xFEu;

    if (g_net_time_valid) {
        uint32_t d = ((uint32_t)g_rtc.year << 16) | ((uint32_t)g_rtc.month << 8) | g_rtc.day;
        if (force || d != c_date)        { ui_draw_clock_date(g_rtc.year, g_rtc.month, g_rtc.day); c_date = d; }
        if (force || g_rtc.weekday != c_wd) { ui_draw_clock_weekday(g_rtc.weekday); c_wd = g_rtc.weekday; }
    }
    if (!g_loc_valid) return;
    /* location pin (g_img_table[660]) + name - redraw only when the name string changes. */
    int locchg = force;
    { int i = 0; for (; i < (int)sizeof(c_loc) - 1; i++) { if (c_loc[i] != g_loc_name[i]) locchg = 1; if (!g_loc_name[i]) break; } }
    if (locchg) {
        ui_draw_img(660);
        if (g_loc_name[0]) clock_draw_loc((const char *)g_loc_name, 10, 56);  /* <=9 chars + "..." */
        { int i = 0; for (; i < (int)sizeof(c_loc) - 1 && g_loc_name[i]; i++) c_loc[i] = g_loc_name[i]; c_loc[i] = 0; }
    }
    if (force || (uint16_t)g_loc_temp != c_temp) { ui_draw_weather_temp(); c_temp = (uint16_t)g_loc_temp; }
    if (force || g_loc_icon != c_icon) {
        if (g_loc_icon != 0xFF) ui_draw_img((uint16_t)(g_loc_icon + 730u));   /* weather icon */
        c_icon = g_loc_icon;
    }
}

/* Power-menu temperature value. Stock (0xbb80 page renderer) draws ONE calibrated
 * temperature - max(sensor0,sensor1) corrected by the NTC cal-curve - as field 4
 * (an integer, vcvt.u32.f32). The two NTC sensors are read via the MCU's own ADC1
 * (temp.c, g_temp_sensor[]); g_temp_display is the displayed degC value (e.g. 28).
 * Earlier this drew 3 bogus rows from g_outlet[].aux12 (the PD packet had no temp
 * there). Redrawn only when the value changes (cache), like the watts number. */
void ui_draw_temp_table(void)
{
    extern uint32_t g_temp_display;
    static uint32_t s_last_temp = 0xFFFFFFFFu;
    uint32_t t = g_temp_display;
    if (t > 199u) t = 199u;
    if (t == s_last_temp) return;
    s_last_temp = t;
    /* integer degrees, medium digits g_img_table[894], no fraction, no unit glyph.
     * x=36 column (same as the I/W port rows), near the bottom of the power page. */
    render_num_x((uint16_t)t, 3, 0, 0, 0, 0, 0, 36, 0x190,
                 &g_img_table[894], 0);
}

/* Page-1 per-port V/I/W numbers (stock ui_set_page case 1: 4x sub_137B0 groups).
 * V uses digits g_img_table[884]/dot[566]/unit[565] at x=64; I/W use [894]/[580]/
 * [579] at x=36; ui_FC80 separator between. Values from g_telemetry (x100). */
/* Set on page entry (ui_set_page case 1/2) so the per-slot value gate redraws everything once
 * over the freshly-filled background; cleared after the first pass. */
static uint8_t s_pv_force = 1;
void ui_page_vals_force(void) { s_pv_force = 1; }

/* Per user: the V and A value text sits a bit too far right on the USB-C / USB-A pages. Nudge those
 * two fields LEFT (= lower framebuffer-y, since y is the rotated screen's horizontal axis, low=left).
 * Stock positions are otherwise 1:1; only V/A are shifted, W stays. Tune this if it's not enough. */
#define VA_SHIFT 8

void ui_draw_page1_values(void)
{
    /* CORRECTED value->position (verified by disasm of power_temp_task case 1: the float in
     * S0 before each draw_value_dec): the BIG number at (0x40,0x74) unit 566 is POWER
     * (flt_82A4), (0x24,0x4A) unit 580 is VOLTAGE (unk_82AC), (0x24,0x73) unit 581 is CURRENT
     * (unk_82B0). My old code put voltage at the 566 slot -> "voltage shown in the watt field"
     * and no real ampere. So: W (3-digit) big, V (2-digit) top-left, A (1-digit) bottom-left. */
    /* PORT ORDER + SOURCE FIX. Slots are C1,C2,C3,C4 top->bottom. C1 is the 240W chip (0x16)
     * whose telemetry is g_total_power (reg 0x21), NOT the per-port PDC/ADC array - routing
     * g_telemetry[3] (a USB-A ADC channel) into C1 was why C1 showed ~0.7V instead of 5V.
     * Stock C1 display (power_calc): V=unk_82AC=g_total_power/1000, A=unk_82B0=same,
     * W=(g_total_power/1000)^2 capped. C2/C3/C4 = g_telemetry[0..2] (PDC ports 0x7a/0x7c/0x7e). */
    extern volatile uint32_t g_total_power;
    static const uint8_t idx[4] = { 0, 0, 1, 2 };   /* slot 0 = C1 special; slots 1..3 = g_tel[0..2] */
    /* PER-SLOT change gate (stock soft_12F98 0x12f98): only redraw a slot's W/V/A when its value
     * actually changed - otherwise the lcd_fill_rect clear inside render_num_x runs every frame and
     * FLICKERS. s_pv_force (set on page entry) forces one full redraw over the fresh background. */
    static uint16_t cache[4][3];
    uint8_t force = s_pv_force; s_pv_force = 0;
    for (int p = 0; p < 4; p++) {
        int dy = p * 0x74;
        uint16_t v, i, w;
        if (p == 0) {
            /* C1 = 240W chip; g_total_power packs V (LOWORD, mV) + I (HIWORD, mA) - see power_calc
             * 0x1098c / fn_180A0. V=LOWORD/1000, A=HIWORD/1000, W=V*A (stock zeros W if >500). */
            uint32_t gtp = g_total_power;
            float vf = (float)(gtp & 0xFFFFu) / 1000.0f;    /* 0x138D -> 5.005 V */
            float af = (float)(gtp >> 16)     / 1000.0f;    /* 0x00D6 -> 0.214 A */
            float wf = vf * af; if (wf > 500.0f) wf = 0.0f; /* stock sanity: >500 -> 0 */
            v = (uint16_t)(vf * 100.0f + 0.5f);
            i = (uint16_t)(af * 100.0f + 0.5f);
            w = (uint16_t)(wf * 100.0f + 0.5f);
        } else {
        uint8_t s = idx[p];
        v = g_telemetry.v_x100[s]; i = g_telemetry.i_x100[s]; w = g_telemetry.p_x100[s];
        }
        if (!force && cache[p][0] == w && cache[p][1] == v && cache[p][2] == i)
            continue;                                   /* unchanged -> no redraw, no clear, no flicker */
        cache[p][0] = w; cache[p][1] = v; cache[p][2] = i;
        /* W (power): 3 int digits, x=0x40, y=0x74, digTable 884, unit 566(W), dot 565 */
        render_num_x((uint16_t)(w/100u), 3, 1, (uint16_t)((w/10u)%10u), 1,
                     &g_img_table[884], &g_img_table[565], 64, (uint16_t)(0x74+dy), &g_img_table[884], &g_img_table[566]);
        /* V (voltage): 2 int digits, x=0x24, digTable 894, unit 580(V), dot 579.
         * y nudged LEFT by RING_VA_SHIFT from stock 0x4A (user: "V/A a bit too far right"). */
        render_num_x((uint16_t)(v/100u), 2, 1, (uint16_t)((v/10u)%10u), 1,
                     &g_img_table[894], &g_img_table[579], 36, (uint16_t)(0x4A+dy-VA_SHIFT), &g_img_table[894], &g_img_table[580]);
        ui_FC80(65 + p * 0x74);   /* STOCK: ui_FC80(65) between V and A, before the A draw (byte-1:1) */
        /* A (current): 1 int digit, x=0x24, digTable 894, unit 581(A), dot 579. y nudged LEFT. */
        render_num_x((uint16_t)(i/100u), 1, 1, (uint16_t)((i/10u)%10u), 1,
                     &g_img_table[894], &g_img_table[579], 36, (uint16_t)(0x73+dy-VA_SHIFT), &g_img_table[894], &g_img_table[581]);
    }
}

/* Page-2 per-port numbers (stock ui_set_page case 2: 2 USB-A ports A1/A2).
 * V@x64 y=0xE6/0x15A, I@x36 y=0xB9/0x12D, sep@178/294, W@x36 y=0xE7/0x15B (2 frac). */
void ui_draw_page2_values(void)
{
    /* Same value->position correction as page1 (stock case 2): big=POWER unit566, then
     * VOLTAGE unit580, CURRENT unit581 (1int+2frac). USB-A ports = g_telemetry[3]/[4]. */
    /* USB-A separators = EXACT stock ui_set_page case 2 values (ui_FC80(178)/(294)). The 2-frac
     * A-field is taller, so stock places it at V-7 (more left than the C page) - matches "USB-A
     * too far right, move left". */
    /* ysep nudged from stock {178,294} to {182,297} = CENTERED in the V->A gap (measured from a SWD
     * screenshot: V ends fb_y 175/291, A starts 189/303). Stock placed the "|" right against V; user
     * wanted it centred ("zu nah an dem V und zu weit weg von der 0.0A"). */
    static const int yw[2] = {0xE6,0x15A}, yv[2] = {0xB9,0x12D}, ysep[2] = {182,297}, yi[2] = {0xE7,0x15B};
    static uint16_t cache[2][3];
    uint8_t force = s_pv_force; s_pv_force = 0;
    for (int p = 0; p < 2; p++) {
        uint16_t v = g_telemetry.v_x100[3+p], i = g_telemetry.i_x100[3+p], w = g_telemetry.p_x100[3+p];
        if (!force && cache[p][0] == w && cache[p][1] == v && cache[p][2] == i)
            continue;                                   /* unchanged -> no flicker */
        cache[p][0] = w; cache[p][1] = v; cache[p][2] = i;
        /* W (power): 3 int digits, x=0x40, unit 566(W) */
        render_num_x((uint16_t)(w/100u), 3, 1, (uint16_t)((w/10u)%10u), 1,
                     &g_img_table[884], &g_img_table[565], 64, (uint16_t)yw[p], &g_img_table[884], &g_img_table[566]);
        /* USB-A page = EXACTLY stock (user: "USB-A separator not exactly like stock"). The VA_SHIFT
         * nudge is applied ONLY on the USB-C page (page 1); page 2 keeps the stock V/A/sep positions. */
        /* V (voltage): 2 int digits, x=0x24, unit 580(V). */
        render_num_x((uint16_t)(v/100u), 2, 1, (uint16_t)((v/10u)%10u), 1,
                     &g_img_table[894], &g_img_table[579], 36, (uint16_t)yv[p], &g_img_table[894], &g_img_table[580]);
        ui_FC80(ysep[p]);              /* stock separator: ui_FC80(178)/(294) */
        /* A (current): 1 int + 2 frac digits, x=0x24, unit 581(A). */
        render_num_x((uint16_t)(i/100u), 1, 2, (uint16_t)(i%100u), 2,
                     &g_img_table[894], &g_img_table[579], 36, (uint16_t)yi[p], &g_img_table[894], &g_img_table[581]);
    }
}

void ui_draw_list(const ui_draw_t *list, int n)
{
    for (int i = 0; i < n; i++) {
        uint16_t w = (uint16_t)(list[i].dim_wh & 0xFFFF);
        uint16_t h = (uint16_t)(list[i].dim_wh >> 16);
        uint16_t x = (uint16_t)(list[i].pos_xy & 0xFFFF);
        uint16_t y = (uint16_t)(list[i].pos_xy >> 16);
        lcd_draw_element(list[i].flash_off, list[i].size, w, h, x, y);
    }
}

/* ---- Static per-page draw lists (transcribed from stock ui_set_page) ---- */
static const ui_draw_t page0_extra[] = {
    { 0xDAB27, 0x560, 0x68000E, 0xA005C },
    /* { 0xDB087, ... } "TO EXPLORE" sprite removed - replaced by the "by ATC1441" text below */
    { 0xDA74A, 0x3DD, 0x3B000E, 0xD20086 },
    { 0xDA095, 0x22A, 0x16001A, 0xE40032 },
};
static const ui_draw_t page1_bg[] = {
    { 0xE1630, 0x70A,  0x7E0012,  0xB000AC },
    { 0xDC259, 0x53D7, 0x1CC008C, 0xA000A  },
};
static const ui_draw_t page2_bg[] = {
    { 0xE1630, 0x70A,  0x7E0012, 0xB000AC },
    { 0xE2404, 0x2D71, 0xE4008C, 0x7E000A },
};
static const ui_draw_t page4[] = {
    { 0xEBCCB, 0x17CF, 0x154001C, 0x460076 },
    { 0xEF700, 0x1729, 0x154001A, 0x460036 },
    { 0xE89EA, 0x1015, 0x152000E, 0x47000A },
};
/* page5/page6 are drawn explicitly in ui_set_page (they interleave sub_FBB4 clears). */
/* Port-detail pages (full-render branch from stock ui_set_page). */
/* page7/page8 drawn explicitly in ui_set_page (sibling-group partial-redraw branch). */
static const ui_draw_t page15[] = { { 0x4FF9A8, 0x48C, 0x9C0014, 0xA2000A } };
/* Splash pages 16..19 are drawn directly in ui_set_page (splash image + WDT-fed delay + transition),
 * matching stock ui_set_page cases 16-19; no static draw-lists needed. */

#define LIST(a) (a), (int)(sizeof(a)/sizeof((a)[0]))

/* Blocking delay that feeds the watchdog - used by the splash pages (stock delay(500)+WDT_Feed()). */
static void ui_splash_delay(uint32_t ms)
{
    uint32_t t0 = get_tick_ms();
    while ((uint32_t)(get_tick_ms() - t0) < ms) wdt_feed();
}

/* fwd decls (defined below) - ui_set_page stops the home power-ring sweep on a page change */
extern uint8_t g_anim_active;
static uint8_t s_ring_flourish;

void ui_set_page(uint8_t page)
{
    anim_stop();                  /* leaving the screensaver animation, if any */
    video_stop();                 /* leaving the video page re-enables the screenshot shadow */
    mode_stop();                  /* leaving any power-mode confirmation display */
    /* Issue 1: stop the home power-ring sweep when navigating away - else ui_animate_page keeps
     * drawing g_page_idx (the ring, 1/2/3) over the new page. The target page re-arms if it needs to. */
    if (g_page_idx >= 1u && g_page_idx <= 3u) { g_anim_active = 0; s_ring_flourish = 0; }
    g_prev_page = g_menu_page;
    g_menu_page = page;

    switch (page) {
    case 0:                                    /* stock ui_set_page case 0 (0x13C66) */
        lcd_fill(0x0000);
        g_clock_mode = 0;                      /* unk_1FFF8298 = 0 */
        if (g_power_w || pd_attached_count() >= 1) {
            g_clock_mode = 1;                  /* active: suppress idle hint */
        } else {
            for (uint16_t i = 34; i <= 41; i++) ui_draw_img(i);  /* TO CHARGE/EXPLORE hint */
        }
        g_page_anim = 0;                       /* byte_1FFF829A = 0 */
        g_page_idx = (uint8_t)(g_page_group <= 2 ? g_page_group + 1 : 1);  /* group->1/2/3 */
        ui_draw_static();                      /* sub_11EE4: top status bar */
        ui_draw_menu();                        /* sub_11FA4: WLAN signal icon */
        ui_draw_list(LIST(page0_extra));       /* POWER / TO CHARGE labels */
        dbg_text(356, 92, "by ATC1441", 0xFFFF, 0x0000);  /* replaces the "TO EXPLORE" sprite (white) */
        ui_draw_watts((uint16_t)g_power_w);    /* sub_13A14 @ (86,294) */
        ui_home_ring_update((uint16_t)g_power_w, 1); /* home entry: play the fill animation from empty */
        break;
    case 1: s_pv_force = 1; lcd_fill(0x0000); ui_draw_list(LIST(page1_bg)); ui_draw_static(); ui_draw_menu(); ui_draw_page1_values(); break;
    case 2: s_pv_force = 1; lcd_fill(0x0000); ui_draw_list(LIST(page2_bg)); ui_draw_static(); ui_draw_menu(); ui_draw_page2_values(); break;
    case DEBUG_PAGE: ui_debug_enter(); break;   /* live telemetry debug page */
    case GRAPH_PAGE: ui_graph_enter(); break;   /* scrolling history graph */
    case 3: /* stock case 3 (0x14124): the TEMPERATURE screen - fill + menu + bg
             * element + the temperature bar (sub_188DC reads the temp float from
             * [0x1FFF8304], NOT power). ui_188DC draws the bar + colour tip + degC
             * number, so no separate number draw is needed. */
        lcd_fill(0x0000); ui_draw_menu();
        lcd_draw_element(0xE1630u, 0x70Au, 0x12, 0x7E, 172, 178);
        ui_188DC();                            /* sub_188DC: 20-seg temp bar + tip + degC */
        break;
    case 4: lcd_fill(0x0000); ui_draw_list(LIST(page4)); ui_draw_menu(); break;
    case 5:  /* stock case 5: img, clear region, img (no full clear) */
        lcd_draw_element(0xE99FF, 0x22CC, 58, 400, 104, 40);
        lcd_fill_rect(38, 40, 58, 400, 0x0000);          /* sub_FBB4 */
        lcd_draw_element(0xEF700, 0x1729, 26, 340, 54, 70);
        break;
    case 6:  /* stock case 6: clear region, img, img */
        lcd_fill_rect(104, 40, 58, 400, 0x0000);         /* sub_FBB4 */
        lcd_draw_element(0xEBCCB, 0x17CF, 28, 340, 118, 70);
        lcd_draw_element(0xED49A, 0x2266, 58, 400, 38, 40);
        break;
    case 7:  /* stock case 7: partial redraw when switching within the 7/8 group */
        if ((g_prev_page == 7 || g_prev_page == 8) && g_page_idx != 7) {
            lcd_draw_element(0xF39E8, 0x173F, 92, 220, 54, 10);
            lcd_draw_element(0xF5127, 0x7DA, 92, 220, 54, 250);
        } else {
            g_page_idx = 0; lcd_fill(0x0000);
            lcd_draw_element(0xF1E3E, 0xC4A, 18, 204, 172, 138);
            lcd_draw_element(0xF39E8, 0x173F, 92, 220, 54, 10);
            lcd_draw_element(0xF5127, 0x7DA, 92, 220, 54, 250);
            lcd_draw_element(0xF0E29, 0x1015, 14, 338, 10, 71);
        }
        break;
    case 8:  /* stock case 8: same group as 7 */
        if ((g_prev_page == 7 || g_prev_page == 8) && g_page_idx != 7) {
            lcd_draw_element(0xF2A88, 0xF60, 92, 220, 54, 10);
            lcd_draw_element(0xF5901, 0xF4A, 92, 220, 54, 250);
        } else {
            g_page_idx = 0; lcd_fill(0x0000);
            lcd_draw_element(0xF1E3E, 0xC4A, 18, 204, 172, 138);
            lcd_draw_element(0xF2A88, 0xF60, 92, 220, 54, 10);
            lcd_draw_element(0xF5901, 0xF4A, 92, 220, 54, 250);
            lcd_draw_element(0xF0E29, 0x1015, 14, 338, 10, 71);
        }
        break;
    case 9:  /* group {9,10,11} */
        if ((g_prev_page == 9 || g_prev_page == 10 || g_prev_page == 11) && g_page_idx != 7) {
            lcd_draw_element(0xF91C4, 0x11D3, 92, 146, 54, 10);
            lcd_draw_element(0xFC4C8, 0xEB9, 92, 146, 54, 166);
            lcd_draw_element(0xFA397, 0xDFD, 92, 146, 54, 322);
        } else {
            g_page_idx = 0; lcd_fill(0x0000);
            lcd_draw_element(0xF7860, 0xCBC, 18, 222, 172, 130);
            lcd_draw_element(0xF91C4, 0x11D3, 92, 146, 54, 10);
            lcd_draw_element(0xFC4C8, 0xEB9, 92, 146, 54, 166);
            lcd_draw_element(0xFA397, 0xDFD, 92, 146, 54, 322);
            lcd_draw_element(0xF684B, 0x1015, 14, 338, 10, 71);
        }
        break;
    case 10:
        if ((g_prev_page == 9 || g_prev_page == 10 || g_prev_page == 11) && g_page_idx != 7) {
            lcd_draw_element(0xF851C, 0xCA8, 92, 146, 54, 10);
            lcd_draw_element(0xFD381, 0x13DF, 92, 146, 54, 166);
            lcd_draw_element(0xFA397, 0xDFD, 92, 146, 54, 322);
        } else {
            g_page_idx = 0; lcd_fill(0x0000);
            lcd_draw_element(0xF7860, 0xCBC, 18, 222, 172, 130);
            lcd_draw_element(0xF851C, 0xCA8, 92, 146, 54, 10);
            lcd_draw_element(0xFD381, 0x13DF, 92, 146, 54, 166);
            lcd_draw_element(0xFA397, 0xDFD, 92, 146, 54, 322);
            lcd_draw_element(0xF684B, 0x1015, 14, 338, 10, 71);
        }
        break;
    case 11:
        if ((g_prev_page == 9 || g_prev_page == 10 || g_prev_page == 11) && g_page_idx != 7) {
            lcd_draw_element(0xF851C, 0xCA8, 92, 146, 54, 10);
            lcd_draw_element(0xFC4C8, 0xEB9, 92, 146, 54, 166);
            lcd_draw_element(0xFB194, 0x1334, 92, 146, 54, 322);
        } else {
            g_page_idx = 0; lcd_fill(0x0000);
            lcd_draw_element(0xF7860, 0xCBC, 18, 222, 172, 130);
            lcd_draw_element(0xF851C, 0xCA8, 92, 146, 54, 10);
            lcd_draw_element(0xFC4C8, 0xEB9, 92, 146, 54, 166);
            lcd_draw_element(0xFB194, 0x1334, 92, 146, 54, 322);
            lcd_draw_element(0xF684B, 0x1015, 14, 338, 10, 71);
        }
        break;
    case 12:  /* group {12,13} */
        if ((g_prev_page == 12 || g_prev_page == 13) && g_page_idx != 7) {
            lcd_draw_element(0xFF775, 0x1075, 58, 180, 72, 50);
            lcd_draw_element(0x1007EA, 0x8D6, 58, 180, 72, 250);
        } else {
            g_page_idx = 0; lcd_fill(0x0000);
            lcd_draw_element(0xFF775, 0x1075, 58, 180, 72, 50);
            lcd_draw_element(0x1007EA, 0x8D6, 58, 180, 72, 250);
            lcd_draw_element(0xFE760, 0x1015, 14, 338, 10, 71);
        }
        break;
    case 13:
        if ((g_prev_page == 12 || g_prev_page == 13) && g_page_idx != 7) {
            lcd_draw_element(0x1010C0, 0x979, 58, 180, 72, 50);
            lcd_draw_element(0x101A39, 0x108B, 58, 180, 72, 250);
        } else {
            g_page_idx = 0; lcd_fill(0x0000);
            lcd_draw_element(0x1010C0, 0x979, 58, 180, 72, 50);
            lcd_draw_element(0x101A39, 0x108B, 58, 180, 72, 250);
            lcd_draw_element(0xFE760, 0x1015, 14, 338, 10, 71);
        }
        break;
    case 15: ui_draw_list(LIST(page15)); break;
    /* Pages 16-19 are SPLASH/transition screens (stock ui_set_page 0x14be2..0x14d94): draw one
     * full splash image, block briefly (feeding the WDT), then transition. Durations shortened from
     * stock's 1-1.5 s (user: "laufen zu lange"). */
    case 16:                                     /* WiFi pairing splash -> home */
        g_clock_mode = 0; lcd_fill(0x0000);
        lcd_draw_element(0x5F0DC7, 0x1A78, 58, 334, 70, 72);
        ui_splash_delay(800);
        g_menu_page = 0;                         /* stock: if [0]==16 -> [1]=0; ui_set_page([1]) */
        ui_set_page(0); g_idle_count = 0;
        return;
    case 17:                                     /* Raserei confirm splash -> home */
        g_clock_mode = 0; lcd_fill(0x0000);
        lcd_draw_element(0x5F283F, 0x163E, 50, 284, 74, 98);
        ui_splash_delay(600);
        ui_set_page(0); g_idle_count = 0;
        return;
    case 18:                                     /* Smart confirm splash -> home */
        g_clock_mode = 0; lcd_fill(0x0000);
        lcd_draw_element(0x5F3E7D, 0x1948, 50, 284, 74, 98);
        ui_splash_delay(600);
        ui_set_page(0); g_idle_count = 0;
        return;
    case 19:                                     /* Individual confirm splash -> home */
        g_clock_mode = 0; lcd_fill(0x0000);
        lcd_draw_element(0x5F57C5, 0x193D, 50, 294, 74, 92);
        ui_splash_delay(600);
        ui_set_page(0); g_idle_count = 0;
        return;
    case 14: {                                  /* clock face (stock ui_set_page case 14 @0x14a96) */
        extern rtc_time_t g_rtc; extern void ui_clock_force(void);
        g_clock_mode = 0;                       /* stock g_menu_page[4] = 0 */
        lcd_fill(0x0000);                       /* LCD_setFullColor(0) */
        ui_clock_force();                       /* fresh screen -> force one full redraw of all extras */
        ui_draw_weather_temp();                 /* stock ui_13A14: outdoor temp digits @ (176,10) */
        draw_num2digit(g_rtc.hour, g_rtc.min);  /* HH:MM big digits + colon */
        ui_draw_clock_extras();                 /* location + date column + weekday + weather icon */
        break;
    }
    case VIDEO_PAGE:                            /* internal rickroll video (centred) */
        video_start();
        break;
    default:
        /* pages 7..13,20: port-detail / settings pages with dynamic content
         * (digit rendering, asset-table lookups) — to be completed. */
        lcd_fill(0x0000);
        break;
    }
}

/* Page slide-in animation state (stock g_port_data per-page frame counters). */
uint8_t        g_anim_active;
/* g_page_anim is the ui_state.h macro (stock byte_1FFF829A) */
static uint16_t s_anim_cur[14], s_anim_start[14], s_anim_end[14];
static uint8_t  s_ring_flourish;     /* 1 = home ring is playing the full-circle plug-in flourish */
static uint16_t s_ring_pwr_target;   /* image index to settle the ring to once the flourish finishes */

void ui_animate_page(void)
{
    if (!g_anim_active || g_page_idx >= 14) { g_anim_active = 0; return; }

    /* Frame-rate gate (stock test_and_clear_flag(byte_2000F3D0) ~ a periodic timer). */
    static uint32_t s_anim_last;
    uint32_t now = get_tick_ms();
    if (now - s_anim_last < 30u) return;
    s_anim_last = now;

    /* Animated pages slide successive sprites in (stock: pages 1/2/3). */
    if (g_page_idx == 1 || g_page_idx == 2 || g_page_idx == 3) {
        uint16_t *cur = &s_anim_cur[g_page_idx];
        uint16_t target = s_anim_end[g_page_idx];
        uint16_t step = (uint16_t)(*cur + 8);          /* 8 sprites per tick */
        if (step > target + 1) step = (uint16_t)(target + 1);
        while (*cur < step && *cur < 871) {
            ui_draw_img(*cur);
            (*cur)++;
        }
        if (*cur > target) {                            /* sweep complete */
            if (g_page_anim) {                          /* stock byte_1FFF829A: loop */
                static uint32_t loop_t; static uint8_t loop_w;
                if (loop_w) {
                    if (now - loop_t >= 50u) { *cur = s_anim_start[g_page_idx]; loop_w = 0; }
                } else {
                    loop_t = now; loop_w = 1;
                }
            } else if (s_ring_flourish) {
                /* the full-circle plug-in flourish just finished -> SETTLE down to the power level:
                 * re-aim end at the power-proportional target and redraw just that level's 8 tiles
                 * (cur = target-7) so the ring drops from full to the charging-power fill. */
                uint16_t st = s_anim_start[g_page_idx];
                uint16_t t  = s_ring_pwr_target;
                s_anim_end[g_page_idx] = t;
                *cur = (t >= (uint16_t)(st + 7u)) ? (uint16_t)(t - 7u) : st;
                s_ring_flourish = 0;                    /* keep g_anim_active=1: draw the settle next tick */
            } else {                                    /* one-shot: transition done */
                g_anim_active = 0;
                g_clock_mode  = 0;                      /* stock unk_1FFF8298 = 0: release the clock-
                                                         * screensaver suppress so the 60 s idle ->
                                                         * clock face can fire again (was stuck on). */
            }
        }
    }
    /* idx 7/8 = the element/fire animations (stock ui_animate_element 0x16c58): one frame per tick,
     * on completion ui_set_page(17) for idx 8 (Raserei view). Drives Raserei the STOCK way via
     * anim_state[8] instead of the old custom mode_tick fire loop (byte-1:1). */
    else if (g_page_idx == 7 || g_page_idx == 8) {
        uint16_t *cur = &s_anim_cur[g_page_idx];
        if (*cur == s_anim_start[g_page_idx] && !g_page_anim) lcd_fill_rect(0, 0, 200, 480, 0x0000);
        ui_draw_img(*cur);
        if (++(*cur) > s_anim_end[g_page_idx]) {
            if (g_page_anim) { *cur = s_anim_start[g_page_idx]; }   /* loop */
            else {
                g_anim_active = 0;
                *cur = s_anim_start[g_page_idx];
                ui_set_page(g_page_idx == 8 ? 17u : 0u);            /* stock: idx8 -> page 17 */
            }
        }
    }
}

/* ui_draw_icon - over-temp warning indicator (called by ui_DB38 on thermal recovery). Cosmetic;
 * the thermal-throttle BEHAVIOR (the 240W limit step) is in power.c. Draws the temp icon glyph. */
void ui_draw_icon(void)
{
    ui_draw_img(593);   /* the degC/temp unit glyph (same family ui_188DC uses) */
}

/* app_F6F8 (0xf6f8) - 1:1: swap the two draw-buffer pointers + clear g_port_cfg[0]. My display is
 * single-buffer CPU-push, so the swapped pointers are inert here, but the function matches stock. */
static void *s_buf_811C, *s_buf_8120;
static volatile uint8_t s_port_cfg0;            /* stock g_port_cfg[0] */
void *app_F6F8(void)
{
    void *v1 = s_buf_811C;
    s_buf_811C = s_buf_8120;
    s_buf_8120 = v1;
    s_port_cfg0 = 0;
    return &s_buf_811C;
}

/* app_18DDC (0x18ddc) - 1:1. When a page animation is active (g_clock_mode && g_page_anim), commit
 * the anim frame pointer back to its start and clear the active flag, returning 1 (= "transition just
 * settled"); else 0. Stock power_temp_task uses this to gate its idle->page dispatch. */
int app_18DDC(void)
{
    if ((g_clock_mode & 1u) && (g_page_anim & 1u)) {
        if (g_page_idx < 14u) s_anim_cur[g_page_idx] = s_anim_start[g_page_idx];
        g_clock_mode = 0;
        return 1;
    }
    return 0;
}

/* Home power-ring fill. The ring is anim_state[1/2/3] = the 3 power-TIER colours (g_page_idx 1/2/3 =
 * tier >=13W / 6-12W / <=5W), image ranges (live-read over J-Link): idx1: g_img_table[34..201],
 * idx2: [202..369], idx3: [370..537] (21 levels x 8 tiles = corners/sides/centre).
 *
 * Behaviour (per user): on a NEW power-taking device the ring plays a FULL-CIRCLE animation ONCE
 * (sweeps empty all the way around to FULL, regardless of watts) and then SETTLES to a level
 * PROPORTIONAL to the charging power. The COLOUR depends on the device (its power tier, idx 1/2/3).
 * After that, watt changes only nudge the level (redraw it), they do NOT replay the animation. So:
 *   restart=1 (new device / tier change / home entry): full-circle flourish (cur=base, end=base+167),
 *             then ui_animate_page settles to s_ring_pwr_target (the power level).
 *   restart=0 (steady watt change): just redraw the power level's 8 tiles (no flourish).
 * One-shot (g_page_anim=0); 8 sprites (=1 level)/tick. Exact watts = the separate number, not the ring.
 * NOTE: watts->level scale (15 W/level) is calibrated to one live point and may need tuning. */
void ui_home_ring_update(uint16_t watts, int restart)
{
    static const uint16_t base[4] = { 0u, 34u, 202u, 370u };
    static uint8_t s_last_idx = 0u;
    uint8_t idx = g_page_idx;
    if (idx < 1u || idx > 3u) return;
    uint16_t b = base[idx];
    uint16_t level = (uint16_t)(watts / 15u);             /* ~15 W per fill level */
    if (level > 20u) level = 20u;
    uint16_t target = (uint16_t)(b + level * 8u + 7u);    /* last image of the power level */
    s_anim_start[idx] = b;
    if (restart || idx != s_last_idx) {
        /* NEW device / tier change -> FULL-CIRCLE flourish, then settle to the power level. */
        s_anim_end[idx]   = (uint16_t)(b + 167u);         /* sweep the WHOLE ring (full circle) */
        s_anim_cur[idx]   = b;                            /* from empty */
        s_ring_flourish   = 1;
        s_ring_pwr_target = target;                       /* settle here when the circle completes */
        s_last_idx        = idx;
    } else {
        /* steady watt change -> redraw the power level (no flourish) */
        s_anim_end[idx] = target;
        s_ring_flourish = 0;
        if (s_anim_cur[idx] < b || s_anim_cur[idx] > (uint16_t)(b + 167u))
            s_anim_cur[idx] = b;                          /* (re)init if stale/out of range */
        if (s_anim_cur[idx] > target)
            /* W dropped: rewind to the START of the target level (target-7), not `target` - else the
             * animator draws only the LAST of the level's 8 tiles (the "small rectangle") and leaves
             * the other 7 showing the stale higher level. Redraws the full level in ~1 tick. */
            s_anim_cur[idx] = (uint16_t)(target - 7u);
    }
    g_page_anim   = 0;                                    /* ONE-SHOT (no loop) */
    g_anim_active = 1;                                    /* let ui_animate_page run */
}

/* ---- Menu button click (stock on_menu_btn_click) ----
 * First press initialises and shows the home page; a press while in standby
 * wakes the device (re-enable charge + wake display); otherwise it navigates. */
/* Mark the UI as initialised at boot (stock sets unk_2000F3E0=1 when the boot
 * animation finishes). Without this the first MENU press is consumed re-showing
 * the home page instead of navigating. */
void ui_mark_initialised(void) { s_initialised = 1; }
int  ui_boot_done(void) { return s_initialised; }   /* 1 after the boot animation finishes */
int  ui_is_standby(void) { return s_standby; }      /* 1 = charging off (stock 0x2000F3DD) */
int  ui_is_disp_off(void) { return s_disp_off; }    /* 1 = Standby Mode (LCD asleep, ports on) */
/* Set the charge on/off state from a SINGLE source of truth (s_standby), so the app's
 * cmd 0x06 DP-1 write and the cmd 0x07 DP-1 report (= !s_standby) stay consistent.
 * Without this the app slider snapped back ON because the report still read s_standby=0. */
void ui_set_charge(int on)
{
    s_standby = on ? 0u : 1u;
    charge_set_enable(on ? 1 : 0);
    /* stock DP1 (case 1) also drives the panel: master OFF -> EnableDisableDisplay(1) (sleep-in +
     * backlight off = "LCD geht aus"); master ON -> EnableDisableDisplay(0) (wake). This was the
     * missing piece - the whole-charger-off did not blank the screen. */
    extern void lcd_sleep(int sleep);
    lcd_sleep(on ? 0 : 1);
}

/* Boot animation (stock ui_home_check @0x18D28). Frames 0..33 = the centre boot
 * logo, one per tick (paced by flag +6 = 120ms), then show home + enable nav.
 * (Confirmed 1:1: the disasm of ui_animate_page's home branch @0x191CC draws the
 * border pieces 34..41 ALL-AT-ONCE in a 34->41 loop only while page==0 - i.e. a
 * static border redraw gated by sub_155A4, NOT a per-tick reveal. So boot ends at
 * 33; the border is drawn statically by ui_set_page(0). End=33 is the empirical
 * J-Link-dumped stock value.) */
void ui_home_check(void)
{
    if (s_initialised) return;
    uint32_t now = get_tick_ms();
    if (now - s_anim_last < 120u) return;    /* stock paces the boot anim by flag +6
                                              * (byte_2000F3CF, sw-timer period 120ms). */
    s_anim_last = now;

    if (s_anim_frame <= ANIM_END) {
        ui_draw_img(s_anim_frame);           /* g_img_table[frame] @ g_img_pos[frame] */
        s_anim_frame++;
    } else {
        ui_set_page(0);                      /* animation done -> home */
        s_initialised = 1;                   /* enable button navigation */
    }
}

/* Menu-handler shared state (stock RAM flags). g_clock_alt/g_wlan_suspend live in
 * fw_main; the page-stash + two confirm flags are local to the menu logic. */
extern uint8_t  g_clock_alt;          /* unk_1FFF8158 (fw_main.c)  */
extern int      g_wlan_suspend;       /* byte_2000F3DF (fw_main.c) */
extern void     ui_18CE4(uint8_t idx, uint8_t anim);   /* g_idle_count via tick.h */
uint8_t g_flag_8411;                  /* stock byte_1FFF8411 (g_port_data+0x81) */
uint8_t g_flag_8483;                  /* stock byte_1FFF8413 (g_port_data+0x83) */
static uint8_t s_page_stash;          /* stock byte_1FFF8296 ([0x1FFF8294+2]) */

void on_menu_btn_click(void)
{
    if (!s_initialised) {                 /* very first press: power on (0x2000F3E0) */
        s_initialised = 1;
        ui_set_page(0);
        return;
    }
    g_idle_count = 0;                     /* stock: clear dword_20014774 (activity) */

    if (s_disp_off) {                     /* Standby Mode: a press just wakes the display */
        s_disp_off = 0;
        lcd_sleep(0);                     /* panel wake + setBlPWM(20) -> backlight + home page back */
        return;
    }

    if (s_standby) {                      /* wake from standby (0x2000F3DD) */
        s_standby = 0;
        g_flag_8483 = 0;                  /* [0x1FFF8390+0x83] = 0 */
        charge_set_enable(1);             /* sub_DAB0(1) */
        lcd_sleep(0);                     /* sub_10320(0) wake display */
        wlan_module_reset(0);             /* sub_124B0(0): power the WLAN module back on */
        ports_clear();                    /* sub_12E78(1) reset readings */
        return;
    }
    if (g_wlan_suspend) {                 /* wake from the WLAN-reset suspend (0x2000F3DF):
                                           * the page-11 confirm put us here (module off,
                                           * display asleep); a click re-powers + wakes.
                                           * Without this the WLAN-reset screen hung
                                           * forever (user: "WLAN reset endlos"). */
        g_wlan_suspend = 0;
        lcd_sleep(0);                     /* sub_10320(0) wake display */
        wlan_module_reset(0);             /* sub_124B0(0): re-power the module + UART */
        return;
    }

    switch (g_menu_page) {
    case 0:  ui_set_page(DEBUG_PAGE);  break;   /* home -> debug page (2nd in the cycle) */
    case DEBUG_PAGE: ui_set_page(GRAPH_PAGE); break;  /* debug -> history graph */
    case GRAPH_PAGE: ui_set_page(VIDEO_PAGE);  break;   /* graph -> rickroll video (moved up) */
    case VIDEO_PAGE: ui_set_page(1);  break;    /* rickroll -> USB-C values */
    case 1:  ui_set_page(2);  break;            /* page 1 -> page 2 */
    case 2:  ui_set_page(3);  break;
    case 3:  ui_set_page(4);  break;
    case 4:  ui_set_page(0);  break;
    case 5:  ui_set_page(6);  break;
    case 6:  ui_set_page(5);  break;
    case 7:  ui_set_page(8);  break;
    case 8:  ui_set_page(7);  break;
    case 9:  ui_set_page(10); break;
    case 10: ui_set_page(11); break;
    case 11: ui_set_page(9);  break;
    case 12: ui_set_page(13); break;
    case 13: ui_set_page(12); break;
    case 14: ui_set_page(0);  break;
    default: break;
    }
}

/* stock on_menu_btn_doubleclick @0x193B2 - reconstructed 1:1.
 * Pages 4/5/6 open a sub-menu; pages 7..11 stash the current page and go to the
 * confirm page 12; page 12 APPLIES the setting that was selected on the stashed
 * page (this is the "double-click confirm" that previously did nothing); page 13
 * returns home. The stashed-page->action map (stock tbb @0x19422):
 *   7 -> clock style A (g_clock_alt=0)      8 -> clock style B (g_clock_alt=1)
 *   9 -> status mode 0 + ui_18CE4(8)       10 -> status mode 1 + page 18
 *  11 -> WLAN reset (suspend + sleep + PA4 power-cycle the module) */
void on_menu_btn_doubleclick(void)
{
    g_idle_count = 0;                              /* stock: clear the idle counter */
    switch (g_menu_page) {
    case 4: ui_set_page(5);  break;                /* open sub-menu */
    case 5: ui_set_page(7);  break;
    case 6: ui_set_page(9);  break;
    case 7: case 8: case 9: case 10: case 11:
        s_page_stash = g_menu_page;                /* [0x1FFF8294+2] = current page */
        ui_set_page(12);                           /* -> confirm page */
        break;
    case 12:                                       /* CONFIRM the stashed page's choice */
        switch (s_page_stash) {
        case 7:  g_clock_alt = 0;   ui_set_page(0); break;
        case 8:  g_clock_alt = 1;   ui_set_page(0); break;
        case 9:  g_flag_8411 = 1; mode_select(0); break;   /* Raserei: fire anim -> home */
        case 10: g_flag_8411 = 0; mode_select(1); break;   /* Smart: page 18 -> home    */
        case 11:                                   /* Standby Mode: LCD + backlight off, ports keep running */
            s_disp_off = 1;
            ui_set_page(0);                        /* home stays in GRAM behind the sleeping panel */
            lcd_sleep(1);                          /* panel sleep-in + setBlPWM(0) -> backlight fully off */
            break;
        default: ui_set_page(0); break;
        }
        break;
    case 13: ui_set_page(0); break;
    case GRAPH_PAGE: ui_graph_select_port(); break;   /* graph: pick the next port */
    default: break;
    }
}

/* Menu long-press = enter standby (stock on_menu_btn_longpress):
 * disable charging, sleep the display, clear the port readings. */
void on_menu_btn_longpress(void)
{
    s_standby = 1;                   /* byte_2000F3DD = 1 */
    g_flag_8483 = 31;                /* sub_EA44(31): [0x1FFF8390+0x83] = 0x1F */
    charge_set_enable(0);            /* sub_DAB0(0) */
    lcd_sleep(1);                    /* sub_10320(1) display to sleep */
    ports_clear();                   /* sub_12E78(0) zero per-port readings */
}

/* ---- WLAN button (stock on_wlan_btn_click / on_wlan_btn_longpress) ---- */
void on_wlan_btn_click(void)
{
    wlan_send_click();               /* notify companion module */
    ui_set_page(16);                 /* WiFi / pairing splash page */
}

void on_wlan_btn_longpress(void)
{
    wlan_send_reset();               /* reset / re-pair request (cmd 0x0D) */
}

/* ---- Power-mode selection (Raserei / Smart / Individual) ----
 * Selecting a mode applies it (g_status_mode, reported to the app as DP2) and shows
 * a brief confirmation, then auto-returns to home after ~3 s:
 *   mode 0 (Raserei)    -> the fire animation g_img_table[799..837] (user-confirmed)
 *   mode 1 (Smart)      -> page 18
 *   mode 2 (Individual) -> page 19
 * Stock used ui_18CE4(8)->page-slide animation; mine drew the wrong static page. */
/* Raserei animation = anim_state[8] (stock ui_18CE4(8,0)), frames 849..863 (15 frames) - verified
 * from the live stock anim_state table. (799..837 was idx 7, the WRONG/longer element animation.) */
#define FIRE_START 849u
#define FIRE_END   863u
static uint8_t  s_mode_disp;        /* 0 = idle, else (mode+1) for Smart/Individual */
static uint32_t s_mode_t0;          /* Smart/Individual auto-return timestamp */

void mode_stop(void) { s_mode_disp = 0; }   /* called from ui_set_page on any nav */

void mode_select(uint8_t mode)
{
    if (mode > 2u) return;
    /* Debounce: the app sends the mode DP more than once (on select AND on activate), which
     * re-triggered the animation -> "anim, home, anim again". Ignore the same mode re-sent within
     * 2.5 s, OR any re-trigger while a mode animation is still playing, so it plays cleanly once. */
    static uint32_t s_mode_dbnc; static uint8_t s_mode_last = 0xFF;
    uint32_t t_now = get_tick_ms();
    if (g_anim_active && (g_page_idx == 7u || g_page_idx == 8u)) return;   /* fire in progress */
    if (mode == s_mode_last && (uint32_t)(t_now - s_mode_dbnc) < 2500u) return;
    s_mode_dbnc = t_now; s_mode_last = mode;
    /* ITEM 4 (1:1 with stock wlan_cmd_set_config 0xB348 case 2): a mode change must
     * ACTIVELY set the device state, not just the display. Stock per mode:
     *   Raserei(1):  byte_1FFF8159=0, byte_1FFF8411=1, ui_18CE4(8,0), i2c_19DD4(0,0,0)
     *   Smart(2):    byte_1FFF8159=1, byte_1FFF8411=0, page18, fn_12EEC (zero limits), i2c_19DD4
     *   Individual(3): byte_1FFF8159=2, page19
     * (My DP2 maps val 1/2/3 -> mode 0/1/2.) */
    extern uint8_t g_flag_8411;                          /* byte_1FFF8411 */
    extern float   g_port_limit[12];                     /* per-port V/I limits */
    g_status_mode = mode;                                /* byte_1FFF8159 variant 0/1/2 */
    *(volatile uint8_t *)0x1FFF8415u = 0u;               /* i2c_19DD4(0,0,0): clear byte_1FFF8415 */
    s_mode_t0     = get_tick_ms();
    if (mode == 0u) {                            /* Raserei: stock ui_18CE4(8,0) */
        g_flag_8411 = 1u;                        /* stock byte_1FFF8411 = 1 */
        lcd_fill(0x0000);
        /* STOCK path: latch the fire animation via anim_state[8] (frames 799..837, one-shot),
         * driven by ui_animate_page's idx-8 branch, which ends with ui_set_page(17). Replaces the
         * old custom mode_tick fire loop (byte-1:1). */
        s_anim_start[8] = FIRE_START; s_anim_end[8] = FIRE_END; s_anim_cur[8] = FIRE_START;
        g_page_idx   = 8u;                       /* = g_menu_page[5] anim index */
        g_page_anim  = 0u;                        /* one-shot */
        g_anim_active = 1u;                       /* unk_1FFF8298 = 1: let ui_animate_page sweep */
        g_clock_mode = 1u;                        /* suppress idle clock during the anim */
        s_mode_disp  = 0u;                        /* no custom mode_tick fire */
    } else if (mode == 1u) {                     /* Smart */
        g_flag_8411 = 0u;                        /* stock byte_1FFF8411 = 0 */
        for (int i = 0; i < 12; i++) g_port_limit[i] = 0.0f;   /* fn_12EEC: clear per-port limits */
        ui_set_page(18u);                        /* page 18 = splash 1s -> home (self-contained) */
    } else {                                     /* Individual */
        ui_set_page(19u);                        /* page 19 = splash 1s -> home (self-contained) */
    }
    /* stock wlan_cmd_set_config tail calls update_outputs() after the mode (DP2) change -> push the
     * mode's module limits (Raserei preset / Smart distribution / Individual values) to the chips. */
    { extern void update_outputs(void); update_outputs(); }
}

/* 1 while ANY power-mode confirmation screen is up (Raserei fire OR Smart/Individual page) - the
 * main loop SUPPRESSES normal page/status rendering so it can't draw over the mode screen and
 * flicker. (Gating only Raserei reintroduced flicker on Smart/Individual - user-reported.) */
int mode_anim_active(void)
{
    /* Smart/Individual confirmation page up, OR the stock Raserei fire is sweeping (anim_state[8],
     * driven by ui_animate_page) - suppress normal page/status rendering either way. */
    return (s_mode_disp != 0u) || (g_anim_active && (g_page_idx == 7u || g_page_idx == 8u));
}

/* True only while a STATIC confirmation page (Smart/Individual) is up. The fw_main loop uses this to
 * suppress ui_animate_page for those pages - but NOT for the Raserei fire, which IS ui_animate_page. */
int mode_static_active(void) { return s_mode_disp != 0u; }

void mode_tick(void)
{
    if (!s_mode_disp) return;
    uint32_t now = get_tick_ms();
    g_idle_count = 0;                            /* hold off the idle-timeout while a mode page is up */
    /* Raserei no longer uses s_mode_disp - it runs through the stock anim_state[8] path in
     * ui_animate_page (set up by mode_select). Only Smart (2) / Individual (3) reach here:
     * static confirmation page, auto-return to HOME after ~1.5 s. */
    if (now - s_mode_t0 >= 1500u) {
        s_mode_disp = 0u;
        g_idle_count = 0;
        ui_set_page(0u);                         /* back to HOME */
    }
}
