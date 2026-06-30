/* extflash.c - 32MB external image SPI-NOR on SPI1, rewritten on the HC32F460 DDL.
 *
 * Full-duplex master; fast-read = 0x0B + 4 address bytes + 1 dummy, then read.
 * Replaces the hand-rolled SPI1 register poking with the DDL PORT/SPI/PWC drivers. */
#include "extflash.h"
#include "board.h"
#include <hc32_ddl.h>
#include <hc32f460_gpio.h>
#include <hc32f460_spi.h>
#include <hc32f460_pwc.h>

#define CS_LOW()   PORT_ResetBits(XF_CS_PORT, XF_CS_PIN)
#define CS_HIGH()  PORT_SetBits(XF_CS_PORT, XF_CS_PIN)

static uint8_t xf_xfer(uint8_t out)
{
    /* Bounded waits: an unresponsive ext-flash SPI must never hang the whole CPU
     * (a stall here froze the device in the clock screensaver - no WDT feed, no
     * button response). On timeout return 0 so the caller makes progress. */
    uint32_t g = 200000u;
    while (SPI_GetFlag(XF_SPI, SpiFlagSendBufferEmpty) != Set && --g) { }
    SPI_SendData8(XF_SPI, out);
    g = 200000u;
    while (SPI_GetFlag(XF_SPI, SpiFlagReceiveBufferFull) != Set && --g) { }
    if (!g) return 0u;
    return SPI_ReceiveData8(XF_SPI);
}

int extflash_init(void)
{
    PWC_Fcg1PeriphClockCmd(PWC_FCG1_PERIPH_SPI1, Enable);

    stc_port_init_t o = { .enPinMode = Pin_Mode_Out, .enPinDrv = Pin_Drv_H, .enPinOType = Pin_OType_Cmos };
    PORT_Init(XF_CS_PORT, XF_CS_PIN, &o);
    CS_HIGH();
    PORT_SetFunc(XF_SCK_PORT,  XF_SCK_PIN,  XF_SCK_FUNC,  Disable);
    PORT_SetFunc(XF_MOSI_PORT, XF_MOSI_PIN, XF_MOSI_FUNC, Disable);
    PORT_SetFunc(XF_MISO_PORT, XF_MISO_PIN, XF_MISO_FUNC, Disable);

    stc_spi_init_t spi = {0};
    spi.enClkDiv          = SpiClkDiv4;
    spi.enFrameNumber     = SpiFrameNumber1;
    spi.enDataLength      = SpiDataLengthBit8;
    spi.enFirstBitPosition= SpiFirstBitPositionMSB;
    spi.enSckPolarity     = SpiSckIdleLevelLow;
    spi.enSckPhase        = SpiSckOddSampleEvenChange;
    spi.enReadBufferObject= SpiReadReceiverBuffer;
    spi.enWorkMode        = SpiWorkMode4Line;
    spi.enTransMode       = SpiTransFullDuplex;
    spi.enMasterSlaveMode = SpiModeMaster;
    spi.enCommAutoSuspendEn      = Disable;
    spi.enModeFaultErrorDetectEn = Disable;
    spi.enParitySelfDetectEn     = Disable;
    spi.enParityEn        = Disable;
    spi.enParity          = SpiParityEven;
    SPI_Init(XF_SPI, &spi);
    SPI_Cmd(XF_SPI, Enable);

    /* COLD-BOOT robustness. On power-up the SPI-NOR is in its DEFAULT state; a warm
     * reset (J-Link / SYSRESET) leaves the chip powered and retains whatever mode a
     * previous session set, which hid this bug. Two cold-boot-only problems:
     *   1) unknown mode -> issue a software reset (66h enable + 99h reset) to a clean state.
     *   2) 3-byte address mode -> the image assets live at >= 16 MB (0x1000000+), which
     *      REQUIRES 4-byte addressing; the read/erase/program here all send 4 address
     *      bytes, so in the cold default (3-byte) they are misaligned -> "verzerrt" garbage.
     *      Enter 4-byte mode (B7h, with a preceding WREN for vendors that need it). */
    Ddl_Delay1ms(2u);                                  /* power-up / SPI settle      */
    CS_LOW(); xf_xfer(0x66u); CS_HIGH();               /* enable reset               */
    CS_LOW(); xf_xfer(0x99u); CS_HIGH();               /* reset device               */
    Ddl_Delay1ms(1u);                                  /* tRST recovery              */
    CS_LOW(); xf_xfer(0x06u); CS_HIGH();               /* WREN (needed by some EN4B) */
    CS_LOW(); xf_xfer(0xB7u); CS_HIGH();               /* EN4B: 4-byte address mode  */
    return 1;
}

