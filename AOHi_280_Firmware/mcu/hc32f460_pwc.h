/* hc32f460_pwc.h - peripheral clock gating (FCG) API (clean-room). */
#ifndef HC32F460_PWC_H
#define HC32F460_PWC_H

#include "hc32_common.h"

/* FCG1 peripheral bits (1 = clock stopped, 0 = clock running) */
#define PWC_FCG1_PERIPH_SPI1        ((uint32_t)0x00010000)
#define PWC_FCG1_PERIPH_SPI3        ((uint32_t)0x00040000)
#define PWC_FCG1_PERIPH_USART1      ((uint32_t)0x01000000)
/* FCG2 peripheral bits */
#define PWC_FCG2_PERIPH_TIM61       ((uint32_t)0x00010000)

void PWC_Fcg1PeriphClockCmd(uint32_t u32Fcg1Periph, en_functional_state_t enNewState);
void PWC_Fcg2PeriphClockCmd(uint32_t u32Fcg2Periph, en_functional_state_t enNewState);

#endif /* HC32F460_PWC_H */
