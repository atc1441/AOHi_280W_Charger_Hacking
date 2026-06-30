/* system_hc32f460.h - core-clock variable + system init (clean-room). */
#ifndef SYSTEM_HC32F460_H
#define SYSTEM_HC32F460_H

#include <stdint.h>

/* HRC frequency monitor: ICG1.HRCFREQSEL lives at this fixed address. */
#define HRC_FREQ_MON()      (*((volatile unsigned int *)(0x40010684UL)))

#define HRC_16MHz_VALUE     ((uint32_t)16000000UL)
#define HRC_20MHz_VALUE     ((uint32_t)20000000UL)
#define MRC_VALUE           ((uint32_t)8000000UL)
#define LRC_VALUE           ((uint32_t)32768UL)
#define XTAL_VALUE          ((uint32_t)8000000UL)
#define XTAL32_VALUE        ((uint32_t)32768UL)

extern uint32_t SystemCoreClock;
extern uint32_t HRC_VALUE;

void SystemInit(void);
void SystemCoreClockUpdate(void);

#endif /* SYSTEM_HC32F460_H */
