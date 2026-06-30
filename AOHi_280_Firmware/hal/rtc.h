/* rtc.h - real-time clock @0x4004C000 (clean-room from stock rtc_read_time/
 * rtc_write_time 0xE584/0xE854 and rtc_write_date 0xE770).
 *   time : +0x10 sec  +0x14 min  +0x18 hour
 *   date : +0x20 day  +0x24 month +0x28 year */
#ifndef HAL_RTC_H
#define HAL_RTC_H
#include <stdint.h>
typedef struct { uint8_t year, month, day, hour, min, sec, weekday; } rtc_time_t;
void rtc_read(rtc_time_t *t);     /* read time (+date) registers */
void rtc_write_time(const rtc_time_t *t);  /* set h/m/s */
void rtc_write_date(const rtc_time_t *t);  /* set y/mo/d */
void rtc_set(const rtc_time_t *t);         /* set full calendar (RW-mode) - UART time */
void rtc_init(void);                       /* configure + START the RTC (LRC clock) */
#endif
