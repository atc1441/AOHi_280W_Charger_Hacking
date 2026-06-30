/* ota.h - WLAN firmware/asset update + the boot-flag config the bootloader reads.
 *
 * 1:1 with stock wlan_cmd_fw_* (0x1A/0x1B/0x1C) and wlan_cmd_image_* (0x20/0x21/0x22):
 *   - firmware OTA streams a bundle [21B header][firmware][asset] in over UART,
 *     stages firmware into INTERNAL flash 0x38000 (what the bootloader copies to
 *     0x8000) and the optional asset blob into EXTERNAL flash 0x800000, CRC32-checks
 *     each, sets the boot flags, and reboots into the bootloader on commit.
 *   - asset image upload writes the four standalone slots in external flash
 *     (0x1000000/0x1300000/0x1600000/0x1900000) then apply_images().
 */
#ifndef APP_OTA_H
#define APP_OTA_H
#include <stdint.h>

/* Boot-flag config sector @0x6000 (stock app_get/set_boot_flag, table @0x1E95C):
 *   slot 0 (byte 0)  = firmware update pending   <- bootloader bl_read_boot_flag(0)
 *   slot 1 (byte 1)  = asset    update pending   <- bootloader bl_read_boot_flag(1)
 *   slot 2 (bytes 2..5) = staged firmware CRC32 (change-detect metadata)
 *   slot 3 (bytes 6..9) = staged asset    CRC32 */
void app_get_boot_flag(void *out, uint8_t slot);
void app_set_boot_flag(const void *val, uint8_t slot);

/* WLAN command handlers (seq/payload/len as delivered by wlan_dispatch_cmd). */
void wlan_cmd_fw_prepare (uint8_t seq, const uint8_t *p, uint16_t n);  /* 0x1A */
void wlan_cmd_fw_data    (uint8_t seq, const uint8_t *p, uint16_t n);  /* 0x1B */
void wlan_cmd_fw_commit  (uint8_t seq, const uint8_t *p, uint16_t n);  /* 0x1C */
void wlan_cmd_image_begin(uint8_t seq, const uint8_t *p, uint16_t n);  /* 0x20 */
void wlan_cmd_image_write(uint8_t seq, const uint8_t *p, uint16_t n);  /* 0x21 */
void wlan_cmd_image_end  (uint8_t seq, const uint8_t *p, uint16_t n);  /* 0x22 */
void wlan_cmd_0D         (uint8_t seq, const uint8_t *p, uint16_t n);  /* 0x0D */

#endif /* APP_OTA_H */
