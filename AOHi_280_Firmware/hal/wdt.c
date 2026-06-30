/* wdt.c - feed the SWDT via the DDL (no raw registers).
 *
 * In secondary boot mode the bootloader's ICG auto-starts the SWDT
 * (M4_SWDT @ 0x40049400; stock fed its refresh reg 0x40049408). The app must
 * keep refreshing it or the chip resets mid-init -> blank panel. */
#include "wdt.h"
#include <hc32_ddl.h>
#include <hc32f460_swdt.h>

void wdt_feed(void)
{
    SWDT_RefreshCounter();
}
