/* render_num.c - numeric value rendering (clean-room from stock sub_150B4). */
#include "render_num.h"
#include "render.h"
#include "display.h"      /* lcd_fill_rect (stock sub_FBB4) */

static void draw_glyph(const img_entry_t *g, uint16_t x, uint16_t y)
{
    lcd_draw_element(g->flash_off, g->data_size, g->width, g->height, x, y);
}

/* render_num (stock 0x150B4), transcribed 1:1. Draws a right-aligned, leading-
 * zero-suppressed integer (3 cells) + optional fractional part + unit glyph,
 * stacked downward (UI rotated 90deg). pitch/cellW come from digTable[5] (uniform
 * digits); centering matches stock v20/v24. */
void render_num_x(uint16_t intval, uint8_t intdigits, uint8_t hasfrac, uint16_t fracval,
                  uint8_t fracdigits, const img_entry_t *fracTable, const img_entry_t *dotGlyph,
                  uint16_t x, uint16_t y, const img_entry_t *digTable, const img_entry_t *unitGlyph)
{
    uint16_t dig[3] = {0,0,0};
    { uint16_t t = intval; for (int i = 0; t && i < 3; i++) { dig[2-i] = (uint16_t)(t % 10u); t /= 10u; } }
    uint8_t fr[3] = {0,0,0};
    if (hasfrac) { uint16_t f = fracval; for (int i = 0; f && i < 3; i++) { fr[2-i] = (uint8_t)(f % 10u); f /= 10u; } }

    uint16_t pitch = digTable[5].height;
    uint16_t cellW = digTable[5].width;
    uint16_t uH = unitGlyph ? unitGlyph->height : 0;
    uint16_t uW = unitGlyph ? unitGlyph->width  : 0;

    int v22 = (dig[0] || dig[1]) ? (dig[0] ? 0 : 1) : 2;   /* leading-zero count */
    int v21 = 3 - v22;
    uint16_t v26 = (uint16_t)(uH + v21 * pitch);
    if (hasfrac) v26 = (uint16_t)(v26 + dotGlyph->height + fracdigits * pitch);
    uint16_t v20 = (uint16_t)(v26 + (3 - v21) * pitch / 2);
    uint16_t v24 = (y >= v20) ? (uint16_t)(y - v20) : y;

    /* STOCK render_num clears the digit strip FIRST (lcd_fill_rect / sub_FBB4) so a now-shorter
     * value can't leave stale pixels from a previous longer one ("old text half-overwritten").
     * The clear height v25 uses the FULL intdigits (not the leading-zero-suppressed count) and is
     * anchored at the baseline y, so it always covers the maximum extent. Color = black (page bg). */
    uint16_t v25 = (uint16_t)(uH + intdigits * pitch);
    if (hasfrac) v25 = (uint16_t)(v25 + dotGlyph->height + fracdigits * pitch + 10);
    uint16_t v23 = (y >= v25) ? (uint16_t)(y - v25) : y;
    lcd_fill_rect(x, v23, cellW, v25, 0x0000);

    for (int i = v22; i <= 2; i++) { draw_glyph(&digTable[dig[i]], x, v24); v24 = (uint16_t)(v24 + pitch); }
    if (hasfrac) {
        draw_glyph(dotGlyph, x, v24); v24 = (uint16_t)(v24 + dotGlyph->height);
        for (int j = 0; j < fracdigits; j++) {
            const img_entry_t *g = &fracTable[fr[3 - fracdigits + j]];
            draw_glyph(g, x, v24); v24 = (uint16_t)(v24 + g->height);
        }
    }
    if (unitGlyph) draw_glyph(unitGlyph, (uint16_t)(x + cellW - uW), v24);
}

void render_number(uint16_t value, uint8_t frac, uint16_t x, uint16_t y,
                   const img_entry_t *digits, const img_entry_t *dot,
                   const img_entry_t *unit, const uint8_t *frac_digits)
{
    /* Split integer part into 3 digits, hundreds..ones (stock word_2000F610). */
    uint16_t d[3] = {0,0,0};
    uint16_t t = value;
    for (int i = 0; i < 3 && t; i++) { d[2 - i] = t % 10u; t /= 10u; }

    /* Leading-zero suppression: skip = number of leading zero cells. */
    uint8_t skip;
    if (d[0] || d[1]) skip = d[0] ? 0 : 1;
    else              skip = 2;

    uint16_t stride = digits[5].height;   /* cell pitch = glyph[5] height */
    uint16_t cy = y;

    for (uint8_t i = skip; i <= 2; i++) {
        draw_glyph(&digits[d[i]], x, cy);
        cy += stride;
    }

    if (frac && dot) {
        draw_glyph(dot, x, cy);
        cy += dot->height;
        for (uint8_t j = 0; j < frac; j++) {
            uint8_t fd = frac_digits ? frac_digits[j] : 0;
            draw_glyph(&digits[fd], x, cy);
            cy += digits[fd].height;
        }
    }

    if (unit)
        draw_glyph(unit, x, cy);
}

void render_float_2dp(float v, uint16_t x, uint16_t y,
                      const img_entry_t *digits, const img_entry_t *dot,
                      const img_entry_t *unit)
{
    uint16_t whole = (uint16_t)v;
    uint16_t frac100 = (uint16_t)((v - (float)whole) * 100.0f + 0.5f);
    if (frac100 >= 100) { whole++; frac100 = 0; }   /* carry (stock behaviour) */
    uint8_t fd[2] = { (uint8_t)(frac100 / 10), (uint8_t)(frac100 % 10) };
    render_number(whole, 2, x, y, digits, dot, unit, fd);
}
