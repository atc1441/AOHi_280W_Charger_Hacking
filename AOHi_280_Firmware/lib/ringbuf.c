/* ringbuf.c - byte ring buffer (clean-room from stock ringbuf_*). */
#include "ringbuf.h"

static volatile uint8_t  s_buf[RINGBUF_SIZE];
static volatile uint16_t s_head, s_tail;

void ringbuf_init(void) { s_head = s_tail = 0; }

int ringbuf_is_empty(void) { return s_head == s_tail; }

int ringbuf_put(uint8_t b)
{
    uint16_t next = (uint16_t)((s_head + 1) % RINGBUF_SIZE);
    if (next == s_tail) return 0;            /* full */
    s_buf[s_head] = b;
    s_head = next;
    return 1;
}

int ringbuf_get(uint8_t *out)
{
    if (s_head == s_tail) return 0;          /* empty */
    *out = s_buf[s_tail];
    s_tail = (uint16_t)((s_tail + 1) % RINGBUF_SIZE);
    return 1;
}

int ringbuf_count(void)
{
    return (int)((s_head - s_tail + RINGBUF_SIZE) % RINGBUF_SIZE);
}
