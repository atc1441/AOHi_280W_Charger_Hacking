/* dbgmenu.h - on-device debug page: live per-port I2C telemetry + link status.
 * Reached via a normal single-click (2nd page in the menu cycle). Page id 30. */
#ifndef APP_DBGMENU_H
#define APP_DBGMENU_H
#include <stdint.h>

#define DEBUG_PAGE 30

void ui_debug_enter(void);   /* ui_set_page case 30: clear + static labels */
void ui_debug_tick(void);    /* main loop on page 30: redraw the live values */

/* Small 5x7 text drawer (sx = screen horizontal 0..479, sy = vertical 0..199). */
void dbg_text(int sx, int sy, const char *s, uint16_t fg, uint16_t bg);

#endif /* APP_DBGMENU_H */
