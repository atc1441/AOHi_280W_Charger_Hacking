/* spi.c - LCD SPI byte stream on the HC32F460 DDL.
 *
 * On the stock chip the LCD hung off "SPI0 @0x40020000", which is M4_SPI3 on the
 * HC32F460. The DDL display HAL (display.c) owns SPI3 - it configures it in
 * lcd_init() and provides lcd_push_bytes() for raw RAMWR streaming. These keep
 * the old spi0_* API for callers (ui_FC80 streams a separator via spi0_write). */
#include "spi.h"
#include "display.h"

void spi0_init(void)      { /* SPI3 configured by lcd_init() */ }
void spi0_arm(void)       { }
void spi0_wait_idle(void) { }

void spi0_write(const void *buf, size_t len)
{
    lcd_push_bytes(buf, (uint32_t)len);
}
