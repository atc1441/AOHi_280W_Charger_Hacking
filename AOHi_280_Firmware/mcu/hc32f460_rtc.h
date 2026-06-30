/* hc32f460_rtc.h - real-time clock API (clean-room). */
#ifndef HC32F460_RTC_H
#define HC32F460_RTC_H

#include "hc32_common.h"

typedef enum { RtcClkXtal32 = 0u, RtcClkLrc = 1u } en_rtc_clk_source_t;
typedef enum { RtcPeriodIntInvalid = 0u } en_rtc_period_int_type_t;
typedef enum { RtcTimeFormat12Hour = 0u, RtcTimeFormat24Hour = 1u } en_rtc_time_format_t;
typedef enum { RtcOutputCompenDistributed = 0u, RtcOutputCompenUniform = 1u } en_rtc_output_compen_t;

typedef struct stc_rtc_init
{
    en_rtc_clk_source_t      enClkSource;
    en_rtc_period_int_type_t enPeriodInt;
    en_rtc_time_format_t     enTimeFormat;
    en_rtc_output_compen_t   enCompenWay;
    uint16_t                 u16CompenVal;
    en_functional_state_t    enCompenEn;
} stc_rtc_init_t;

en_result_t RTC_DeInit(void);
en_result_t RTC_Init(const stc_rtc_init_t *pstcRtcInit);
en_result_t RTC_Cmd(en_functional_state_t enNewSta);
en_result_t RTC_EnterRwMode(void);
en_result_t RTC_ExitRwMode(void);

#endif /* HC32F460_RTC_H */
