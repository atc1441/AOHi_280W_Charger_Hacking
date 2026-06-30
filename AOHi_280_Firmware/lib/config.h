/* config.h - settings/NV storage in internal flash (from stock cfg_read/write).
 * A single config blob at 0x68000 holds fixed-offset fields described by a
 * key table; writes are read-modify-erase-write of the blob. */
#ifndef LIB_CONFIG_H
#define LIB_CONFIG_H
#include <stdint.h>

#define CONFIG_FLASH_ADDR 0x00068000u

/* Each key occupies [offset, offset+size) within the blob. */
typedef struct { uint16_t offset; uint16_t size; } cfg_field_t;

void cfg_read(uint8_t key, void *out);
void cfg_write(uint8_t key, const void *in);

/* Board supplies the field table + count. */
extern const cfg_field_t g_cfg_fields[];
extern const uint8_t      g_cfg_field_count;

#endif /* LIB_CONFIG_H */
