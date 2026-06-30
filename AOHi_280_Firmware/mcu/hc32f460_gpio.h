/* hc32f460_gpio.h - PORT (GPIO) driver API (clean-room). */
#ifndef HC32F460_GPIO_H
#define HC32F460_GPIO_H

#include "hc32_common.h"

typedef enum en_port
{
    PortA = 0, PortB = 1, PortC = 2, PortD = 3, PortE = 4, PortH = 5,
} en_port_t;

/* pin bit masks (en_pin_t) */
typedef enum en_pin
{
    Pin00 = 1u<<0,  Pin01 = 1u<<1,  Pin02 = 1u<<2,  Pin03 = 1u<<3,
    Pin04 = 1u<<4,  Pin05 = 1u<<5,  Pin06 = 1u<<6,  Pin07 = 1u<<7,
    Pin08 = 1u<<8,  Pin09 = 1u<<9,  Pin10 = 1u<<10, Pin11 = 1u<<11,
    Pin12 = 1u<<12, Pin13 = 1u<<13, Pin14 = 1u<<14, Pin15 = 1u<<15,
} en_pin_t;

typedef enum en_pin_mode { Pin_Mode_In = 0, Pin_Mode_Out = 1, Pin_Mode_Ana = 2, } en_pin_mode_t;
typedef enum en_pin_drv  { Pin_Drv_L = 0, Pin_Drv_M = 1, Pin_Drv_H = 2, } en_pin_drv_t;
typedef enum en_pin_o_type { Pin_OType_Cmos = 0, Pin_OType_Od = 1, } en_pin_o_type_t;

/* peripheral routing (subset used by the firmware) */
typedef enum en_port_func
{
    Func_Gpio       = 0u,
    Func_Tim6       = 3u,
    Func_Usart1_Tx  = 32u,
    Func_Usart1_Rx  = 33u,
    Func_Spi1_Mosi  = 40u,
    Func_Spi3_Mosi  = 40u,
    Func_Spi1_Miso  = 41u,
    Func_Spi3_Miso  = 41u,
    Func_Spi1_Sck   = 43u,
    Func_Spi3_Sck   = 43u,
} en_port_func_t;

typedef struct stc_port_init
{
    en_pin_mode_t         enPinMode;
    en_functional_state_t enLatch;
    en_functional_state_t enExInt;
    en_functional_state_t enInvert;
    en_functional_state_t enPullUp;
    en_pin_drv_t          enPinDrv;
    en_pin_o_type_t       enPinOType;
    en_functional_state_t enPinSubFunc;
} stc_port_init_t;

en_result_t      PORT_Init(en_port_t enPort, uint16_t u16Pin, const stc_port_init_t *pstcPortInit);
void             PORT_Unlock(void);
void             PORT_Lock(void);
en_flag_status_t PORT_GetBit(en_port_t enPort, en_pin_t enPin);
en_result_t      PORT_SetBits(en_port_t enPort, uint16_t u16Pin);
en_result_t      PORT_ResetBits(en_port_t enPort, uint16_t u16Pin);
en_result_t      PORT_SetFunc(en_port_t enPort, uint16_t u16Pin,
                              en_port_func_t enFuncSel, en_functional_state_t enSubFunc);

#endif /* HC32F460_GPIO_H */
