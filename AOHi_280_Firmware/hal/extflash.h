/* extflash.h - external SPI-NOR image flash (32 MiB) on SPI1 (HC32F460 DDL). */
#ifndef HAL_HC32_EXTFLASH_H
#define HAL_HC32_EXTFLASH_H
#include <stdint.h>
#include <stddef.h>

int  extflash_init(void);                                 /* returns 1 on success */
uint16_t extflash_read_id(void);                          /* (mfg<<8)|device via REMS */
void extflash_read(uint32_t addr, void *dst, size_t len); /* fast-read 0x0B */
void extflash_write(uint32_t addr, const void *src, size_t len);
void extflash_erase_sector(uint32_t addr);                /* 4 KiB sector (0x20) */
void extflash_reset_erasecache(void);
int  extflash_embedded_read(uint32_t addr, void *dst, uint32_t len);  /* weak override */

#define EXTFLASH_CMD_WREN     0x06u
#define EXTFLASH_CMD_PP       0x02u   /* page program (256 B page) */
#define EXTFLASH_CMD_SE       0x20u   /* 4 KiB sector erase        */
#define EXTFLASH_CMD_RDSR     0x05u   /* read status (WIP = bit0)  */
#define EXTFLASH_PAGE         256u
#define EXTFLASH_SECTOR       0x1000u

/* Image-upload slot bases in the 32 MiB external flash. */
#define EXTFLASH_IMG_SLOT1    0x1000000u
#define EXTFLASH_IMG_SLOT2    0x1300000u
#define EXTFLASH_IMG_SLOT3    0x1600000u
#define EXTFLASH_IMG_SLOT4    0x1900000u
#define EXTFLASH_FW_STAGE     0x0800000u   /* firmware OTA staging base */

#endif /* HAL_HC32_EXTFLASH_H */
