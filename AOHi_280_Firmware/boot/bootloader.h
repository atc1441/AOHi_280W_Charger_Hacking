/* bootloader.h - stage-1 bootloader at flash 0x0 (1:1 with stock BL_Main @0x1404).
 *
 * Internal-flash layout (HC32F460xE, 512 KiB, all addresses memory-mapped from 0x0):
 *   0x00000  bootloader            (this image, < 32 KiB)
 *   0x06000  boot-flag config      (1 sector: flag0=update-pending @0x6000, flag1=staging-valid @0x6001)
 *   0x08000  application           (192 KiB OTA region)
 *   0x38000  staged firmware       (192 KiB - app downloads the new image here, then sets flag0)
 *   0x68000  config / NV
 *
 * On boot (BL_Main): if flag0==1, copy the 192 KiB staged image 0x38000 -> 0x08000, clear the
 * flags (and if flag1==1, erase the external asset-staging area), then jump to the app vector
 * table at 0x08000 (RunFW). All geometry verified from the stock bootloader disassembly. */
#ifndef BOOT_BOOTLOADER_H
#define BOOT_BOOTLOADER_H
#include <stdint.h>

#define APP_BASE        0x00008000u     /* application vector table / entry            */
#define APP_OTA_SIZE    0x00030000u     /* 192 KiB updatable app region (stock: 0x18 sectors) */
#define STAGING_BASE    0x00038000u     /* staged image source (stock bl_flash_firmware var_10) */
#define FLAG0_ADDR      0x00006000u     /* update-pending flag (stock dword_2AB4[0])    */
#define FLAG1_ADDR      0x00006001u     /* staging-valid  flag (stock dword_2AB4[2])    */
#define FLAG_SECTOR     0x00006000u     /* config sector holding both flags             */

/* Set MSP from vt[0] and branch to the reset vector vt[1] (stock RunFW @0x13b0). */
int  run_fw(const uint32_t *vt);
/* Bootloader entry (stock BL_Main @0x1404). */
void bl_main(void);
/* Boot-flag accessors (slot 0 = update-pending, 1 = staging-valid). */
uint8_t bl_read_boot_flag(uint8_t slot);
void    bl_write_boot_flag(uint8_t value, uint8_t slot);
/* Copy the staged image into the application slot (stock bl_flash_firmware @0x1494). 0 = ok. */
int  bl_flash_firmware(void);

#endif /* BOOT_BOOTLOADER_H */
