/* bootloader.c - stage-1 bootloader at flash 0x0, 1:1 with the stock loader.
 *
 *   BL_Main            stock 0x1404
 *   RunFW              stock 0x13b0
 *   bl_flash_firmware  stock 0x1494
 *   EFM ops            stock 0xb14 (erase_sector) / 0x6e8 (write_raw) /
 *                      0x950 (erase_app) / 0x9cc (program)
 *
 * The flash controller (EFM) is driven register-for-register as the stock loader
 * does, so this image is self-contained (no DDL). The staged image is read from
 * the internal 192 KiB staging slot at 0x38000 into a RAM page, then programmed
 * into the app slot at 0x8000 - the EFM cannot read flash mid-program, hence the
 * RAM bounce buffer (stock uses 0x1FFF8410). */
#include "bootloader.h"

/* ------------------------------------------------------------------ EFM regs */
#define EFM_FAPRT   (*(volatile uint32_t *)0x40010400u)  /* unlock: 0x0123 then 0x3210; lock: 0 */
#define EFM_FRMC    (*(volatile uint32_t *)0x40010408u)  /* read-mode (cache bits 0x01010000)   */
#define EFM_FWMC    (*(volatile uint32_t *)0x4001040Cu)  /* write-mode (PE bits 0x70, bit0 = PE)*/
#define EFM_FSR     (*(volatile uint32_t *)0x40010410u)  /* status (RDY=0x100, op-end=0x10)     */
#define EFM_FSCLR   (*(volatile uint32_t *)0x40010414u)  /* status clear                        */

#define EFM_RDY     0x100u
#define EFM_OPEND   0x10u
#define EFM_MODE_MSK 0x70u
#define EFM_MODE_ERASE   0x40u
#define EFM_MODE_PROGRAM 0x10u
#define EFM_CACHE_MSK 0x01010000u

#define SECTOR_SIZE 0x2000u                              /* 8 KiB (stock erase stride)          */
#define OPEND_TMO   0x02000000u                          /* fixed RDY poll budget (clock-agnostic) */

/* flash_op_wait (stock 0xbbc): spin until RDY, then ack the op-end flag. */
static int efm_wait(void)
{
    uint32_t g = OPEND_TMO;
    while ((EFM_FSR & EFM_RDY) != EFM_RDY)
        if (!g--) return -8;                             /* stock flash_poll_status timeout */
    if ((EFM_FSR & EFM_OPEND) == EFM_OPEND)
        EFM_FSCLR = EFM_OPEND;
    return 0;
}

/* bl_flash_erase_sector (stock 0xb14): erase one 8 KiB sector. */
static int efm_erase_sector(uint32_t addr)
{
    EFM_FSCLR = 0x13Fu;                                  /* clear all status (flashctl_arm 319) */
    uint32_t cache = EFM_FRMC & EFM_CACHE_MSK;
    EFM_FRMC &= ~EFM_CACHE_MSK;                           /* disable I/D cache during the op     */
    EFM_FWMC = (EFM_FWMC & ~EFM_MODE_MSK) | EFM_MODE_ERASE;
    *(volatile uint32_t *)addr = 0;                       /* trigger sector erase                */
    int r = efm_wait();
    EFM_FWMC &= ~EFM_MODE_MSK;                            /* back to read mode                   */
    EFM_FRMC = (EFM_FRMC & ~EFM_CACHE_MSK) | cache;       /* restore cache                       */
    return r;
}

/* bl_flash_write_raw (stock 0x6e8): program a byte range, word at a time. The
 * tail (1-3 bytes) is padded with 0xFF so a full word is always programmed. */
static int efm_write_raw(uint32_t dst, const uint8_t *src, uint32_t n)
{
    uint32_t words = n >> 2, tail = n & 3, r = 0;
    EFM_FSCLR = 0x13Fu;
    uint32_t cache = EFM_FRMC & EFM_CACHE_MSK;
    EFM_FRMC &= ~EFM_CACHE_MSK;
    EFM_FWMC = (EFM_FWMC & ~EFM_MODE_MSK) | EFM_MODE_PROGRAM;
    const uint32_t *s = (const uint32_t *)src;
    volatile uint32_t *d = (volatile uint32_t *)dst;
    /* The destination was just erased to all-1s, so skip words that are already
     * 0xFFFFFFFF: leaves them correct, is far faster on sparse/short images, and
     * avoids programming an all-1s word (which this EFM rejects, unlike a real
     * data word). Without this, copying a <192 KiB image's erased tail errors out. */
    while (words-- && r == 0) {
        if (*s != 0xFFFFFFFFu) { *d = *s; r = efm_wait(); }
        d++; s++;
    }
    if (tail && !r) {
        uint32_t w = *s | (0xFFFFFFFFu << (8 * tail));    /* pad tail with 0xFF (stock) */
        if (w != 0xFFFFFFFFu) { *d = w; r = efm_wait(); }
    }
    EFM_FWMC &= ~EFM_MODE_MSK;
    EFM_FRMC = (EFM_FRMC & ~EFM_CACHE_MSK) | cache;
    return r;
}

