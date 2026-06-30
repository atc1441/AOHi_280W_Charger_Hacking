/* video.c - plays an internal rickroll clip scaled to the full screen height
 * (aspect ratio preserved), centred. Stored tiny (VIDEO_W x VIDEO_H, 8-bit
 * palette, already rotated for the panel) in internal flash; upscaled UP x.
 *
 * Window = VIDEO_W*UP x VIDEO_H*UP = 200 x 356, centred in the 200x480 portrait
 * panel -> full height + 16:9 width in the landscape view. */
#include "video.h"
#include "video_data.h"
#include "display.h"
#include "ui_state.h"   /* g_menu_page */
#include "tick.h"       /* get_tick_ms, g_idle_count */
#include <stdint.h>

#define UP      4
#define WIN_W   (VIDEO_W * UP)                  /* 200 */
#define WIN_H   (VIDEO_H * UP)                  /* 356 = landscape video width */
#define WIN_X   0
#define WIN_Y   0                               /* video at the left edge (landscape) */

static uint8_t  s_active;
static uint16_t s_frame;
static uint32_t s_last;
static uint8_t  s_line[WIN_W * 2];              /* one upscaled portrait row, hi,lo */

/* Static credit text panel, drawn once next to the video (right side in landscape).
 * 8-bit grayscale -> RGB565 gray. Window = (0, WIN_H, TEXT_W, TEXT_H) = (0,356,200,124). */
static void video_draw_text(void)
{
    lcd_set_window(0, WIN_H, TEXT_W, TEXT_H);
    lcd_start_ramwr();
    for (int row = 0; row < TEXT_H; row++) {
        const uint8_t *p = &text_panel[row * TEXT_W];
        for (int x = 0; x < TEXT_W; x++) {
            uint8_t g = p[x];
            uint16_t c = (uint16_t)(((g & 0xF8) << 8) | ((g & 0xFC) << 3) | (g >> 3));
            s_line[2 * x]     = (uint8_t)(c >> 8);
            s_line[2 * x + 1] = (uint8_t)c;
        }
        lcd_push_bytes(s_line, TEXT_W * 2);
    }
    lcd_end_ramwr();
}

void video_start(void)
{
    lcd_fill(0x0000);
    video_draw_text();          /* drawn once; video_tick then only touches the video window */
    s_frame = 0;
    s_last  = 0;
    s_active = 1;
}

void video_stop(void) { s_active = 0; }

void video_tick(void)
{
    if (!s_active || g_menu_page != VIDEO_PAGE) { s_active = 0; return; }

    uint32_t now = get_tick_ms();
    if (now - s_last < (1000u / VIDEO_FPS)) return;
    s_last = now;

    const uint8_t *fr = &video_frames[(uint32_t)s_frame * VIDEO_W * VIDEO_H];

    lcd_set_window(WIN_X, WIN_Y, WIN_W, WIN_H);
    lcd_start_ramwr();
    for (int sy = 0; sy < VIDEO_H; sy++) {           /* each stored row -> UP screen rows */
        const uint8_t *row = &fr[sy * VIDEO_W];
        for (int sx = 0; sx < VIDEO_W; sx++) {
            uint16_t c = video_palette[row[sx]];
            uint8_t hi = (uint8_t)(c >> 8), lo = (uint8_t)c;
            int base = sx * UP * 2;
            for (int u = 0; u < UP; u++) { s_line[base + 2 * u] = hi; s_line[base + 2 * u + 1] = lo; }
        }
        for (int r = 0; r < UP; r++) lcd_push_bytes(s_line, sizeof s_line);
    }
    lcd_end_ramwr();

    if (++s_frame >= VIDEO_NFRAMES) s_frame = 0;
    g_idle_count = 0;
}
