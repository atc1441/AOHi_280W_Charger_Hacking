/* mcu_misc.c - small peripheral helpers (clean-room):
 *   PWC  : peripheral clock gating (FCG1/FCG2)
 *   SWDT : watchdog refresh
 *   INTC : shared-interrupt-line registration (enIrqRegistration) */
#include "hc32f460_pwc.h"
#include "hc32f460_swdt.h"
#include "hc32f460_interrupts.h"

/* ---- peripheral clock gate (1 = stopped, 0 = running) ---- */
void PWC_Fcg1PeriphClockCmd(uint32_t u32Fcg1Periph, en_functional_state_t enNewState)
{
    if (Enable == enNewState) M4_MSTP->FCG1 &= ~u32Fcg1Periph;
    else                      M4_MSTP->FCG1 |=  u32Fcg1Periph;
}

void PWC_Fcg2PeriphClockCmd(uint32_t u32Fcg2Periph, en_functional_state_t enNewState)
{
    if (Enable == enNewState) M4_MSTP->FCG2 &= ~u32Fcg2Periph;
    else                      M4_MSTP->FCG2 |=  u32Fcg2Periph;
}

/* ---- watchdog refresh: write the two-key sequence ---- */
en_result_t SWDT_RefreshCounter(void)
{
    M4_SWDT->RR = 0x0123u;
    M4_SWDT->RR = 0x3210u;
    return Ok;
}

/* ---- route a peripheral interrupt source onto an NVIC line ----
 * Writes the source number into the INTC selector for the chosen IRQ. The
 * selector resets to 0x1FF (unassigned); a second registration of a line that
 * is already taken returns ErrorUninitialized. The actual ISR is a strong
 * IRQxxx_Handler symbol, so the callback array the vendor used is unnecessary. */
en_result_t enIrqRegistration(const stc_irq_regi_conf_t *p)
{
    volatile stc_intc_sel_field_t *sel =
        (volatile stc_intc_sel_field_t *)((uint32_t)&M4_INTC->SEL0 + 4u * (uint32_t)p->enIRQn);

    if (0x1FFu == sel->INTSEL)
    {
        sel->INTSEL = (uint32_t)p->enIntSrc;
        return Ok;
    }
    return ErrorUninitialized;
}
