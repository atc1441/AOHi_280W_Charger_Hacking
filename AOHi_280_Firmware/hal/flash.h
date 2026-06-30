/* flash.h - internal flash program/erase (clean-room from stock bl_flash_*).
 * Sector size 4 KiB (addr>>12); programming is chunked to 8 KiB pages. */
#ifndef HAL_FLASH_H
#define HAL_FLASH_H
#include <stdint.h>
#include <stddef.h>

#define FLASH_SECTOR_SIZE  0x1000u   /* 4 KiB  */
#define FLASH_PAGE_SIZE    0x2000u   /* 8 KiB  */

uint32_t flash_addr_to_sector(uint32_t addr);   /* addr >> 12 */
int  flash_erase_sector(uint32_t addr);
int  flash_program(uint32_t addr, const void *src, uint32_t len);
void flash_read(uint32_t addr, void *dst, uint32_t len);

#endif /* HAL_FLASH_H */
