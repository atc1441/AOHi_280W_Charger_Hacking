/* mcu_gpio.c - PORT (GPIO) driver (clean-room).
 *
 * HC32F460 PORT registers, base 0x40053800:
 *   per-port bit registers at +0x10*port: PIDR +0x0, PODR +0x4, POER +0x6,
 *   POSR +0x8, PORR +0xA, POTR +0xC.
 *   pin control block at +0x400: PCRxy +0x40*port + 4*pin (16-bit),
 *   PFSRxy +0x40*port + 4*pin + 2 (16-bit). PWPR (write-protect) at +0x3FC. */
#include "hc32f460_gpio.h"

#define PORT_BASE        0x40053800UL
#define PORT_PIDR(p)     (*(volatile uint16_t *)(PORT_BASE + (uint32_t)(p) * 0x10u + 0x00u))
#define PORT_POSR(p)     (*(volatile uint16_t *)(PORT_BASE + (uint32_t)(p) * 0x10u + 0x08u))
#define PORT_PORR(p)     (*(volatile uint16_t *)(PORT_BASE + (uint32_t)(p) * 0x10u + 0x0Au))
#define PORT_PCR(p, pin) (*(volatile uint16_t *)(PORT_BASE + 0x400u + (uint32_t)(p) * 0x40u + (uint32_t)(pin) * 4u))
#define PORT_PFSR(p, pin)(*(volatile uint16_t *)(PORT_BASE + 0x400u + (uint32_t)(p) * 0x40u + (uint32_t)(pin) * 4u + 2u))
#define PORT_PWPR_REG    (*(volatile uint16_t *)(PORT_BASE + 0x3FCu))

/* PCR bit positions */
#define PCR_POUT   (1u << 0)
#define PCR_POUTE  (1u << 1)
#define PCR_NOD    (1u << 2)
#define PCR_DRV_Pos 4u
#define PCR_PUU    (1u << 6)
#define PCR_INVE   (1u << 9)
#define PCR_INTE   (1u << 12)
#define PCR_LTE    (1u << 14)
#define PCR_DDIS   (1u << 15)

/* PFSR bit positions */
#define PFSR_FSEL_Msk  0x3Fu
#define PFSR_BFE       (1u << 8)

void PORT_Unlock(void) { PORT_PWPR_REG = 0xA501u; }
void PORT_Lock(void)   { PORT_PWPR_REG = 0xA500u; }

en_result_t PORT_Init(en_port_t enPort, uint16_t u16Pin, const stc_port_init_t *p)
{
    PORT_Unlock();
    for (uint8_t pin = 0u; pin < 16u; pin++)
    {
        if (u16Pin & (1u << pin))
        {
            /* keep the static output level (POUT); rewrite the config fields */
            uint16_t v = PORT_PCR(enPort, pin)
                       & ~(PCR_POUTE | PCR_NOD | (3u << PCR_DRV_Pos) | PCR_PUU
                           | PCR_INVE | PCR_INTE | PCR_LTE | PCR_DDIS);

            if (p->enLatch)   v |= PCR_LTE;
            if (p->enExInt)   v |= PCR_INTE;
            if (p->enInvert)  v |= PCR_INVE;
            if (p->enPullUp)  v |= PCR_PUU;
            if (p->enPinOType) v |= PCR_NOD;
            v |= ((uint16_t)p->enPinDrv & 3u) << PCR_DRV_Pos;

            switch (p->enPinMode)
            {
                case Pin_Mode_In:  v &= ~PCR_DDIS; v &= ~PCR_POUTE; break;
                case Pin_Mode_Out: v &= ~PCR_DDIS; v |=  PCR_POUTE; break;
                case Pin_Mode_Ana: v |=  PCR_DDIS;                  break;
                default: break;
            }
            PORT_PCR(enPort, pin) = v;

            /* pin sub-function enable bit (main function stays GPIO here) */
            uint16_t f = PORT_PFSR(enPort, pin) & ~PFSR_BFE;
            if (p->enPinSubFunc) f |= PFSR_BFE;
            PORT_PFSR(enPort, pin) = f;
        }
    }
    PORT_Lock();
    return Ok;
}

en_result_t PORT_SetFunc(en_port_t enPort, uint16_t u16Pin,
                         en_port_func_t enFuncSel, en_functional_state_t enSubFunc)
{
    PORT_Unlock();
    for (uint8_t pin = 0u; pin < 16u; pin++)
    {
        if (u16Pin & (1u << pin))
        {
            uint16_t f = PORT_PFSR(enPort, pin) & ~(PFSR_FSEL_Msk | PFSR_BFE);
            f |= ((uint16_t)enFuncSel & PFSR_FSEL_Msk);
            if (Enable == enSubFunc) f |= PFSR_BFE;
            PORT_PFSR(enPort, pin) = f;
        }
    }
    PORT_Lock();
    return Ok;
}

en_result_t PORT_SetBits(en_port_t enPort, uint16_t u16Pin)
{
    PORT_POSR(enPort) |= u16Pin;
    return Ok;
}

en_result_t PORT_ResetBits(en_port_t enPort, uint16_t u16Pin)
{
    PORT_PORR(enPort) |= u16Pin;
    return Ok;
}

en_flag_status_t PORT_GetBit(en_port_t enPort, en_pin_t enPin)
{
    return (PORT_PIDR(enPort) & (uint16_t)enPin) ? Set : Reset;
}
