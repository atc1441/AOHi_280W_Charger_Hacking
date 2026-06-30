/* wlan.h - UART link protocol to the companion WLAN module.
 *
 * Reconstructed from stock wlan_parse_rx / wlan_dispatch_cmd / uart_send_buf.
 * Frame: 0x55 0xAA <ver> <seq> <cmd> <flag> <lenHi> <lenLo> <payload..> <cksum8>
 * cksum8 = 8-bit sum of every byte from 0x55 through the last payload byte.
 * Max payload 1016 bytes. RX idle timeout 10001 ms (0x2711).
 */
#ifndef APP_WLAN_H
#define APP_WLAN_H
#include <stdint.h>

#define WLAN_MAGIC0      0x55
#define WLAN_MAGIC1      0xAA
#define WLAN_MAX_PAYLOAD 1016
#define WLAN_RX_TIMEOUT  10001u

/* Command IDs (verified 1:1 from stock wlan_dispatch_cmd @0xBAD0). */
enum {
    WLAN_CMD_HELLO      = 0x00,
    WLAN_CMD_INFO       = 0x01,
    WLAN_CMD_ACK        = 0x02,
    WLAN_CMD_03         = 0x03,
    WLAN_CMD_SET_CONFIG = 0x06,
    WLAN_CMD_STATUS     = 0x08,
    WLAN_CMD_09         = 0x09,
    WLAN_CMD_0D         = 0x0D,   /* pair/identify: full-screen red/green flash (stock 0xBA28) */
    WLAN_CMD_0F         = 0x0F,
    WLAN_CMD_SET_TZ     = 0x14,
    WLAN_CMD_TIME_SYNC  = 0x15,   /* RTC set from BE32 Unix time (stock 0x15D50)      */
    /* Firmware OTA bundle (stock 0x165F4 / 0x15D84 / 0x16514). The earlier guess that
     * 0x1A/0x1B/0x1C/0x20/0x21/0x22 were time/weather/data-sync was WRONG: 0x1A-0x1C
     * stream firmware into internal staging 0x38000 + reboot, 0x20-0x22 upload asset
     * images into external flash. Time comes from 0x15; stock has no weather command. */
    WLAN_CMD_FW_PREPARE = 0x1A,   /* announce size, erase staging, show update page   */
    WLAN_CMD_FW_DATA    = 0x1B,   /* stream [hdr][firmware][asset] chunks             */
    WLAN_CMD_FW_COMMIT  = 0x1C,   /* set boot flags + reboot -> bootloader flashes    */
    WLAN_CMD_IMG_BEGIN  = 0x20,   /* asset image upload begin (stock 0x16BA8)         */
    WLAN_CMD_IMG_WRITE  = 0x21,   /* asset image data        (stock 0x168BC)         */
    WLAN_CMD_IMG_END    = 0x22,   /* asset image finalize + apply (stock 0x16AC0)     */
};

/* Live telemetry the status response (cmd 8 -> resp 7) reports. Values are the
 * per-port voltage/current/power the stock packs as float*100 TLV fields. */
#define WLAN_NUM_PORTS 6
typedef struct {
    uint8_t  charge_enable;            /* TLV id 1 */
    uint8_t  mode;                     /* TLV id 2 (1..3) */
    uint16_t v_x100[WLAN_NUM_PORTS];   /* per-port voltage *100  (ids 5,8,..) */
    uint16_t i_x100[WLAN_NUM_PORTS];   /* per-port current *100               */
    uint16_t p_x100[WLAN_NUM_PORTS];   /* per-port power   *100               */
    uint8_t  port_flag[WLAN_NUM_PORTS];/* connection flags   (ids 0x18..0x1D) */
    uint16_t max_power;                /* stock max_of_channels(): max over outlets */
} wlan_telemetry_t;
extern wlan_telemetry_t g_telemetry;

/* TLV response assembly (stock wlan_pack_tlv + status serialiser). */
int  wlan_tlv_set(uint8_t id, uint8_t type, uint8_t size, const void *data);
void wlan_tlv_send(uint8_t resp_cmd, uint8_t seq);

void wlan_init(void);
void wlan_rx_isr_byte(uint8_t b);   /* feed one received byte (from UART IRQ) */
void wlan_parse_rx(void);           /* drain ring buffer, run parser FSM      */
void wlan_dispatch_cmd(uint8_t cmd, uint8_t seq, const uint8_t *payload, uint16_t len);

/* Build + send a framed packet to the module. */
void wlan_send(uint8_t cmd, uint8_t seq, const void *payload, uint16_t len);

/* Local-event notifications to the companion module (stock wlan_send_*_cmd). */
void wlan_send_click(void);   /* button-press notify (cmd 0x04)   */
void wlan_send_reset(void);   /* reset / re-pair request (cmd 0x0D) */
void wlan_send_get_status(void); /* status poll (cmd 0x08)         */
void wlan_on_set_config(uint8_t seq, const uint8_t *p, uint16_t n); /* cmd 0x06 */
void wlan_on_time(uint8_t seq, const uint8_t *p, uint16_t n);       /* cmd 0x1C time/date */
void wlan_on_weather(uint8_t cmd, uint8_t seq, const uint8_t *p, uint16_t n); /* 0x21/0x22 */
void wlan_request_time(void);    /* ask the module for local time (send cmd 0x1C) */

/* Network time/date + weather received over UART (for the clock + weather display). */
extern volatile uint8_t  g_net_time_valid;       /* 1 once a valid cmd 0x1C arrived */
extern volatile uint8_t  g_weather_valid;        /* 1 once weather arrived          */
extern volatile uint8_t  g_weather_raw[32];      /* raw weather payload (for display) */
extern volatile uint16_t g_weather_len;

#endif /* APP_WLAN_H */
