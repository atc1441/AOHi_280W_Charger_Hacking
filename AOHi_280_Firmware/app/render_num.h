/* render_num.h - numeric value rendering (clean-room from stock sub_150B4/137B0).
 *
 * Draws a number as a vertical stack of digit-glyph sprites with optional
 * fractional part, decimal point and unit suffix. Glyph tables are img_entry_t
 * arrays indexed by digit 0..9; the cell stride is glyph[5]'s width/height. */
#ifndef APP_RENDER_NUM_H
#define APP_RENDER_NUM_H
#include <stdint.h>
#include "assets.h"

/* Render integer `value` with `frac` fractional digits at column x, baseline y.
 *   digits : 10-entry glyph table (index = digit)
 *   dot    : decimal-point glyph   (NULL to omit / frac==0)
 *   unit   : unit suffix glyph     (NULL to omit) */
void render_num_x(uint16_t intval, uint8_t intdigits, uint8_t hasfrac, uint16_t fracval, uint8_t fracdigits, const img_entry_t *fracTable, const img_entry_t *dotGlyph, uint16_t x, uint16_t y, const img_entry_t *digTable, const img_entry_t *unitGlyph);
void render_number(uint16_t value, uint8_t frac, uint16_t x, uint16_t y,
                   const img_entry_t *digits, const img_entry_t *dot,
                   const img_entry_t *unit, const uint8_t *frac_digits);

/* Render a float (stock sub_137B0): integer part + 2 fractional digits. */
void render_float_2dp(float v, uint16_t x, uint16_t y,
                      const img_entry_t *digits, const img_entry_t *dot,
                      const img_entry_t *unit);

#endif /* APP_RENDER_NUM_H */
