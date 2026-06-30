/* ui_state.c - the stock-RAM-layout reservation lives in a NOLOAD linker section
 * (.stock_ram, 0x1FFF8150..0x1FFF8700), so the C runtime does not zero it. Clear it
 * explicitly before any UI state is touched (called first thing in the boot path). */
#include "ui_state.h"

void stock_ram_init(void)
{
    volatile uint8_t *p = (volatile uint8_t *)0x1FFF8150u;
    for (uint32_t i = 0; i < (0x1FFF8700u - 0x1FFF8150u); i++) p[i] = 0u;
    g_loc_icon = 0xFFu;   /* stock: weather icon defaults to "none" until a code maps */
}
