/* hc32f460_interrupts.h - shared INTC registration API (clean-room).
 *
 * On the HC32F460 each of the 144 peripheral interrupt sources is routed onto an
 * NVIC line through an INTC_SELx selector register. enIrqRegistration() programs
 * that selector. The firmware uses one line (USART1 RX onto Int000), with a
 * strong IRQ000_Handler defined in hal/uart.c. */
#ifndef HC32F460_INTERRUPTS_H
#define HC32F460_INTERRUPTS_H

#include "hc32_common.h"

/* IRQ priority levels (0 = highest, 15 = lowest with 4 priority bits). */
#define DDL_IRQ_PRIORITY_DEFAULT    15u
#define DDL_IRQ_PRIORITY_00         0u
#define DDL_IRQ_PRIORITY_01         1u
#define DDL_IRQ_PRIORITY_15         15u

/* interrupt sources (subset used) */
typedef enum en_int_src
{
    INT_USART1_RI = 279u,
} en_int_src_t;

typedef struct stc_irq_regi_conf
{
    en_int_src_t enIntSrc;
    IRQn_Type    enIRQn;
    func_ptr_t   pfnCallback;
} stc_irq_regi_conf_t;

en_result_t enIrqRegistration(const stc_irq_regi_conf_t *pstcIrqRegiConf);

#endif /* HC32F460_INTERRUPTS_H */
