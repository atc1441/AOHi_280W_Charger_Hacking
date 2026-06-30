/* main.c - HC32F460 secondary-boot entry. Sets the vector table to the app's
 * flash start (behind the bootloader), feeds the SWDT once, then hands off to
 * fw_main() - the stock-faithful top-level bring-up and main loop. */
#include <hc32_ddl.h>
#include "wdt.h"
#include "ui_state.h"   /* stock_ram_init() */

extern void fw_main(void);

int main(void)
{
    SCB->VTOR = ((uint32_t)LD_FLASH_START) & SCB_VTOR_TBLOFF_Msk;
    wdt_feed();                       /* SWDT may be near timeout at hand-off */
    stock_ram_init();                 /* zero the NOLOAD .stock_ram reserve (0x1FFF8150..) */
    fw_main();
    for (;;) { }                      /* fw_main() never returns */
}