/* REMS (0x90): manufacturer + device ID. */
uint16_t extflash_read_id(void)
{
    CS_LOW();
    xf_xfer(0x90); xf_xfer(0); xf_xfer(0); xf_xfer(0);
    uint8_t mfg = xf_xfer(0);
    uint8_t dev = xf_xfer(0);
    CS_HIGH();
    return (uint16_t)((mfg << 8) | dev);
}

static void xf_cmd(uint8_t c) { CS_LOW(); xf_xfer(c); CS_HIGH(); }

static void xf_wait_wip(void)
{
    CS_LOW();
    xf_xfer(EXTFLASH_CMD_RDSR);
    while (xf_xfer(0) & 0x01u) { }      /* WIP = bit0 */
    CS_HIGH();
}

void extflash_erase_sector(uint32_t addr)
{
    xf_cmd(EXTFLASH_CMD_WREN);
    CS_LOW();
    xf_xfer(EXTFLASH_CMD_SE);
    xf_xfer((uint8_t)(addr >> 24)); xf_xfer((uint8_t)(addr >> 16));
    xf_xfer((uint8_t)(addr >> 8));  xf_xfer((uint8_t)addr);
    CS_HIGH();
    xf_wait_wip();
}

void extflash_write(uint32_t addr, const void *src, size_t len)
{
    const uint8_t *p = (const uint8_t *)src;
    while (len) {
        uint32_t page = EXTFLASH_PAGE - (addr & (EXTFLASH_PAGE - 1));
        if (page > len) page = (uint32_t)len;
        xf_cmd(EXTFLASH_CMD_WREN);
        CS_LOW();
        xf_xfer(EXTFLASH_CMD_PP);
        xf_xfer((uint8_t)(addr >> 24)); xf_xfer((uint8_t)(addr >> 16));
        xf_xfer((uint8_t)(addr >> 8));  xf_xfer((uint8_t)addr);
        for (uint32_t i = 0; i < page; i++) xf_xfer(p[i]);
        CS_HIGH();
        xf_wait_wip();
        addr += page; p += page; len -= page;
    }
}

/* Reset the per-upload erased-sector cache (stock: clears the dedup state so the
 * next upload re-erases). The cache lives in wlan_ota's write path; here it is a
 * no-op hook kept for API compatibility. */
void extflash_reset_erasecache(void) { }

/* allow an embedded/internal-flash image source to override (e.g. for testing) */
__attribute__((weak)) int extflash_embedded_read(uint32_t addr, void *dst, uint32_t len)
{
    (void)addr; (void)dst; (void)len; return 0;
}

void extflash_read(uint32_t addr, void *dst, size_t len)
{
    if (extflash_embedded_read(addr, dst, (uint32_t)len)) return;
    uint8_t *p = (uint8_t *)dst;
    CS_LOW();
    xf_xfer(0x0B);                                  /* fast read */
    xf_xfer((uint8_t)(addr >> 24));
    xf_xfer((uint8_t)(addr >> 16));
    xf_xfer((uint8_t)(addr >> 8));
    xf_xfer((uint8_t)addr);
    xf_xfer(0x00);                                  /* dummy */
    for (uint32_t i = 0; i < len; i++) p[i] = xf_xfer(0x00);
    CS_HIGH();
}
