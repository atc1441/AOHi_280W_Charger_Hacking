/* graphmenu.h - scrolling per-port history graph (W / V / A line chart), page 31.
 * Single-click reaches it (home -> debug -> graph -> ...); double-click cycles the
 * shown port (C1..A2). */
#ifndef APP_GRAPHMENU_H
#define APP_GRAPHMENU_H
#include <stdint.h>

#define GRAPH_PAGE 31

void ui_graph_enter(void);        /* ui_set_page case 31: clear + axes + reset history */
void ui_graph_tick(void);         /* main loop on page 31: sample + scroll + redraw    */
void ui_graph_select_port(void);  /* double-click: advance to the next port            */

#endif /* APP_GRAPHMENU_H */
