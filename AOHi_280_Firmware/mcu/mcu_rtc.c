/* mcu_rtc.c - real-time clock control driver (clean-room).
 *
 * Configures the calendar counter (clock source, 24h format) and the running /
 * read-write-mode handshake hal/rtc.c needs to set network time on a live RTC.
 * The calendar BCD registers themselves are read/written by hal/rtc.c. */
#include "hc32f460_rtc.h"
#include "system_hc32f460.h"

en_result_t RTC_DeInit(void)
{
    uint8_t sta;
    uint32_t timeout = SystemCoreClock >> 8u, cnt = 0u;

    M4_RTC->CR0_f.RESET = 0u;
    do { sta = (uint8_t)M4_RTC->CR0_f.RESET; cnt++; }
    while ((cnt < timeout) && (sta == 1u));

    if (1u == sta) return ErrorTimeout;
    M4_RTC->CR0_f.RESET = 1u;     /* reset all RTC registers */
    return Ok;
}

en_result_t RTC_Init(const stc_rtc_init_t *p)
{
    if (NULL == p) return Error;

    if (RtcClkLrc == p->enClkSource) M4_RTC->CR3_f.LRCEN = 1u;
    M4_RTC->CR3_f.RCKSEL = p->enClkSource;

    M4_RTC->CR1_f.PRDS     = p->enPeriodInt;
    M4_RTC->CR1_f.AMPM     = p->enTimeFormat;
    M4_RTC->CR1_f.ONEHZSEL = p->enCompenWay;

    M4_RTC->ERRCRH_f.COMP8 = ((uint32_t)p->u16CompenVal >> 8u) & 0x01u;
    M4_RTC->ERRCRL         = (uint8_t)(p->u16CompenVal & 0x00FFu);
    M4_RTC->ERRCRH_f.COMPEN = p->enCompenEn;
    return Ok;
}

en_result_t RTC_Cmd(en_functional_state_t enNewSta)
{
    M4_RTC->CR1_f.START = enNewSta;
    return Ok;
}

en_result_t RTC_EnterRwMode(void)
{
    uint8_t sta;
    uint32_t timeout = SystemCoreClock >> 8u, cnt = 0u;

    if (0u != M4_RTC->CR1_f.START)
    {
        M4_RTC->CR2_f.RWREQ = 1u;
        do { sta = (uint8_t)M4_RTC->CR2_f.RWEN; cnt++; }
        while ((cnt < timeout) && (sta == 0u));
        if (0u == sta) return ErrorTimeout;
    }
    return Ok;
}

en_result_t RTC_ExitRwMode(void)
{
    uint8_t sta;
    uint32_t timeout = SystemCoreClock >> 8u, cnt = 0u;

    if (0u != M4_RTC->CR1_f.START)
    {
        M4_RTC->CR2_f.RWREQ = 0u;
        do { sta = (uint8_t)M4_RTC->CR2_f.RWEN; cnt++; }
        while ((cnt < timeout) && (sta == 1u));
        if (1u == sta) return ErrorTimeout;
    }
    return Ok;
}
