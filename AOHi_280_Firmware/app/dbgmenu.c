/* dbgmenu.c - on-device debug page (id 30): live per-port I2C telemetry, total
 * power, temps, RTC and the UART/WLAN link state, drawn with a 5x7 font scaled 2x
 * to fill the panel.
 *
 * The panel is mounted rotated 90 deg: framebuffer-y (the 480 axis) is the screen
 * HORIZONTAL (low y = left) and framebuffer-x (the 200 axis) is the screen VERTICAL
 * (top-bottom flipped). dbg_char maps an upright (optionally scaled) glyph into
 * that frame. Reached as the 2nd page of the normal single-click cycle. Every value
 * is refreshed live (~10 Hz) with per-line change-gating so it never flickers. */
#include "dbgmenu.h"
#include "dbgfont.h"
#include "menu_state.h"
#include "wlan.h"
#include "display.h"      /* lcd_set_window/start/end ramwr, lcd_send_pixels, LCD_WIDTH/HEIGHT */
#include "rtc.h"
#include "wdt.h"
#include "tick.h"
#include <string.h>

#define GW 5              /* glyph width  (px) */
#define GH 7              /* glyph height (px) */
#define CW 6              /* cell width  (glyph + 1 gap) */
#define CH 8              /* cell height (glyph + 1 gap) */
#define SC 2              /* scale: 2x -> 12x16 px cells, fills the 480x200 panel */

#define COL_TITLE 0xFFFF  /* white  */
#define COL_LABEL 0x07FF  /* cyan   */
#define COL_VAL   0xFFE0  /* yellow */
#define COL_OK    0x07E0  /* green  */
#define COL_OFF   0xF800  /* red    */
#define COL_BG    0x0000

extern menu_state_t g_menu_state;
extern volatile uint8_t g_wlan_alive;
extern uint8_t g_wlan_conn;
extern rtc_time_t g_rtc;
extern volatile uint8_t g_port_capw[6];   /* current per-port max (W); 0 = no SW cap */
extern uint8_t g_port_enable[];           /* per-port output enable (software on/off) */

/* ---- scaled glyph blit ---- */
static uint16_t s_cell[CW * CH * SC * SC];

static void dbg_char(int sx, int sy, char ch, uint16_t fg, uint16_t bg)
{
    const uint8_t *g = dbgfont_glyph(ch);
    const int w = CH * SC;                 /* window extent on the fb-x (200) axis */
    const int h = CW * SC;                 /* window extent on the fb-y (480) axis */
    for (int i = 0; i < w * h; i++) {
        int fx = i % w;                    /* window scans x (200 axis) fast */
        int fy = i / w;
        int c = fy / SC;                   /* glyph column 0..4 (5=gap)      */
        int r = (CH - 1) - (fx / SC);      /* glyph row 0(top)..6 (flip)     */
        int on = (c < GW && r < GH) ? ((g[c] >> r) & 1) : 0;
        s_cell[i] = on ? fg : bg;
    }
    int fx0 = LCD_WIDTH - sy - w;          /* flip top-bottom */
    int fy0 = sx;
    if (fx0 < 0 || fy0 < 0 || fy0 + h > LCD_HEIGHT) return;
    lcd_set_window(fx0, fy0, w, h);
    lcd_start_ramwr();
    lcd_send_pixels(s_cell, w * h);
    lcd_end_ramwr();
}

void dbg_text(int sx, int sy, const char *s, uint16_t fg, uint16_t bg)
{
    for (; *s; s++) { dbg_char(sx, sy, *s, fg, bg); sx += CW * SC; }
}

/* ---- tiny formatting into a fixed buffer ---- */
static int put(char *b, int p, char c) { b[p] = c; return p + 1; }
static int put_u(char *b, int p, uint32_t v, int width)
{
    char t[10]; int n = 0;
    do { t[n++] = (char)('0' + v % 10u); v /= 10u; } while (v);
    for (int i = n; i < width; i++) p = put(b, p, ' ');
    while (n) p = put(b, p, t[--n]);
    return p;
}
static int put_s(char *b, int p, const char *s) { while (*s) p = put(b, p, *s++); return p; }
static int put_f1(char *b, int p, float v, int iw)
{
    if (v < 0) v = 0;
    uint32_t scaled = (uint32_t)(v * 10.0f + 0.5f);
    p = put_u(b, p, scaled / 10u, iw);
    p = put(b, p, '.');
    p = put(b, p, (char)('0' + scaled % 10u));
    return p;
}

static const char *const k_port[6] = { "C1", "C2", "C3", "C4", "A1", "A2" };

