/* display.h - GC9x-class LCD over SPI3, rewritten on the HC32F460 DDL. */
#ifndef HAL_HC32_DISPLAY_H
#define HAL_HC32_DISPLAY_H
#include <stdint.h>

#define LCD_WIDTH       200
#define LCD_HEIGHT      480
#define LCD_COL_OFFSET  20
#define LCD_ROW_OFFSET  0

void lcd_init(void);
void lcd_write_cmd(uint8_t cmd, const uint8_t *data, uint32_t n);
void lcd_set_window(int x, int y, int w, int h);
void lcd_start_ramwr(void);              /* CS low + RAMWR(0x2C) + DC high, held */
void lcd_end_ramwr(void);                /* CS high */
void lcd_send_pixels(const uint16_t *px, uint32_t count);  /* stream RGB565 (big-endian) */
void lcd_fill(uint16_t rgb565);
void lcd_fill_rect(int x, int y, int w, int h, uint16_t rgb565);
void lcd_display_on(void);   /* ST7701 DISPON - deferred until after the boot black-fill (item 5) */
void lcd_backlight(int on);
void lcd_push_bytes(const void *buf, uint32_t len);   /* raw bytes into an open RAMWR window */
void lcd_sleep(int on);                                /* 1 = sleep (display off), 0 = wake */
void lcd_dma_setup(void);                              /* no-op: pixels pushed by CPU, not DMA */

#endif
