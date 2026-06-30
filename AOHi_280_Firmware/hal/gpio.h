/* gpio.h - GPIO HAL (old BWX468 API) implemented on the HC32F460 DDL PORT driver.
 * Old port index N maps to DDL en_port_t N (port0=PortA, port1=PortB, ...); the old
 * pin "mask" is the DDL u16Pin bitmask directly. */
#ifndef HAL_GPIO_H
#define HAL_GPIO_H
#include <stdint.h>

typedef struct {
    uint16_t mode;     /* word[0] */
    uint16_t w1;       /* word[1] */
    uint16_t dir;      /* word[2]: 0 = output, 2 = input  */
    uint16_t w3;       /* word[3] */
    uint16_t pull;     /* word[4] */
    uint16_t speed;    /* word[5] */
    uint16_t w6, w7, w8;
} gpio_cfg_t;

int  gpio_read(uint8_t port, uint16_t mask);
void gpio_set_pins(uint8_t port, uint16_t mask);
void gpio_clear_pins(uint8_t port, uint16_t mask);
void gpio_config(uint8_t port, uint16_t mask, const gpio_cfg_t *cfg);
void gpio_set_altfunc(uint8_t port, uint16_t mask, uint8_t af);

#endif /* HAL_GPIO_H */
