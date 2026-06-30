/* clockfont.c - letter/punctuation glyph font for the clock-page text, 1:1 from
 * stock (font table @0x1A9D4: 67 glyphs x 16B {ext_flash_off, rle_size, w, h};
 * glyph pixels live in ext-flash 0x108349+). char->index map = stock 0x155E0,
 * renderer = stock 0x138E8 (per char draw the glyph, advance the cursor by h - the
 * panel is rotated 90deg so text flows along +y). No digits in this font. */
#include "clockfont.h"
#include "render.h"
#include "wdt.h"

const clock_glyph_t g_clock_glyphs[CLOCK_GLYPH_COUNT] = {
    { 0x108349u, 0x00D5u,  22,  10 },
    { 0x10841Eu, 0x00CEu,  22,  10 },
    { 0x1084ECu, 0x00BFu,  22,  10 },
    { 0x1085ABu, 0x00CFu,  22,  10 },
    { 0x10867Au, 0x00E1u,  22,  10 },
    { 0x10875Bu, 0x0075u,  22,   6 },
    { 0x1087D0u, 0x011Du,  22,  10 },
    { 0x1088EDu, 0x0078u,  22,  10 },
    { 0x108965u, 0x003Bu,  22,   4 },
    { 0x1089A0u, 0x006Cu,  22,   6 },
    { 0x108A0Cu, 0x00ACu,  22,  10 },
    { 0x108AB8u, 0x0043u,  22,   6 },
    { 0x108AFBu, 0x00C3u,  22,  16 },
    { 0x108BBEu, 0x0072u,  22,  10 },
    { 0x108C30u, 0x00D3u,  22,  10 },
    { 0x108D03u, 0x00DAu,  22,  10 },
    { 0x108DDDu, 0x00D2u,  22,  10 },
    { 0x108EAFu, 0x004Eu,  22,   8 },
    { 0x108EFDu, 0x00D3u,  22,  10 },
    { 0x108FD0u, 0x0090u,  22,   8 },
    { 0x109060u, 0x0087u,  22,  10 },
    { 0x1090E7u, 0x00ACu,  22,  10 },
    { 0x109193u, 0x0125u,  22,  16 },
    { 0x1092B8u, 0x00CDu,  22,  10 },
    { 0x109385u, 0x00D9u,  22,  10 },
    { 0x10945Eu, 0x00C1u,  22,  10 },
    { 0x10951Fu, 0x0121u,  22,  14 },
    { 0x109640u, 0x012Fu,  22,  12 },
    { 0x10976Fu, 0x0117u,  22,  12 },
    { 0x109886u, 0x00F3u,  22,  14 },
    { 0x109979u, 0x00F7u,  22,  12 },
    { 0x109A70u, 0x00C4u,  22,  12 },
    { 0x109B34u, 0x0153u,  22,  12 },
    { 0x109C87u, 0x008Bu,  22,  14 },
    { 0x109D12u, 0x002Cu,  22,   6 },
    { 0x109D3Eu, 0x007Cu,  22,  10 },
    { 0x109DBAu, 0x00F5u,  22,  14 },
    { 0x109EAFu, 0x0069u,  22,  12 },
    { 0x109F18u, 0x00F7u,  22,  14 },
    { 0x10A00Fu, 0x00C9u,  22,  14 },
    { 0x10A0D8u, 0x0129u,  22,  12 },
    { 0x10A201u, 0x00D9u,  22,  12 },
    { 0x10A2DAu, 0x0145u,  22,  12 },
    { 0x10A41Fu, 0x011Eu,  22,  12 },
    { 0x10A53Du, 0x013Du,  22,  12 },
    { 0x10A67Au, 0x008Cu,  22,  12 },
    { 0x10A706u, 0x00BBu,  22,  12 },
    { 0x10A7C1u, 0x00EEu,  22,  12 },
    { 0x10A8AFu, 0x01B2u,  22,  18 },
    { 0x10AA61u, 0x011Cu,  22,  12 },
    { 0x10AB7Du, 0x00B4u,  22,  10 },
    { 0x10AC31u, 0x0120u,  22,  12 },
    { 0x10AD51u, 0x0023u,  20,   6 },
    { 0x10AD74u, 0x0049u,  20,   6 },
    { 0x10ADBDu, 0x0031u,  20,   6 },
    { 0x10ADEEu, 0x005Au,  20,   6 },
    { 0x10AE48u, 0x002Du,  20,   6 },
    { 0x10AE75u, 0x0054u,  20,   8 },
    { 0x10AEC9u, 0x0064u,  20,   6 },
    { 0x10AF2Du, 0x006Eu,  20,   6 },
    { 0x10AF9Bu, 0x00C5u,  20,  10 },
    { 0x10B060u, 0x0066u,  20,   6 },
    { 0x10B0C6u, 0x0097u,  20,  10 },
    { 0x10B15Du, 0x004Fu,  20,   8 },
    { 0x10B1ACu, 0x0093u,  20,  10 },
    { 0x10B23Fu, 0x00ABu,  20,   8 },
    { 0x10B2EAu, 0x0081u,  20,  10 },
};

