/* graphmenu.c - per-port live WATT graph (page 31): scrolling W line + horizontal value grid.
 *
 * Panel is mounted rotated 90 deg (see dbgmenu.c): screen-HORIZONTAL = framebuffer RASET (480
 * axis), screen-VERTICAL = framebuffer CASET (200 axis, flipped). All drawing goes through
 * g_fill()/dbg_text() (the SAME proven mapping as the debug menu).
 *
 * Layout: left margin (x < GLX) holds ONLY the watt numbers (drawn once on enter; nothing else
 * ever paints there). The plot (x >= GLX) has 5 horizontal value gridlines and the yellow W line.
 * The W line is drawn ON TOP of the gridlines (so it stays solid - no dotting) and updated
 * DIFFERENTIALLY: each frame only the columns whose level changed are touched (erase old segment +
 * restore any gridline it covered, then draw the new segment). The background is never cleared, so
 * there is no flicker. The line scrolls right->left. Header (all 3 values) is right-aligned. */
#include "graphmenu.h"
#include "dbgmenu.h"        /* dbg_text */
#include "menu_state.h"
#include "display.h"        /* lcd_fill, lcd_fill_rect, LCD_WIDTH(200)/LCD_HEIGHT(480) */
#include "wdt.h"
#include "tick.h"
#include <string.h>

#define GY   28            /* plot top (header above)            */
#define GH   168           /* plot height -> value rows sy 28..196 */
#define GLX  40            /* plot/grid left edge; numbers live left of it */
#define NX   110           /* time columns (NX*CW = 440 = GLX..480) */
#define CW   4             /* px per column                      */
#define LH   4             /* W-line thickness                   */
#define GSAMPLE_MS 400u

#define C_W    0xFFE0      /* watt line = yellow */
#define C_GRID 0x528A      /* grid = medium grey */
#define C_AXIS 0xFFFF      /* labels = white     */
#define C_BG   0x0000

extern menu_state_t g_menu_state;
extern float g_port_maxw[];

static const char *const k_port[6] = { "C1", "C2", "C3", "C4", "A1", "A2" };
static uint8_t  s_port;
static uint8_t  s_w[NX];           /* W level history 0..GH, [NX-1] = newest */
static uint8_t  s_lo[NX], s_hi[NX];/* the line span currently DRAWN per column (for change-detect) */
static char     c_hdr[48];
static float    s_wmax;            /* full-scale currently drawn on the Y axis */
static uint16_t s_colbuf[GH * CW]; /* one column's composed pixels (blitted in a single write) */

static int gline_y(int k) { return GY + (GH - k * GH / 4); }   /* screen-y of value line k (0..4) */

static float wfull(uint8_t port)
{
    float m = g_port_maxw[port];
    if (m < 1.0f) m = (port == 0) ? 240.0f : (port < 4) ? 100.0f : 30.0f;
    return m;
}

static void g_fill(int sx, int sy, int sw, int sh, uint16_t c)
{
    if (sy < 0)               { sh += sy; sy = 0; }
    if (sy + sh > LCD_WIDTH)    sh = LCD_WIDTH - sy;
    if (sx < 0)               { sw += sx; sx = 0; }
    if (sx + sw > LCD_HEIGHT)   sw = LCD_HEIGHT - sx;
    if (sw <= 0 || sh <= 0) return;
    lcd_fill_rect(LCD_WIDTH - sy - sh, sx, sh, sw, c);
}

static int sval(float v, float vmax)
{
    if (v < 0) v = 0;
    int y = (int)(v * GH / vmax + 0.5f);
    if (y < 0) y = 0;
    if (y > GH) y = GH;
    return y;
}
static int liney(int lvl)
{
    int y = GY + (GH - lvl);
    if (y < GY)           y = GY;
    if (y > GY + GH - LH) y = GY + GH - LH;
    return y;
}

static void pad3(char *b, int v)
{
    if (v > 999) v = 999;
    b[0] = (v >= 100) ? (char)('0' + (v / 100) % 10) : ' ';
    b[1] = (v >= 10)  ? (char)('0' + (v / 10) % 10)  : ' ';
    b[2] = (char)('0' + v % 10);
    b[3] = 0;
}

/* horizontal value gridlines across the plot (NO vertical lines - they chopped the W line). */
static void draw_gridlines(void)
{
    for (int k = 0; k <= 4; k++)
        g_fill(GLX, gline_y(k) - 1, LCD_HEIGHT - GLX, 2, C_GRID);
}

/* watt numbers in the left margin - drawn ONCE on enter; nothing else paints there. Each is
 * vertically clamped fully on-screen (below the header, above the bottom edge). */
static void draw_labels(float wmax)
{
    g_fill(0, GY - 8, GLX - 2, GH + 16, C_BG);     /* clear the whole label margin once */
    for (int k = 0; k <= 4; k++) {
        int sy = gline_y(k) - 8;                   /* centre the 16px cell on the line */
        if (sy < 19)        sy = 19;               /* keep clear of the header           */
        if (sy > 184)       sy = 184;              /* fx0 = 184 - sy must be >= 0 (else dbg_char skips) */
        char b[4]; pad3(b, (int)(k * wmax / 4.0f + 0.5f));
        dbg_text(4, sy, b, C_AXIS, C_BG);
    }
}

