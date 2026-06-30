/* flash.c - internal flash program/erase on the HC32F460 DDL EFM driver.
 *
 * Internal flash is execute-in-place (memory-mapped at 0x0), so reads are plain
 * loads. Erase/program go through the DDL EFM driver (unlock -> set mode ->
 * op -> read-only -> lock); no raw register access. */
#include "flash.h"
#include <hc32_ddl.h>
#include <hc32f460_efm.h>

uint32_t flash_addr_to_sector(uint32_t addr) { return addr >> 12; }

void flash_read(uint32_t addr, void *dst, uint32_t len)
{
    const uint8_t *s = (const uint8_t *)(uintptr_t)addr;
    uint8_t *d = (uint8_t *)dst;
    while (len--) *d++ = *s++;
}

int flash_erase_sector(uint32_t addr)
{
    EFM_Unlock();
    EFM_SetErasePgmMode(EFM_MODE_SECTORERASE);
    en_result_t r = EFM_SectorErase(addr & ~(FLASH_SECTOR_SIZE - 1));
    EFM_SetErasePgmMode(EFM_MODE_READONLY);
    EFM_Lock();
    return (r == Ok) ? 0 : -1;
}

int flash_program(uint32_t addr, const void *src, uint32_t len)
{
    const uint8_t *p = (const uint8_t *)src;
    int rc = 0;
    EFM_Unlock();
    EFM_SetErasePgmMode(EFM_MODE_SINGLEPROGRAM);
    uint32_t i = 0;
    for (; i + 4u <= len && !rc; i += 4u) {
        uint32_t w = (uint32_t)p[i] | ((uint32_t)p[i+1] << 8)
                   | ((uint32_t)p[i+2] << 16) | ((uint32_t)p[i+3] << 24);
        if (EFM_SingleProgram(addr + i, w) != Ok) rc = -1;
    }
    if (!rc && i < len) {                      /* tail: pad unwritten bytes with 1s */
        uint32_t w = 0xFFFFFFFFu;
        for (uint32_t b = 0; i + b < len; b++)
            w = (w & ~(0xFFu << (8 * b))) | ((uint32_t)p[i + b] << (8 * b));
        if (EFM_SingleProgram(addr + i, w) != Ok) rc = -1;
    }
    EFM_SetErasePgmMode(EFM_MODE_READONLY);
    EFM_Lock();
    return rc;
}
