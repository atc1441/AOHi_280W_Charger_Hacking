/* wlan.c - UART link protocol (clean-room from stock wlan_parse_rx/dispatch). */
#include "wlan.h"
#include "ota.h"
#include "uart.h"
#include "ringbuf.h"
#include "tick.h"

/* Parser FSM states match the stock g_wlan_payload[1528] state byte. */
enum { ST_MAGIC0, ST_MAGIC1, ST_VER, ST_SEQ, ST_CMD, ST_FLAG,
       ST_LENHI, ST_LENLO, ST_PAYLOAD, ST_CKSUM };

static uint8_t  s_state;
static uint8_t  s_ver, s_seq, s_cmd, s_flag;
static uint16_t s_len, s_idx;
static uint8_t  s_cksum;
static uint8_t  s_payload[WLAN_MAX_PAYLOAD + 8];
static uint32_t s_last_rx_tick;

void wlan_init(void)
{
    ringbuf_init();
    s_state = ST_MAGIC0;
    s_len = s_idx = 0;
    s_cksum = 0;
}

void wlan_rx_isr_byte(uint8_t b)
{
    ringbuf_put(b);
    s_last_rx_tick = get_tick_ms();
}

void wlan_parse_rx(void)
{
    /* Idle-timeout reset (stock: >= 0x2711 ms since last byte). */
    if (s_state != ST_MAGIC0) {
        uint32_t now = get_tick_ms();
        if (now > s_last_rx_tick && now - s_last_rx_tick >= WLAN_RX_TIMEOUT) {
            s_state = ST_MAGIC0; s_len = s_idx = 0; s_cksum = 0;
        }
    }

    uint8_t v;
    while (ringbuf_get(&v)) {
        switch (s_state) {
        case ST_MAGIC0:
            if (v == WLAN_MAGIC0) { s_state = ST_MAGIC1; s_cksum = v; }
            break;
        case ST_MAGIC1:
            if (v == WLAN_MAGIC1) { s_state = ST_VER; s_cksum += v; }
            else                  { s_state = ST_MAGIC0; }
            break;
        case ST_VER:    s_ver  = v; s_cksum += v; s_state = ST_SEQ;   break;
        case ST_SEQ:    s_seq  = v; s_cksum += v; s_state = ST_CMD;   break;
        case ST_CMD:    s_cmd  = v; s_cksum += v; s_state = ST_FLAG;  break;
        case ST_FLAG:   s_flag = v; s_cksum += v; s_state = ST_LENHI; break;
        case ST_LENHI:  s_len  = (uint16_t)(v << 8); s_cksum += v; s_state = ST_LENLO; break;
        case ST_LENLO:
            s_len |= v; s_cksum += v;
            if (s_len <= WLAN_MAX_PAYLOAD) {
                if (s_len) { s_state = ST_PAYLOAD; s_idx = 0; }
                else       { s_state = ST_CKSUM; }
            } else {
                s_state = ST_MAGIC0; s_len = s_idx = 0; s_cksum = 0;
                return;
            }
            break;
        case ST_PAYLOAD:
            if (s_idx < s_len) { s_payload[s_idx++] = v; s_cksum += v; }
            if (s_idx >= s_len) s_state = ST_CKSUM;
            break;
        case ST_CKSUM:
            if (s_cksum == v) {
                wlan_dispatch_cmd(s_cmd, s_seq, s_payload, s_len);
            }
            s_state = ST_MAGIC0; s_len = s_idx = 0; s_cksum = 0;
            break;
        }
    }
    (void)s_ver; (void)s_flag;
}

/* Command handlers are weak so the application can override each one.
 * Defaults: ACK-only / no-op, matching a minimal but valid responder. */