void ui_graph_enter(void)
{
    lcd_fill(C_BG);
    memset(s_w, 0, sizeof s_w);
    memset(s_lo, 0, sizeof s_lo);                  /* nothing drawn yet (lo==hi==0) */
    memset(s_hi, 0, sizeof s_hi);
    c_hdr[0] = 0;
    s_wmax = wfull(s_port);
    draw_gridlines();
    draw_labels(s_wmax);
}

void ui_graph_select_port(void)
{
    s_port = (uint8_t)((s_port + 1u) % 6u);
    ui_graph_enter();
}

/* header */
static int put(char *b, int p, char c) { b[p] = c; return p + 1; }
static int put_u(char *b, int p, uint32_t v, int w)
{
    char t[8]; int n = 0;
    do { t[n++] = (char)('0' + v % 10u); v /= 10u; } while (v);
    for (int i = n; i < w; i++) p = put(b, p, ' ');
    while (n) p = put(b, p, t[--n]);
    return p;
}
static int put_f1(char *b, int p, float v, int iw)
{ if (v<0) v=0; uint32_t s=(uint32_t)(v*10.0f+0.5f); p=put_u(b,p,s/10u,iw); p=put(b,p,'.'); return put(b,p,(char)('0'+s%10u)); }
static int put_s(char *b, int p, const char *s) { while (*s) p = put(b, p, *s++); return p; }

void ui_graph_tick(void)
{
    static uint32_t t;
    uint32_t now = get_tick_ms();
    if ((uint32_t)(now - t) < GSAMPLE_MS) return;
    t = now;

    const menu_outlet_t *o = &g_menu_state.outlet[s_port];
    float wmax = wfull(s_port);

    /* The configured max can change while this page is open. The Y scale then changes, so any line
     * already on screen (especially FLAT stretches my differential skips as "unchanged") would be
     * stranded at the old scale = ghost lines. So on a max change do a FULL reset: clear the plot,
     * redraw grid + labels, and restart the history at the new scale. */
    if ((int)(wmax + 0.5f) != (int)(s_wmax + 0.5f)) {
        s_wmax = wmax;
        g_fill(GLX, GY - 1, NX * CW, GH + 2, C_BG);   /* clear the plot area */
        draw_gridlines();
        draw_labels(wmax);
        memset(s_w, 0, sizeof s_w);
        memset(s_lo, 0, sizeof s_lo);
        memset(s_hi, 0, sizeof s_hi);
    }

    memmove(s_w, s_w + 1, NX - 1);
    s_w[NX - 1] = (uint8_t)sval(o->power, wmax);

    /* header: all three live values + the configured MAX, RIGHT-aligned (12 px/char) */
    char b[48]; int p = 0;
    p = put_s(b, p, k_port[s_port]); p = put_s(b, p, " W:");
    p = put_u(b, p, (uint32_t)(o->power + 0.5f), 3);
    p = put_s(b, p, " V:"); p = put_f1(b, p, o->volt, 2);
    p = put_s(b, p, " A:"); p = put_f1(b, p, o->cur, 2);
    p = put_s(b, p, " MAX:"); p = put_u(b, p, (uint32_t)(wmax + 0.5f), 3);
    b[p] = 0;
    if (strcmp(b, c_hdr)) {
        strcpy(c_hdr, b);
        g_fill(GLX, 0, LCD_HEIGHT - GLX, 18, C_BG);          /* clear header band (right of labels) */
        int hx = LCD_HEIGHT - p * 12; if (hx < GLX) hx = GLX;
        dbg_text(hx, 2, b, C_AXIS, C_BG);
    }

    /* SCANLINE method: the line is stored per column (s_w). For each column whose content changed,
     * COMPOSE the whole column one pixel at a time - deciding, per pixel, whether it is the W line,
     * a gridline, or background - into a buffer, then blit that column in a SINGLE write. One pass,
     * one write per column, only for changed columns -> no erase step, no flash, no ghosting. */
    int gy[5];
    for (int k = 0; k < 5; k++) gy[k] = gline_y(k);
    const int fx0 = LCD_WIDTH - GY - GH;                     /* column window CASET origin */
    for (int i = 0; i < NX; i++) {
        int na = liney(s_w[i]);
        int nb = (i > 0) ? liney(s_w[i - 1]) : na;
        int nlo = na < nb ? na : nb;                         /* connected span [nlo, nhi) */
        int nhi = (na > nb ? na : nb) + LH;

        if (s_lo[i] == nlo && s_hi[i] == nhi) continue;      /* unchanged column -> skip */

        for (int c = 0; c < GH; c++) {                       /* compose top..bottom, per pixel */
            int y = GY + GH - 1 - c;                         /* screen-y for this buffer row */
            uint16_t col = C_BG;
            if (y >= nlo && y < nhi) {
                col = C_W;                                   /* the W line (priority) */
            } else {
                for (int k = 0; k < 5; k++)
                    if (y == gy[k] - 1 || y == gy[k]) { col = C_GRID; break; }  /* gridline */
            }
            for (int r = 0; r < CW; r++) s_colbuf[r * GH + c] = col;
        }
        lcd_set_window(fx0, GLX + i * CW, GH, CW);
        lcd_start_ramwr();
        lcd_send_pixels(s_colbuf, GH * CW);
        lcd_end_ramwr();

        s_lo[i] = (uint8_t)nlo;
        s_hi[i] = (uint8_t)nhi;
        if ((i & 15) == 0) wdt_feed();
    }
    wdt_feed();
}
