/* mcu_efm.c - embedded flash program/erase driver (clean-room).
 *
 * Internal flash is execute-in-place; erase/program run from RAM-safe code with
 * the cache disabled and restored. Mirrors the unlock -> set-mode -> op ->
 * read-only -> lock sequence the OTA writer needs. */
#include "hc32f460_efm.h"

#define EFM_KEY1     (0x0123ul)
#define EFM_KEY2     (0x3210ul)
#define EFM_UNLOCK   (0x00000001u)

static void efm_clear_flags(void)
{
    M4_EFM->FSCLR = EFM_FLAG_WRPERR | EFM_FLAG_PEPRTERR | EFM_FLAG_PGSZERR |
                    EFM_FLAG_PGMISMTCH | EFM_FLAG_EOP | EFM_FLAG_COLERR;
}

static en_result_t efm_wait_ready(void)
{
    uint16_t to = 0u;
    while (1u != M4_EFM->FSR_f.RDY)
        if (++to > 0x1000u) return ErrorTimeout;
    return Ok;
}

void EFM_Unlock(void)
{
    M4_EFM->FAPRT = EFM_KEY1;
    M4_EFM->FAPRT = EFM_KEY2;
}

void EFM_Lock(void)
{
    if (EFM_UNLOCK == M4_EFM->FAPRT)
    {
        M4_EFM->FAPRT = EFM_KEY2;
        M4_EFM->FAPRT = EFM_KEY2;
    }
}

en_result_t EFM_SetErasePgmMode(uint32_t u32Mode)
{
    en_result_t r = efm_wait_ready();
    if (Ok == r)
    {
        M4_EFM->FWMC_f.PEMODE = Enable;       /* allow PEMOD writes */
        M4_EFM->FWMC_f.PEMOD  = u32Mode;
        M4_EFM->FWMC_f.PEMODE = Disable;
    }
    return r;
}

en_result_t EFM_SingleProgram(uint32_t u32Addr, uint32_t u32Data)
{
    en_result_t r = Ok;
    uint8_t cache;

    efm_clear_flags();
    cache = (uint8_t)M4_EFM->FRMC_f.CACHE;
    M4_EFM->FRMC_f.CACHE = Disable;

    M4_EFM->FWMC_f.PEMODE = Enable;
    M4_EFM->FWMC_f.PEMOD  = EFM_MODE_SINGLEPROGRAM;
    *(volatile uint32_t *)u32Addr = u32Data;

    if (efm_wait_ready() != Ok) r = ErrorTimeout;
    if (u32Data != *(volatile uint32_t *)u32Addr) r = Error;

    M4_EFM->FSCLR = EFM_FLAG_EOP;
    M4_EFM->FWMC_f.PEMOD  = EFM_MODE_READONLY;
    M4_EFM->FWMC_f.PEMODE = Disable;
    M4_EFM->FRMC_f.CACHE  = cache;
    return r;
}

en_result_t EFM_SectorErase(uint32_t u32Addr)
{
    en_result_t r = Ok;
    uint8_t cache;

    efm_clear_flags();
    cache = (uint8_t)M4_EFM->FRMC_f.CACHE;
    M4_EFM->FRMC_f.CACHE = Disable;

    M4_EFM->FWMC_f.PEMODE = Enable;
    M4_EFM->FWMC_f.PEMOD  = EFM_MODE_SECTORERASE;
    *(volatile uint32_t *)u32Addr = 0x12345678u;

    if (efm_wait_ready() != Ok) r = ErrorTimeout;

    M4_EFM->FSCLR = EFM_FLAG_EOP;
    M4_EFM->FWMC_f.PEMOD  = EFM_MODE_READONLY;
    M4_EFM->FWMC_f.PEMODE = Disable;
    M4_EFM->FRMC_f.CACHE  = cache;
    return r;
}