/* stock char_to_glyph @0x155E0: a-z->0..25, A-Z->26..51, then punctuation.
 * Returns -1 for unmapped chars (digits, space, '#', ...). */
int clock_char_index(uint8_t c)
{
    if (c >= 'a' && c <= 'z') return c - 'a';            /* 0..25  */
    if (c >= 'A' && c <= 'Z') return (c - 'A') + 26;     /* 26..51 */
    switch (c) {
    case '.': return 52; case ':': return 53; case ',': return 54; case ';': return 55;
    case 0x27: return 56; case '"': return 57; case '[': return 58; case '!': return 59;
    case '?': return 60; case ']': return 61; case '+': return 62; case '-': return 63;
    case '*': return 64; case '/': return 65; case '=': return 66;
    }
    return -1;
}

/* Draw a NUL-terminated string with the letter font (stock 0x138E8). x is fixed;
 * each glyph advances the cursor along +y by its height (rotated panel). */
void clock_draw_text(const char *s, uint16_t x, uint16_t y)
{
    for (; *s; s++) {
        int i = clock_char_index((uint8_t)*s);
        if (i < 0 || i >= CLOCK_GLYPH_COUNT) { y += 11; continue; }  /* gap for unmapped */
        const clock_glyph_t *g = &g_clock_glyphs[i];
        lcd_draw_element(g->off, g->size, g->w, g->h, x, y);
        y += g->h;
        wdt_feed();
    }
}

/* clock_draw_loc (stock sub_138E8): the clock-page LOCATION name. Clears the 22x144 strip, draws at
 * most 9 characters, then appends "..." when the name is 10+ chars long (the stock truncation). */
void clock_draw_loc(const char *s, uint16_t x, uint16_t y)
{
    extern void lcd_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
    lcd_fill_rect(x, y, 22, 144, 0x0000);            /* stock: lcd_fill_rect(a2,a3,22,144) */
    int len = 0; while (s[len]) len++;
    int n = (len < 10) ? len : 9;                    /* stock: min(strlen,9) */
    for (int k = 0; k < n; k++) {
        int i = clock_char_index((uint8_t)s[k]);
        if (i < 0 || i >= CLOCK_GLYPH_COUNT) continue;   /* stock skips unmapped (no advance) */
        const clock_glyph_t *g = &g_clock_glyphs[i];
        lcd_draw_element(g->off, g->size, g->w, g->h, x, y);
        y += g->h;
        wdt_feed();
    }
    if (len >= 10) {                                 /* stock: append '.' x3 */
        int i = clock_char_index('.');
        if (i >= 0 && i < CLOCK_GLYPH_COUNT) {
            const clock_glyph_t *g = &g_clock_glyphs[i];
            for (int j = 0; j < 3; j++) {
                lcd_draw_element(g->off, g->size, g->w, g->h, x, y);
                y += g->h;
            }
        }
    }
}
