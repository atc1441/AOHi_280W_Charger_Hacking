/* spi.h - blocking SPI master for the LCD (SPI0 @ 0x40020000). */
#ifndef HAL_SPI_H
#define HAL_SPI_H
#include <stdint.h>
#include <stddef.h>

void spi0_init(void);                           /* configure SPI0 for the LCD (stock sub_EC6C) */
void spi0_arm(void);                             /* CR=0x48 before a CPU transmit (bit6 one-shot) */
void spi0_write(const void *buf, size_t len);  /* blocking byte stream */
void spi0_wait_idle(void);                      /* wait until not busy */

#endif /* HAL_SPI_H */