#define WLAN_WEAK __attribute__((weak))
WLAN_WEAK void wlan_on_hello   (uint8_t seq, const uint8_t *p, uint16_t n) { (void)p;(void)n; wlan_send(WLAN_CMD_ACK, seq, 0, 0); }
WLAN_WEAK void wlan_on_info    (uint8_t seq, const uint8_t *p, uint16_t n) { (void)seq;(void)p;(void)n; }
WLAN_WEAK void wlan_on_ack     (uint8_t seq, const uint8_t *p, uint16_t n) { (void)seq;(void)p;(void)n; }
WLAN_WEAK void wlan_on_03      (uint8_t seq, const uint8_t *p, uint16_t n) { (void)p;(void)n; wlan_send(WLAN_CMD_03, seq, 0, 0); }
WLAN_WEAK void wlan_on_status  (uint8_t seq, const uint8_t *p, uint16_t n) { (void)seq;(void)p;(void)n; }
WLAN_WEAK void wlan_on_set_tz  (uint8_t seq, const uint8_t *p, uint16_t n) { (void)seq;(void)p;(void)n; }
WLAN_WEAK void wlan_on_set_config(uint8_t seq, const uint8_t *p, uint16_t n) { (void)seq;(void)p;(void)n; }
WLAN_WEAK void wlan_on_time    (uint8_t seq, const uint8_t *p, uint16_t n) { (void)p;(void)n; wlan_send(WLAN_CMD_TIME_SYNC, seq, 0, 0); }
WLAN_WEAK void wlan_on_weather (uint8_t cmd, uint8_t seq, const uint8_t *p, uint16_t n) { (void)p;(void)n; wlan_send(cmd, seq, 0, 0); }
WLAN_WEAK void wlan_on_09      (uint8_t seq, const uint8_t *p, uint16_t n) { (void)seq;(void)p;(void)n; }
WLAN_WEAK void wlan_on_0F      (uint8_t seq, const uint8_t *p, uint16_t n) { (void)seq;(void)p;(void)n; }
WLAN_WEAK void wlan_on_fw      (uint8_t cmd, uint8_t seq, const uint8_t *p, uint16_t n) { (void)cmd;(void)seq;(void)p;(void)n; }
WLAN_WEAK void wlan_on_image   (uint8_t cmd, uint8_t seq, const uint8_t *p, uint16_t n) { (void)cmd;(void)seq;(void)p;(void)n; }
WLAN_WEAK void wlan_on_other   (uint8_t cmd, uint8_t seq, const uint8_t *p, uint16_t n) { (void)cmd;(void)seq;(void)p;(void)n; }

volatile uint8_t g_wlan_alive;     /* set on ANY RX frame: the module is talking to us */

void wlan_dispatch_cmd(uint8_t cmd, uint8_t seq, const uint8_t *payload, uint16_t len)
{
    g_wlan_alive = 1;
    switch (cmd) {
    case WLAN_CMD_HELLO:  wlan_on_hello(seq, payload, len);  break;
    case WLAN_CMD_INFO:   wlan_on_info(seq, payload, len);   break;
    case WLAN_CMD_ACK:    wlan_on_ack(seq, payload, len);    break;
    case WLAN_CMD_03:     wlan_on_03(seq, payload, len);     break;
    case WLAN_CMD_STATUS: wlan_on_status(seq, payload, len); break;
    case WLAN_CMD_SET_TZ: wlan_on_set_tz(seq, payload, len); break;
    case WLAN_CMD_SET_CONFIG: wlan_on_set_config(seq, payload, len); break;
    case WLAN_CMD_09:     wlan_on_09(seq, payload, len);     break;
    case WLAN_CMD_0D:     wlan_cmd_0D(seq, payload, len);    break;  /* pair/identify flash */
    case WLAN_CMD_0F:     wlan_on_0F(seq, payload, len);     break;
    /* cmd 0x15 = time-sync: 4-byte BE Unix timestamp -> RTC (stock 0x15D50), then ACK. */
    case WLAN_CMD_TIME_SYNC:
        wlan_on_time(seq, payload, len);
        wlan_send(0x15, seq, 0, 0);
        break;
    /* Firmware OTA (stock 0x165F4 / 0x15D84 / 0x16514): stage -> internal 0x38000,
     * set boot flags, reboot into the bootloader on commit. */
    case WLAN_CMD_FW_PREPARE: wlan_cmd_fw_prepare(seq, payload, len); break;  /* 0x1A */
    case WLAN_CMD_FW_DATA:    wlan_cmd_fw_data(seq, payload, len);    break;  /* 0x1B */
    case WLAN_CMD_FW_COMMIT:  wlan_cmd_fw_commit(seq, payload, len);  break;  /* 0x1C */
    /* Asset image upload (stock 0x16BA8 / 0x168BC / 0x16AC0): external-flash slots. */
    case WLAN_CMD_IMG_BEGIN:  wlan_cmd_image_begin(seq, payload, len); break; /* 0x20 */
    case WLAN_CMD_IMG_WRITE:  wlan_cmd_image_write(seq, payload, len); break; /* 0x21 */
    case WLAN_CMD_IMG_END:    wlan_cmd_image_end(seq, payload, len);   break; /* 0x22 */
    default:
        wlan_on_other(cmd, seq, payload, len); break;
    }
}

void wlan_send(uint8_t cmd, uint8_t seq, const void *payload, uint16_t len)
{
    uint8_t hdr[8];
    uint8_t cksum = 0;
    const uint8_t *p = (const uint8_t *)payload;

    hdr[0] = WLAN_MAGIC0;
    hdr[1] = WLAN_MAGIC1;
    hdr[2] = 0x00;                 /* version/reserved */
    hdr[3] = seq;
    hdr[4] = cmd;
    hdr[5] = 0x00;                 /* flag/reserved */
    hdr[6] = (uint8_t)(len >> 8);
    hdr[7] = (uint8_t)len;
    for (int i = 0; i < 8; i++) cksum += hdr[i];
    for (uint16_t i = 0; i < len; i++) cksum += p[i];

    uart_send_buf(hdr, 8);
    if (len) uart_send_buf(p, len);
    uart_send_buf(&cksum, 1);
}
