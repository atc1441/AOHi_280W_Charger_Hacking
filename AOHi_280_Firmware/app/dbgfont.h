/* dbgfont.h - compact 5x7 ASCII font for the debug page (data in dbgfont.c). */
#ifndef APP_DBGFONT_H
#define APP_DBGFONT_H
#include <stdint.h>

extern const uint8_t g_dbgfont[];
/* 5 column bytes (bit r = row r, top..bottom) for char c (space if unsupported). */
const uint8_t *dbgfont_glyph(char c);

#endif /* APP_DBGFONT_H */
