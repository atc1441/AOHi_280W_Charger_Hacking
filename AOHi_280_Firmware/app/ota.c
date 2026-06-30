/* ota.c - WLAN firmware/asset OTA, 1:1 with stock wlan_cmd_fw_ / wlan_cmd_image_.
 *
 * Firmware bundle wire format (streamed over cmd 0x1B, each chunk = [LE32 stream
 * offset][data]); the 21-byte header lives at stream offset 0, big-endian fields:
 *   off  0  u32  total bundle size   (sanity vs the prepare announce)
 *   off  4  u32  firmware size       -> internal staging 0x38000 (bootloader src)
 *   off  8  u32  firmware CRC32
 *   off 12  u8   has-asset flag
 *   off 13  u32  asset size          -> external staging 0x800000
 *   off 17  u32  asset CRC32
 * Stream layout: [21B header][firmware][asset]. CRC32 is the HC32 hardware CRC
 * configured exactly as stock crc_134B4 (CRC32, refin/refout, init 0xFFFFFFFF). */
#include "ota.h"
#include "flash.h"
#include "extflash.h"
#include "config.h"
#include "ui.h"
#include "wdt.h"
#include "tick.h"
#include "display.h"          /* lcd_fill_rect, LCD_WIDTH/HEIGHT */
#include "uart.h"             /* uart_send_buf */
#include <string.h>

/* ---------------- geometry ---------------- */
#define BOOTCFG_ADDR   0x00006000u
#define FW_STAGE_INT   0x00038000u
#define FW_OTA_MAX     0x00030000u        /* 192 KiB updatable app region */
#define ASSET_STAGE_EXT 0x00800000u
#define OTA_HDR_LEN    21u

/* ---------------- HC32 hardware CRC32 (stock crc_134B4 / fn_9794 / crc_966C) ---- */
#define CRC_CR    (*(volatile uint32_t *)0x40008C00u)
#define CRC_RSLT  (*(volatile uint32_t *)0x40008C04u)   /* write init, read CRC32 result */
#define CRC_DAT8  (*(volatile uint8_t  *)0x40008C80u)
#define CRC_INIT  CRC_RSLT

/* Enable the CRC peripheral clock: FCG0 bit 23 (0 = running), behind the FCG0
 * write-protect (PWC FCG0PC @0x40048010, key 0xA5A5). */
static void crc_clk_on(void)
{
    *(volatile uint32_t *)0x40048010u = 0xA5A50001u;   /* unlock FCG0 writes */
    *(volatile uint32_t *)0x40048000u &= ~0x00800000u; /* CRC clock on */
    *(volatile uint32_t *)0x40048010u = 0xA5A50000u;   /* re-lock */
}

static void crc_begin(void)
{
    crc_clk_on();
    CRC_CR   = 0x1Eu;            /* CRC32 + reflect-in/out + final-xor (stock CR) */
    CRC_INIT = 0xFFFFFFFFu;
}
static void crc_feed(const uint8_t *p, uint32_t n) { while (n--) CRC_DAT8 = *p++; }
static uint32_t crc_result(void) { return CRC_RSLT; }

/* ---------------- boot-flag config sector @0x6000 ---------------- */
/* slot byte-offset + width within the config blob (stock table @0x1E95C). */
static void slot_loc(uint8_t slot, uint32_t *off, uint32_t *sz)
{
    switch (slot) {
    case 0: *off = 0; *sz = 1; break;          /* fw pending  (bootloader reads) */
    case 1: *off = 1; *sz = 1; break;          /* asset pending */
    case 2: *off = 2; *sz = 4; break;          /* fw crc32 */
    default:*off = 6; *sz = 4; break;          /* asset crc32 */
    }
}

void app_get_boot_flag(void *out, uint8_t slot)
{
    uint32_t off, sz; slot_loc(slot, &off, &sz);
    flash_read(BOOTCFG_ADDR + off, out, sz);
}

