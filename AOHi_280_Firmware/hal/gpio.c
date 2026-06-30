/* gpio.c - old GPIO HAL API on the HC32F460 DDL PORT driver (no raw registers). */
#include "gpio.h"
#include <hc32_ddl.h>
#include <hc32f460_gpio.h>

int gpio_read(uint8_t port, uint16_t mask)
{
    for (uint8_t b = 0; b < 16; b++)
        if ((mask >> b) & 1u)
            if (PORT_GetBit((en_port_t)port, (en_pin_t)(1u << b)) == Set) return 1;
    return 0;
}

void gpio_set_pins(uint8_t port, uint16_t mask)   { PORT_SetBits((en_port_t)port, mask); }
void gpio_clear_pins(uint8_t port, uint16_t mask) { PORT_ResetBits((en_port_t)port, mask); }

void gpio_config(uint8_t port, uint16_t mask, const gpio_cfg_t *cfg)
{
    stc_port_init_t init = {0};
    /* w3 bit15 (0x8000) = PCR DDIS: analog mode (ADC inputs). Stock configures
     * PA0/PA1 this way; without it they'd be digital outputs (live A/B mismatch
     * on POERA). dir==2 = digital input, else digital output. */
    init.enPinMode  = (cfg && (cfg->w3 & 0x8000u)) ? Pin_Mode_Ana
                    : (cfg && cfg->dir == 2u)      ? Pin_Mode_In
                    :                                Pin_Mode_Out;
    init.enPullUp   = (cfg && cfg->pull) ? Enable : Disable;
    init.enPinDrv   = Pin_Drv_H;
    /* cfg->speed as an OPEN-DRAIN flag (NOD): needed for bit-banged I2C (PDC SDA/SCL). With
     * push-pull (Cmos), switching SDA to output-high while SCL is still high after reading a
     * 0 data bit forces a low->high edge = a spurious I2C STOP, so the slave aborts after the
     * first byte. Open-drain makes "high" a release (hi-Z), eliminating the glitch. */
    init.enPinOType = (cfg && cfg->speed) ? Pin_OType_Od : Pin_OType_Cmos;
    PORT_Init((en_port_t)port, mask, &init);
}

void gpio_set_altfunc(uint8_t port, uint16_t mask, uint8_t af)
{
    PORT_SetFunc((en_port_t)port, mask, (en_port_func_t)af, Disable);
}
