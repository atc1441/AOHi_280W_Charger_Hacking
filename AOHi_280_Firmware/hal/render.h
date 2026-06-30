/* render.h - RLE image blit from external flash (HC32F460 DDL). */
#ifndef HAL_HC32_RENDER_H
#define HAL_HC32_RENDER_H
#include <stdint.h>

void lcd_draw_element(uint32_t flash_off, uint32_t size,
                      uint16_t w, uint16_t h, uint16_t x, uint16_t y);

#endif