void app_set_boot_flag(const void *val, uint8_t slot)
{
    uint8_t blob[16];
    uint32_t off, sz; slot_loc(slot, &off, &sz);
    flash_read(BOOTCFG_ADDR, blob, sizeof blob);
    memcpy(blob + off, val, sz);
    flash_erase_sector(BOOTCFG_ADDR);
    flash_program(BOOTCFG_ADDR, blob, sizeof blob);
}

/* ---------------- software reset (stock system_reboot, SCB AIRCR) ---------------- */
static void system_reboot(void)
{
    volatile uint32_t *aircr = (volatile uint32_t *)0xE000ED0Cu;
    __asm volatile ("dsb");
    *aircr = (*aircr & 0x700u) | 0x05FA0004u;            /* VECTKEY | SYSRESETREQ */
    __asm volatile ("dsb");
    for (;;) { }
}

/* ---------------- firmware OTA state machine ---------------- */
static struct {
    uint8_t  active;        /* a prepare has armed the transfer */
    uint8_t  error;         /* sticky transfer error (stock byte_2000F3E2) */
    uint8_t  has_asset;
    uint32_t announce;      /* total size from prepare (cmd 0x1A) */
    uint32_t stream_pos;    /* bytes consumed from the bundle stream so far */
    uint8_t  hdr[OTA_HDR_LEN];
    uint32_t fw_size, fw_crc;
    uint32_t asset_size, asset_crc;
    uint32_t fw_pos;        /* firmware bytes staged into 0x38000 */
    uint32_t asset_pos;     /* asset bytes staged into ext 0x800000 */
    uint8_t  page[FLASH_PAGE_SIZE];
    uint32_t page_fill;     /* bytes pending in `page` (firmware path) */
} s_fw;

