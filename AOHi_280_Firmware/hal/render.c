/* render.c - RLE image blit from external flash to the LCD, ported 1:1 from the
 * stock lcd_blit_image (0xE104) + stream_decode (0xDD4C) onto the HC32F460 DDL HAL.
 *
 * Codec: opcode byte; high bit set -> RUN of (op&0x7F) pixels (value = next 2 bytes
 * big-endian); clear -> (op) LITERAL pixels (2 bytes each). Output bytes byte-swapped.
 * A 1024-byte source window is refilled from ext-flash as the decoder consumes it. */
#include "render.h"
#include "display.h"
#include "extflash.h"
#include <string.h>

typedef struct {
    uint8_t        state;       /* 0=opcode 1=run-hdr 2=run-emit 3=literal */
    uint8_t        count;
    uint16_t       runval;
    const uint8_t *src;
    uint32_t       src_avail;
    uint32_t       consumed;
    uint32_t       total;
    uint16_t      *dst;
    uint32_t       out_left;
} dec_t;

static uint8_t s_srcwin[1024];

static void fill_src(dec_t *d, uint32_t *pflash_off)
{
    uint32_t remaining = d->total - d->consumed;
    if (!remaining) { d->src = 0; d->src_avail = 0; return; }
    uint32_t want     = (remaining >> 10) ? 1024u : remaining;
    uint32_t leftover = d->src_avail;
    if (leftover) memmove(s_srcwin, d->src, leftover);
    uint32_t toread = (want == 1024u) ? (1024u - leftover) : want;
    extflash_read(*pflash_off, s_srcwin + leftover, toread);
    d->src       = s_srcwin;
    d->src_avail = (want == 1024u) ? 1024u : (toread + leftover);
    *pflash_off += toread;
}

static int stream_decode(dec_t *d, const uint16_t *dst_end)
{
    for (;;) {
        if (d->dst >= dst_end) return 0;
        switch (d->state) {
        case 0:
            if (d->src_avail) {
                int8_t op = (int8_t)*d->src++;
                d->src_avail--; d->consumed++;
                if (op < 0) { d->count = (uint8_t)(op + 0x80); d->state = 1; }
                else        { d->count = (uint8_t)op; d->state = 3; if (d->src_avail <= 1) return 1; }
                continue;
            }
            return d->consumed < d->total;
        case 1:
            if (d->src_avail <= 1) return d->consumed < d->total;
            d->runval  = (uint16_t)(*d->src++ << 8);
            d->runval |= *d->src++;
            d->src_avail -= 2; d->consumed += 2; d->state = 2;
            continue;
        case 2: {
            uint32_t room = (uint32_t)(dst_end - d->dst);
            if (!room) return 0;
            uint32_t n = (d->count >= room) ? room : d->count;
            for (uint32_t i = 0; i < n; i++) *d->dst++ = d->runval;
            d->count -= (uint8_t)n; d->out_left -= n;
            if (!d->count) d->state = 0;
            if (d->dst >= dst_end) return 0;
            continue;
        }
        case 3: {
            uint32_t room = (uint32_t)(dst_end - d->dst);
            if (!room) return 0;
            uint32_t n = (d->count >= room) ? room : d->count;
            if (d->src_avail <= 1) return d->consumed < d->total;
            if (n >= (d->src_avail >> 1)) n = d->src_avail >> 1;
            if (n) {
                for (uint32_t j = 0; j < n; j++) {
                    uint8_t b0 = d->src[2 * j], b1 = d->src[2 * j + 1];
                    d->dst[j] = (uint16_t)((b0 << 8) | b1);
                }
                d->dst += n; d->src += 2 * n;
                d->src_avail -= 2 * n; d->consumed += 2 * n;
                d->count -= (uint8_t)n; d->out_left -= n;
            }
            if (!d->count) d->state = 0;
            if (d->dst >= dst_end) return 0;
            if (d->src_avail <= 1 && d->consumed < d->total) return 1;
            continue;
        }
        default: return 0;
        }
    }
}

static void lcd_blit_image(uint32_t flash_off, uint32_t size, uint16_t w, uint16_t h)
{
    static uint16_t linebuf[480];
    dec_t d; memset(&d, 0, sizeof d);
    d.total = size;
    uint32_t foff = flash_off;
    fill_src(&d, &foff);
    for (uint16_t row = 0; row < h; row++) {
        extern void wdt_feed(void);
        wdt_feed();                              /* never let a slow blit trip the SWDT */
        d.dst = linebuf;
        const uint16_t *dst_end = linebuf + w;
        uint32_t guard = 4096u;                  /* bound the refill loop too */
        while (stream_decode(&d, dst_end) && --guard) fill_src(&d, &foff);
        lcd_send_pixels(linebuf, w);             /* sends LE + mirrors to shadow */
    }
}

void lcd_draw_element(uint32_t flash_off, uint32_t size,
                      uint16_t w, uint16_t h, uint16_t x, uint16_t y)
{
    lcd_set_window(x, y, w, h);
    lcd_start_ramwr();
    lcd_blit_image(flash_off, size, w, h);
    lcd_end_ramwr();
}
