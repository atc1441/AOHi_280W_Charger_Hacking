/* clockfont.h - letter glyph font for the clock-page location/weather text. */
#ifndef CLOCKFONT_H
#define CLOCKFONT_H
#include <stdint.h>
#define CLOCK_GLYPH_COUNT 67
typedef struct { uint32_t off; uint16_t size; uint16_t w, h; } clock_glyph_t;
extern const clock_glyph_t g_clock_glyphs[CLOCK_GLYPH_COUNT];
int  clock_char_index(uint8_t c);
void clock_draw_text(const char *s, uint16_t x, uint16_t y);
void clock_draw_loc(const char *s, uint16_t x, uint16_t y);  /* location: <=9 chars + "..." if longer */
#endif