/* layout (sy = vertical 0..199, lines stack down; X0 = left horizontal margin) */
#define X0       6
#define COLW     (CW * SC)        /* one character step (12 px) */
#define SY_STAT  22
#define SY_TIME  40
#define SY_TEMP  58
#define SY_HEAD  80
#define SY_ROW0  100
#define DSY      16
#define VAL_COL  6                /* value text starts at char column 6 */
#define EN_COL   28               /* output-enabled column (g_port_enable) */

/* per-line caches for change-gating; ui_debug_enter forces a full redraw */
static char     c_stat[40], c_time[40], c_temp[40], c_row[6][24];
static uint8_t  c_en[6];
static uint8_t  s_force;

void ui_debug_enter(void)
{
    lcd_fill(COL_BG);
    dbg_text(X0, 2, "AOHi DEBUG", COL_TITLE, COL_BG);
    dbg_text(X0, SY_HEAD, "PORT  VOLT  AMP   WATT MAX  EN", COL_LABEL, COL_BG);
    for (int i = 0; i < 6; i++)
        dbg_text(X0, SY_ROW0 + i * DSY, k_port[i], COL_LABEL, COL_BG);
    /* invalidate caches so the next tick draws every value afresh */
    c_stat[0] = c_time[0] = c_temp[0] = 0;
    for (int i = 0; i < 6; i++) { c_row[i][0] = 0; c_en[i] = 0xFF; }
    s_force = 1;
}

static void line(int sx, int sy, const char *b, char *cache, uint16_t col)
{
    if (s_force || strcmp(b, cache)) { strcpy(cache, b); dbg_text(sx, sy, b, col, COL_BG); }
}

void ui_debug_tick(void)
{
    static uint32_t s_t;
    uint32_t now = get_tick_ms();
    if (!s_force && (uint32_t)(now - s_t) < 100u) return;   /* ~10 Hz */
    s_t = now;

    const menu_state_t *m = &g_menu_state;
    char b[44]; int p;

    p = 0;
    p = put_s(b, p, "UART:");  p = put_s(b, p, g_wlan_alive ? "UP " : "-- ");
    p = put_s(b, p, "WLAN:");  p = put_s(b, p, g_wlan_conn ? "CON " : "OFF ");
    p = put_s(b, p, "MODE:");
    { uint8_t md = *(volatile uint8_t *)0x1FFF8159u;
      p = put_s(b, p, md == 0 ? "RAGE" : md == 2 ? "INDV" : "SMRT"); }
    b[p] = 0; line(X0, SY_STAT, b, c_stat, COL_VAL);

    p = 0;
    p = put_s(b, p, "TIME ");
    p = put_u(b, p, g_rtc.hour, 2); p = put(b, p, ':');
    p = put_u(b, p, g_rtc.min, 2);  p = put(b, p, ':');
    p = put_u(b, p, g_rtc.sec, 2);
    p = put_s(b, p, " WD"); p = put_u(b, p, g_rtc.weekday, 1);
    p = put_s(b, p, " PWR"); p = put_u(b, p, m->total_w, 4); p = put_s(b, p, "W");
    b[p] = 0; line(X0, SY_TIME, b, c_time, COL_VAL);

    p = 0;
    p = put_s(b, p, "TEMP A:"); p = put_f1(b, p, m->tempA, 2);
    p = put_s(b, p, "C B:");    p = put_f1(b, p, m->tempB, 2); p = put_s(b, p, "C");
    b[p] = 0; line(X0, SY_TEMP, b, c_temp, COL_VAL);

    for (int i = 0; i < 6; i++) {
        const menu_outlet_t *o = &m->outlet[i];
        int sy = SY_ROW0 + i * DSY;
        p = 0;
        p = put_f1(b, p, o->volt, 2);  p = put_s(b, p, "  ");
        p = put_f1(b, p, o->cur, 2);   p = put_s(b, p, "  ");
        p = put_u(b, p, (uint32_t)(o->power + 0.5f), 3); p = put_s(b, p, "  ");
        if (i >= 4)              p = put_s(b, p, "22.5");          /* A1/A2 USB-A fixed max */
        else if (g_port_capw[i]) p = put_u(b, p, g_port_capw[i], 4); /* current per-port max (W) */
        else                     p = put_s(b, p, "  --");
        b[p] = 0;
        line(X0 + VAL_COL * COLW, sy, b, c_row[i], COL_VAL);
        uint8_t en = g_port_enable[i];                     /* output enabled (software on/off) */
        if (s_force || c_en[i] != en) {
            c_en[i] = en;
            dbg_text(X0 + EN_COL * COLW, sy, en ? "Y" : "n", en ? COL_OK : COL_OFF, COL_BG);
        }
        wdt_feed();
    }
    s_force = 0;
}
