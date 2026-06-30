/* hc32f460_utility.h - small utility helpers (clean-room). */
#ifndef HC32F460_UTILITY_H
#define HC32F460_UTILITY_H

#include "hc32_common.h"

/* asserts are compiled out in this build */
#define DDL_ASSERT(x)   ((void)0)

void Ddl_Delay1ms(uint32_t u32Cnt);

#endif /* HC32F460_UTILITY_H */