static uint32_t be32(const uint8_t *p)
{ return ((uint32_t)p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; }
static uint32_t le32(const uint8_t *p)
{ return (uint32_t)p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

/* 9-byte status reply [55 AA 00 seq cmd status 00 00 cksum8] (stock layout). */
static void ota_reply(uint8_t cmd, uint8_t seq, uint8_t status)
{
    uint8_t b[9] = { 0x55, 0xAA, 0x00, seq, cmd, status, 0x00, 0x00, 0x00 };
    uint8_t c = 0;
    for (int i = 0; i < 8; i++) c += b[i];
    b[8] = c;
    uart_send_buf(b, 9);
}

static void fw_reset_state(void)
{
    memset(&s_fw, 0, sizeof s_fw);
}

/* flush whatever is buffered in s_fw.page into internal staging at fw_pos. */
static int fw_flush_page(void)
{
    if (!s_fw.page_fill) return 0;
    if (s_fw.fw_pos + s_fw.page_fill > FW_OTA_MAX) return -1;
    int r = flash_program(FW_STAGE_INT + s_fw.fw_pos, s_fw.page, s_fw.page_fill);
    if (r) return r;
    s_fw.fw_pos  += s_fw.page_fill;
    s_fw.page_fill = 0;
    return 0;
}

/* append firmware bytes: CRC-feed, page-buffer, program full 8 KiB pages. */
static int fw_take_firmware(const uint8_t *d, uint32_t n)
{
    crc_feed(d, n);
    while (n) {
        uint32_t room = FLASH_PAGE_SIZE - s_fw.page_fill;
        uint32_t k = (n < room) ? n : room;
        memcpy(s_fw.page + s_fw.page_fill, d, k);
        s_fw.page_fill += k; d += k; n -= k;
        if (s_fw.page_fill == FLASH_PAGE_SIZE && fw_flush_page()) return -1;
    }
    return 0;
}

/* append asset bytes: CRC-feed (separate accumulator) + external staging write. */
static int fw_take_asset(const uint8_t *d, uint32_t n)
{
    if (s_fw.asset_pos == 0) crc_begin();           /* restart CRC for the asset blob */
    crc_feed(d, n);
    extflash_write(ASSET_STAGE_EXT + s_fw.asset_pos, d, n);
    s_fw.asset_pos += n;
    return 0;
}

/* cmd 0x1A: announce size, erase the 192 KiB internal staging, show the update
 * page, ACK (status = 1 when the announced size is zero, i.e. nothing to do). */
void wlan_cmd_fw_prepare(uint8_t seq, const uint8_t *p, uint16_t n)
{
    fw_reset_state();
    s_fw.announce = (n >= 4) ? le32(p) : 0;
    s_fw.active   = (s_fw.announce != 0);
    crc_begin();
    for (uint32_t off = 0; off < FW_OTA_MAX; off += FLASH_PAGE_SIZE) {
        flash_erase_sector(FW_STAGE_INT + off);
        wdt_feed();
    }
    ui_set_page(15);
    ota_reply(0x1A, seq, (uint8_t)(s_fw.announce == 0));
}

/* cmd 0x1B: one bundle chunk = [LE32 stream offset][data]. */
void wlan_cmd_fw_data(uint8_t seq, const uint8_t *p, uint16_t n)
{
    if (s_fw.error || !s_fw.active || n < 4) { ota_reply(0x1B, seq, s_fw.error); return; }

    uint32_t offset = le32(p);
    const uint8_t *d = p + 4;
    uint32_t len = (uint32_t)n - 4;

    if (offset != s_fw.stream_pos) { s_fw.error = 1; goto done; }  /* out of order */

    while (len) {
        uint32_t pos = s_fw.stream_pos;
        if (pos < OTA_HDR_LEN) {                       /* ---- 21-byte header ---- */
            uint32_t k = OTA_HDR_LEN - pos; if (k > len) k = len;
            memcpy(s_fw.hdr + pos, d, k);
            d += k; len -= k; s_fw.stream_pos += k;
            if (s_fw.stream_pos == OTA_HDR_LEN) {       /* header complete: parse it */
                s_fw.fw_size    = be32(&s_fw.hdr[4]);
                s_fw.fw_crc     = be32(&s_fw.hdr[8]);
                s_fw.has_asset  = s_fw.hdr[12];
                s_fw.asset_size = be32(&s_fw.hdr[13]);
                s_fw.asset_crc  = be32(&s_fw.hdr[17]);
                if (s_fw.fw_size > FW_OTA_MAX) { s_fw.error = 1; goto done; }
            }
        } else {
            uint32_t fw_end = OTA_HDR_LEN + s_fw.fw_size;
            if (pos < fw_end) {                         /* ---- firmware region ---- */
                uint32_t k = fw_end - pos; if (k > len) k = len;
                if (fw_take_firmware(d, k)) { s_fw.error = 1; goto done; }
                d += k; len -= k; s_fw.stream_pos += k;
                if (s_fw.stream_pos == fw_end) {        /* firmware complete: finish */
                    if (fw_flush_page()) { s_fw.error = 1; goto done; }
                    if (crc_result() != s_fw.fw_crc) { s_fw.error = 1; goto done; }
                    uint8_t one = 1;
                    app_set_boot_flag(&s_fw.fw_crc, 2); /* metadata */
                    app_set_boot_flag(&one, 0);         /* flag0: fw update pending */
                }
            } else {                                    /* ---- asset region ---- */
                uint32_t k = len;
                if (s_fw.has_asset) { if (fw_take_asset(d, k)) { s_fw.error = 1; goto done; } }
                d += k; len -= k; s_fw.stream_pos += k;
                uint32_t bundle_end = fw_end + (s_fw.has_asset ? s_fw.asset_size : 0);
                if (s_fw.has_asset && s_fw.stream_pos >= bundle_end) {
                    if (crc_result() != s_fw.asset_crc) { s_fw.error = 1; goto done; }
                    uint8_t one = 1;
                    app_set_boot_flag(&s_fw.asset_crc, 3);
                    app_set_boot_flag(&one, 1);         /* flag1: asset update pending */
                }
            }
        }
    }
done:
    if (s_fw.error) { ui_set_page(0); }
    ota_reply(0x1B, seq, s_fw.error);
}

/* cmd 0x1C: commit. If a firmware update is staged (flag0==1) reboot into the
 * bootloader, which copies 0x38000 -> 0x8000 on the next boot. */
void wlan_cmd_fw_commit(uint8_t seq, const uint8_t *p, uint16_t n)
{
    (void)p; (void)n;
    uint8_t flag0 = 0; app_get_boot_flag(&flag0, 0);
    ota_reply(0x1C, seq, (uint8_t)(flag0 == 0));        /* status 0 => will reboot */
    if (flag0 == 1)
        system_reboot();
    s_fw.active = 0;
    ui_set_page(0);
}

/* ---------------- asset image upload (standalone slots in external flash) -------- */
static uint8_t  s_img_slot;
static uint32_t s_img_total;

static uint32_t img_slot_base(uint8_t slot)
{
    switch (slot) {
    case 1: return EXTFLASH_IMG_SLOT1;
    case 2: return EXTFLASH_IMG_SLOT2;
    case 3: return EXTFLASH_IMG_SLOT3;
    case 4: return EXTFLASH_IMG_SLOT4;
    default:return 0;
    }
}

/* cmd 0x20: slot=p[0], total=BE32(p[1..4]), 32-byte name follows. */
void wlan_cmd_image_begin(uint8_t seq, const uint8_t *p, uint16_t n)
{
    s_img_slot  = (n >= 1) ? p[0] : 0;
    s_img_total = (n >= 5) ? be32(p + 1) : 0;
    ui_set_page(15);
    ota_reply(0x20, seq, 0);
}

/* cmd 0x21: chunk = [LE32 slot offset][data] -> external flash slot base. */
void wlan_cmd_image_write(uint8_t seq, const uint8_t *p, uint16_t n)
{
    if (n < 4 || !s_img_slot) { ota_reply(0x21, seq, 0); return; }
    uint32_t off = le32(p);
    const uint8_t *d = p + 4;
    uint32_t len = (uint32_t)n - 4;
    uint32_t base = img_slot_base(s_img_slot);
    if (off == 0) extflash_reset_erasecache();
    if (base) extflash_write(base + off, d, len);
    if (off + len == s_img_total) {                     /* slot complete -> mark ready */
        uint8_t one = 1;
        cfg_write((uint8_t)(2 + s_img_slot), &one);     /* keys 3..6 for slots 1..4 */
    }
    ota_reply(0x21, seq, 0);
}

/* cmd 0x22: finalize. If any slot was written, apply_images(); reply cmd 0x0C. */
void wlan_cmd_image_end(uint8_t seq, const uint8_t *p, uint16_t n)
{
    (void)p; (void)n;
    uint8_t ready = 0, v;
    for (uint8_t k = 3; k <= 6; k++) { v = 0; cfg_read(k, &v); if (v == 1) ready = 1; }
    uint8_t status = 1;
    if (ready) { apply_images(); status = 0; }
    ota_reply(0x0C, seq, status);
    ui_set_page(0);
}

/* ---------------- cmd 0x0D: pair/identify flash (red if p[0]<60 else green) ----- */
static void delay_ms_feed(uint32_t ms)
{
    uint32_t t0 = get_tick_ms();
    while (get_tick_ms() - t0 < ms) wdt_feed();
}

void wlan_cmd_0D(uint8_t seq, const uint8_t *p, uint16_t n)
{
    (void)seq;
    uint8_t v = (n >= 1) ? p[0] : 0;
    ui_set_page(20);
    lcd_fill_rect(0, 0, LCD_WIDTH, LCD_HEIGHT, (v < 60) ? 0xF800 : 0x07E0);  /* red / green */
    for (int i = 0; i < 4; i++) { delay_ms_feed(600); }
    ui_set_page(0);
}