static void efm_unlock(void) { EFM_FAPRT = 0x0123u; EFM_FAPRT = 0x3210u; }
static void efm_lock(void)   { EFM_FAPRT = 0; }          /* stock flashctl_850 */
static void efm_pe_enable(uint8_t on)                    /* stock util_6B0: FWMC bit0 */
{
    if (on) EFM_FWMC |= 1u; else EFM_FWMC &= ~1u;
}

/* bl_flash_erase_app (stock 0x950): erase `count` 8 KiB sectors from `base`. */
static int efm_erase_app(uint32_t base, uint32_t count)
{
    efm_unlock();
    while ((EFM_FSR & EFM_RDY) != EFM_RDY) { }
    efm_pe_enable(1);
    for (uint32_t i = 0; i < count; i++) {
        int r = efm_erase_sector(base + i * SECTOR_SIZE);
        if (r) { efm_pe_enable(0); efm_lock(); return r; }
    }
    efm_pe_enable(0);
    efm_lock();
    return 0;
}

/* bl_flash_program (stock 0x9cc): program `n` bytes, never crossing an 8 KiB page. */
static int efm_program(uint32_t dst, const uint8_t *src, uint32_t n)
{
    int r = 0;
    efm_unlock();
    while ((EFM_FSR & EFM_RDY) != EFM_RDY) { }
    efm_pe_enable(1);
    uint32_t off = 0;
    while (off < n) {
        uint32_t room = SECTOR_SIZE - (dst & (SECTOR_SIZE - 1));   /* bytes left in this page */
        uint32_t chunk = (n - off >= room) ? room : (n - off);
        if (chunk > SECTOR_SIZE) chunk = SECTOR_SIZE;
        r = efm_write_raw(dst, src + off, chunk);
        if (r) break;
        off += chunk;
        dst += chunk;
    }
    efm_pe_enable(0);
    efm_lock();
    return r;
}

/* ------------------------------------------------------------------ boot flags */
uint8_t bl_read_boot_flag(uint8_t slot)
{
    return *(volatile uint8_t *)(FLAG_SECTOR + (slot & 1));    /* flash is memory-mapped */
}

void bl_write_boot_flag(uint8_t value, uint8_t slot)
{
    uint8_t shadow[2];
    shadow[0] = *(volatile uint8_t *)(FLAG_SECTOR + 0);
    shadow[1] = *(volatile uint8_t *)(FLAG_SECTOR + 1);
    shadow[slot & 1] = value;
    efm_erase_app(FLAG_SECTOR, 1);
    efm_program(FLAG_SECTOR, shadow, sizeof shadow);
}

/* ------------------------------------------------------------------ OTA copy */
static uint8_t s_page[SECTOR_SIZE];                       /* RAM bounce buffer (stock 0x1FFF8410) */

int bl_flash_firmware(void)
{
    int r = efm_erase_app(APP_BASE, APP_OTA_SIZE / SECTOR_SIZE);   /* 24 sectors */
    if (r) return r;
    for (uint32_t off = 0; off < APP_OTA_SIZE; off += SECTOR_SIZE) {
        const uint8_t *s = (const uint8_t *)(STAGING_BASE + off);
        for (uint32_t i = 0; i < SECTOR_SIZE; i++) s_page[i] = s[i];   /* flash -> RAM */
        r = efm_program(APP_BASE + off, s_page, SECTOR_SIZE);
        if (r) return r;
    }
    return 0;
}

/* ------------------------------------------------------------------ jump to app */
int run_fw(const uint32_t *vt)
{
    uint32_t sp = vt[0];
    if (sp < 0x1FFF8001u)                                 /* stock RunFW sanity check */
        return -1;
    void (*reset)(void) = (void (*)(void))vt[1];
    __asm volatile ("msr msp, %0" :: "r"(sp) : );
    __asm volatile ("isb");
    reset();
    return 0;                                             /* unreachable */
}

void bl_main(void)
{
    uint8_t update_pending = bl_read_boot_flag(0);
    uint8_t staging_valid  = bl_read_boot_flag(1);

    for (;;) {
        if (update_pending == 1) {
            if (bl_flash_firmware() != 0) {
                bl_write_boot_flag(1, 0);                 /* leave pending; retry next boot */
            } else {
                if (staging_valid == 1)
                    bl_write_boot_flag(0, 1);
                bl_write_boot_flag(0, 0);                 /* update applied */
                update_pending = 0;
            }
        }
        run_fw((const uint32_t *)APP_BASE);               /* returns only if app vector invalid */
    }
}
