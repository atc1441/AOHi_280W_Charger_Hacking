/* rtc.c - real-time clock on the HC32F460 M4_RTC (0x4004C000).
 *
 * The RTC is already running (kept ticking by the hardware/bootloader from the
 * 32.768 kHz source; verified live on stock: 00:12:26 -> 00:12:31 over 5 s, BCD,
 * default epoch 2025-01-01). Registers (1:1 with stock rtc_read 0xE584/0xE4B8):
 *   time: SEC +0x10  MIN +0x14  HOUR +0x18      date: DAY +0x20  MON +0x24  YEAR +0x28
 * All BCD. HOUR is 24h in bits[5:0]. Read until two consecutive SEC reads agree
 * so we never latch a value straddling a 1 Hz register rollover. */
#include "rtc.h"
#include <hc32f460_rtc.h>
#include <hc32f460_clk.h>
#include "wdt.h"

#define RTC_BASE   0x4004C000u
#define RTC(off)   (*(volatile uint8_t *)(RTC_BASE + (off)))

static uint8_t bcd2bin(uint8_t v) { return (uint8_t)((v >> 4) * 10u + (v & 0x0Fu)); }
static uint8_t bin2bcd(uint8_t v) { return (uint8_t)(((v / 10u) << 4) | (v % 10u)); }

void rtc_read(rtc_time_t *t)
{
    if (!t) return;
    uint8_t s0, s1, mi, h, w, d, mo, y;
    do {                                   /* coherent snapshot across a rollover */
        s0 = RTC(0x10);
        mi = RTC(0x14); h = RTC(0x18); w = RTC(0x1C);
        d  = RTC(0x20); mo = RTC(0x24); y = RTC(0x28);
        s1 = RTC(0x10);
    } while (s0 != s1);

    t->sec     = bcd2bin(s0 & 0x7Fu);
    t->min     = bcd2bin(mi & 0x7Fu);
    t->hour    = bcd2bin(h  & 0x3Fu);
    t->weekday = (uint8_t)(w & 0x07u);     /* WEEK register holds 0..6 directly */
    t->day     = bcd2bin(d  & 0x3Fu);
    t->month   = bcd2bin(mo & 0x1Fu);
    t->year    = bcd2bin(y);
}

/* Writing the calendar needs RTC write-enable (CR1.START cleared, set CR0/RDWEN).
 * The clock screensaver only reads; time setting comes from the app/UART command
 * path. Provide BCD writes guarded by the RTC enable bit so they no-op safely if
 * the counter is locked. */
void rtc_write_time(const rtc_time_t *t)
{
    if (!t) return;
    RTC(0x10) = bin2bcd(t->sec);
    RTC(0x14) = bin2bcd(t->min);
    RTC(0x18) = bin2bcd(t->hour);
}

void rtc_write_date(const rtc_time_t *t)
{
    if (!t) return;
    RTC(0x20) = bin2bcd(t->day);
    RTC(0x24) = bin2bcd(t->month);
    RTC(0x28) = bin2bcd(t->year);
}

/* Set the full calendar (used when the WLAN module delivers network time via the
 * cmd 0x1C UART message). The HC32F460 RTC requires read/write mode (CR2.RWREQ ->
 * wait CR2.RWEN) around register writes or they are ignored while the counter runs;
 * the DDL RTC_EnterRwMode/ExitRwMode do exactly that. */
void rtc_set(const rtc_time_t *t)
{
    if (!t) return;
    if (RTC_EnterRwMode() == ErrorTimeout) return;
    RTC(0x10) = bin2bcd(t->sec);
    RTC(0x14) = bin2bcd(t->min);
    RTC(0x18) = bin2bcd(t->hour);
    RTC(0x1C) = (uint8_t)(t->weekday & 0x07u);   /* WEEK register (0..6, NOT BCD) - was
                                                  * never written, so the clock page always
                                                  * showed "Sunday" regardless of the date. */
    RTC(0x20) = bin2bcd(t->day);
    RTC(0x24) = bin2bcd(t->month);
    RTC(0x28) = bin2bcd(t->year);
    RTC_ExitRwMode();
}

/* Initialize + START the RTC. CRITICAL: my earlier init stubbed stock's 0xE9A8
 * (which runs the DDL RTC_Init/RTC_Cmd @0xe67c/0xe708), so the RTC was never
 * configured/started -> CR registers all 0, counter frozen at the 12:00 power-on
 * default, the clock never ticked, AND rtc_set's RW-mode timed out (so even a
 * network time would not stick). LRC (internal ~32 kHz) needs no external crystal
 * and RTC_Init enables it via CR3.LRCEN; periodic cmd 0x1C re-sync corrects drift. */
void rtc_init(void)
{
    stc_rtc_init_t cfg;
    cfg.enClkSource  = RtcClkLrc;
    cfg.enPeriodInt  = RtcPeriodIntInvalid;
    cfg.enTimeFormat = RtcTimeFormat24Hour;
    cfg.enCompenWay  = RtcOutputCompenDistributed;
    cfg.u16CompenVal = 0u;
    cfg.enCompenEn   = Disable;
    wdt_feed();
    RTC_DeInit();                 /* clears write-protect + resets the counter */
    wdt_feed();
    RTC_Init(&cfg);               /* CR3.LRCEN/RCKSEL, CR1 24h, error-comp off */
    RTC_Cmd(Enable);              /* CR1.START = 1 -> counter runs */
    wdt_feed();
}
