/* assets.h - image asset metadata extracted from stock firmware. */
#ifndef BOARD_ASSETS_H
#define BOARD_ASSETS_H
#include <stdint.h>

#define IMG_COUNT 951

typedef struct {
    uint32_t flash_off;   /* byte offset in external image flash */
    uint32_t data_size;   /* byte length of the sprite           */
    uint16_t width;
    uint16_t height;
    uint32_t flags;
} img_entry_t;

extern const img_entry_t g_img_table[IMG_COUNT];
extern const uint32_t    g_img_pos[IMG_COUNT];   /* packed (y<<16)|x */

#endif
