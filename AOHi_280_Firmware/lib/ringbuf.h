/* ringbuf.h - byte ring buffer for UART RX (from stock ringbuf_*). */
#ifndef LIB_RINGBUF_H
#define LIB_RINGBUF_H
#include <stdint.h>

#define RINGBUF_SIZE 256

void    ringbuf_init(void);
int     ringbuf_is_empty(void);
int     ringbuf_put(uint8_t b);          /* 1 on success, 0 if full */
int     ringbuf_get(uint8_t *out);       /* 1 on success, 0 if empty */
int     ringbuf_count(void);

#endif /* LIB_RINGBUF_H */
