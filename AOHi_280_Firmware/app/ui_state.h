#ifndef UI_STATE_H
#define UI_STATE_H
#include <stdint.h>

/* Stock RAM layout: display-state variables placed at the SAME absolute addresses
 * as the stock firmware. The linker reserves 0x1FFF8150..0x1FFF8700 in a NOLOAD
 * .stock_ram region (general .data/.bss live above it), so device RAM is
 * byte-comparable with stock. Offsets decoded from stock field accesses:
 *
 *   UI-state struct @0x1FFF8294 (stock byte_1FFF8294):
 *     +0x00 page (g_menu_page) | +0x04 clock_mode | +0x06 page_anim
 *   Weather/loc struct @0x1FFF8150 (stock byte_1FFF8150):
 *     +0x09 status_mode | +0x0A temp | +0x0B name | +0x3D icon
 *
 * Relocated incrementally; only g_menu_page is active so far. The region is NOLOAD
 * (not cleared by the C runtime) so stock_ram_init() zeroes it before first use. */
#define g_menu_page   (*(volatile uint8_t *)0x1FFF8294u)  /* UI struct +0x00 current page */
#define g_clock_mode  (*(volatile uint8_t *)0x1FFF8298u)  /* UI struct +0x04 bit0=2nd clock face */
#define g_page_anim   (*(volatile uint8_t *)0x1FFF829Au)  /* UI struct +0x06 page transition flag */
#define g_status_mode (*(volatile uint8_t *)0x1FFF8159u)  /* weather struct +0x09 status-bar mode */
#define g_loc_temp    (*(volatile int8_t  *)0x1FFF815Au)  /* weather struct +0x0A outdoor temp (degC) */
#define g_loc_name    ((char *)             0x1FFF815Bu)  /* weather struct +0x0B location name (NUL) */
#define g_loc_icon    (*(volatile uint8_t *)0x1FFF818Du)  /* weather struct +0x3D weather icon (0xFF=none) */
#define G_LOC_NAME_SZ 0x32u                                /* 0x815B..0x818D capacity */

void stock_ram_init(void);   /* zero the reserved stock-RAM region + set icon=0xFF at boot */
#endif
