/* uart.h - UART HAL for BWX468 (UART0 @ 0x4001D000). */
#ifndef HAL_UART_H
#define HAL_UART_H
#include <stdint.h>

typedef void (*uart_rx_cb_t)(uint8_t byte);

void uart_init(uint32_t baud);
void uart_set_rx_callback(uart_rx_cb_t cb);  /* invoked from the RX ISR per byte */
void uart_enable_rx_irq(void);               /* stock sub_12090: CTRL |= 0xB0000 */
void uart_rx_isr(void);                      /* call from the UART RX vector */
void uart_poll(void);                        /* drain RX to the callback (call each main loop) */
void wlan_module_reset(uint8_t off);         /* sub_124B0: PA4 power/reset the WLAN module */
void uart_putc(char c);
void uart_puts(const char *s);
void uart_send_buf(const void *buf, uint16_t len);
uint8_t uart_read_byte(void);
int  uart_tx_ready(void);

#endif /* HAL_UART_H */
