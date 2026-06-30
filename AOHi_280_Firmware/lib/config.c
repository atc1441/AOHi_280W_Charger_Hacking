/* config.c - settings/NV storage (clean-room from stock cfg_read/cfg_write). */
#include "config.h"
#include "flash.h"

/* Sum the field sizes to get the blob length. */
static uint32_t cfg_blob_len(void)
{
    uint32_t n = 0;
    for (uint8_t i = 0; i < g_cfg_field_count; i++)
        n += g_cfg_fields[i].size;
    return n;
}

void cfg_read(uint8_t key, void *out)
{
    if (key >= g_cfg_field_count) return;
    const cfg_field_t *f = &g_cfg_fields[key];
    flash_read(CONFIG_FLASH_ADDR + f->offset, out, f->size);
}

void cfg_write(uint8_t key, const void *in)
{
    if (key >= g_cfg_field_count) return;
    const cfg_field_t *f = &g_cfg_fields[key];

    /* read-modify-erase-write the whole blob (stock keeps it in one sector) */
    static uint8_t blob[256];
    uint32_t len = cfg_blob_len();
    if (len > sizeof blob) len = sizeof blob;

    flash_read(CONFIG_FLASH_ADDR, blob, len);
    const uint8_t *src = (const uint8_t *)in;
    for (uint16_t i = 0; i < f->size && (f->offset + i) < len; i++)
        blob[f->offset + i] = src[i];

    flash_erase_sector(CONFIG_FLASH_ADDR);
    flash_program(CONFIG_FLASH_ADDR, blob, len);
}
