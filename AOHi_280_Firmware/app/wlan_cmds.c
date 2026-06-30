/* wlan_cmds.c - concrete WLAN command handlers (clean-room from stock wlan_cmd_*).
 *
 * These override the weak handlers in wlan.c with faithful bodies. Response
 * frames are built by wlan_send() (same 0x55 0xAA framing + checksum8 the stock
 * wlan_cmd_hello/ack produce). TLV responses mirror wlan_pack_tlv's 14-byte slot
 * table used to assemble info/status payloads. */
#include "wlan.h"
#include "uart.h"
#include "charge.h"
#include "power.h"          /* g_channel_mask */
#include "ui.h"            /* ui_set_page, ui_18CE4, g_status_mode */
#include <stdint.h>

extern int g_wlan_suspend;         /* byte_2000F3DF (fw_main.c) */
/* g_status_mode in ui_state.h */
void ui_18CE4(uint8_t idx, uint8_t anim);

/* Handshake state byte (stock byte_1FFFA5B2[0]): 0 until first hello answered. */
static uint8_t s_hello_state;

/* Per-port ON/OFF state shown in the app (DP24..29). Defaults all-ON to match the SW3556
 * autonomous-sourcing reality (ports power up enabled at full current with g_channel_mask=0). */
uint8_t g_port_enable[WLAN_NUM_PORTS] = { 1u, 1u, 1u, 1u, 1u, 1u };
/* per-port max-watt (Szenario/Individual mode), stock flt_1FFF809C[4*port]. Set by the power DPs
 * (7,0xA,0xD,0x10,0x13,0x16 = 7+3*port); applied in update_outputs when mode==Individual. */
float g_port_maxw[WLAN_NUM_PORTS];

void wlan_on_hello(uint8_t seq, const uint8_t *p, uint16_t n)
{
    (void)p; (void)n;
    wlan_send(WLAN_CMD_HELLO, seq, &s_hello_state, 1);   /* [..00 00 00 01 state] */
    if (!s_hello_state) s_hello_state = 1;
}

void wlan_on_ack(uint8_t seq, const uint8_t *p, uint16_t n)
{
    (void)p; (void)n;
    wlan_send(WLAN_CMD_ACK, seq, 0, 0);                  /* [..02 00 00 00] */
}

/* --- Network time (cmd 0x1C) + weather (cmd 0x21/0x22) over UART --------------
 * These come FROM the module after it is cloud-connected and are normally shown on
 * the clock/home (user-reported). My dispatch previously mis-routed 0x1C->FW and
 * 0x21/0x22->IMG stubs (wrong command constants) so they were silently dropped.
 * cmd 0x1C payload (Tuya local-time): [valid, year-2000, month, day, hour, min,
 * sec, weekday]. On valid, set the RTC so the existing clock display shows it. */
#include "rtc.h"
volatile uint8_t  g_net_time_valid;
volatile uint8_t  g_weather_valid;
volatile uint8_t  g_weather_raw[32];
volatile uint16_t g_weather_len;

/* Unix timestamp (seconds) -> broken-down date/time. Stock gmtime_from_unix
 * (@0x18e4c) uses the same civil-from-days math (epoch 1970, constants 365/146097).
 * The cloud sends LOCAL time as the timestamp (Tuya "local time"), so no TZ shift -
 * stock converts the value directly. */
static void unix_to_datetime(uint32_t ts, rtc_time_t *t)
{
    uint32_t rem  = ts % 86400u;
    t->hour = (uint8_t)(rem / 3600u);
    t->min  = (uint8_t)((rem % 3600u) / 60u);
    t->sec  = (uint8_t)(rem % 60u);
    uint32_t z   = ts / 86400u + 719468u;       /* days since 0000-03-01 */
    uint32_t era = z / 146097u;
    uint32_t doe = z - era * 146097u;           /* [0,146096] */
    uint32_t yoe = (doe - doe/1460u + doe/36524u - doe/146096u) / 365u;
    uint32_t y   = yoe + era * 400u;
    uint32_t doy = doe - (365u*yoe + yoe/4u - yoe/100u);
    uint32_t mp  = (5u*doy + 2u) / 153u;
    uint32_t d   = doy - (153u*mp + 2u)/5u + 1u;
    uint32_t m   = mp < 10u ? mp + 3u : mp - 9u;
    if (m <= 2u) y += 1u;
    t->year  = (uint8_t)(y - 2000u);            /* RTC stores 2-digit year */
    t->month = (uint8_t)m;
    t->day   = (uint8_t)d;
    /* weekday 0=Sun..6=Sat (stock byte_1FFFC619+3): 1970-01-01 was a Thursday(=4),
     * so weekday = (days_since_epoch + 4) mod 7. Drawn by ui clock page (sub_14db8). */
    t->weekday = (uint8_t)(((ts / 86400u) + 4u) % 7u);
}

/* Mark the link as cloud-connected. Stock learns this from the module's cmd 0x03
 * status report (state>=5), but the module only re-reports status on CHANGE - a
 * freshly-booted MCU joining an already-connected module never sees it (verified:
 * 6 min = only heartbeats + product queries, no cmd 0x03). So we ALSO treat the
 * arrival of real cloud data (time/weather) as proof of connection: set g_wlan_conn
 * and redraw the WLAN icon. */
extern void ui_draw_menu(void);
void wlan_mark_connected(void)
{
    if (!g_wlan_conn) { g_wlan_conn = 1; ui_draw_menu(); }
}

/* cmd 0x1C (stock RX time): the module sends a 4-byte BIG-ENDIAN Unix timestamp.
 * Stock read_be32 (@0x12f58) reads it BE, validates > ~Dec 2024 (0x67741501), then
 * gmtime (@0x18e4c) -> RTC. My old handler assumed binary Tuya fields (p[0]==1) and
 * rejected all frames -> RTC stuck at the 12:00 power-on default. The sanity check
 * makes a wrong offset safe (it just won't sync rather than set a bad date). */
void wlan_on_time(uint8_t seq, const uint8_t *p, uint16_t n)
{
    if (n >= 4) {
        uint32_t ts = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
                      ((uint32_t)p[2] << 8)  |  (uint32_t)p[3];
        if (ts > 0x67741501u) {                 /* sanity bound (stock 0x15d6a) */
            rtc_time_t t;
            unix_to_datetime(ts, &t);
            rtc_set(&t);
            g_net_time_valid = 1;
            wlan_mark_connected();              /* got cloud time -> definitely online */
        }
    }
    wlan_send(WLAN_CMD_TIME_SYNC, seq, 0, 0);        /* ACK so the module keeps sync'ing */
}

void wlan_on_weather(uint8_t cmd, uint8_t seq, const uint8_t *p, uint16_t n)
{
    uint16_t k = (n > sizeof(g_weather_raw)) ? (uint16_t)sizeof(g_weather_raw) : n;
    for (uint16_t i = 0; i < k; i++) g_weather_raw[i] = p[i];
    g_weather_len = k;
    g_weather_valid = 1;
    wlan_mark_connected();                      /* got cloud weather -> definitely online */
    wlan_send(cmd, seq, 0, 0);                  /* ACK (echo the same cmd) */
}

/* Ask the module for the current local time (Tuya: MCU sends cmd 0x1C empty, the
 * module replies with the 8-byte time -> wlan_on_time). Call once connected. */
void wlan_request_time(void)
{
    wlan_send(WLAN_CMD_TIME_SYNC, 0, 0, 0);
}

/* cmd 0x03 (stock wlan_cmd_03 @0xC488): the module reports its network status in
 * the payload. status == 5 => WiFi connected to cloud (stock sets unk_1FFF818E=1,
 * else 0). The MCU must ACK with an empty cmd 0x03 frame or the module keeps
 * retrying / never advances to delivering data (weather/location/time). Without
 * this handler cmd 0x03 fell through to the no-op default -> handshake stalled
 * after the heartbeat -> nothing else ever arrived (user: WLAN one-directional). */
extern uint8_t g_wlan_conn;        /* unk_1FFF818E (ui.c) */
extern void ui_draw_menu(void);    /* sub_11FA4: WLAN signal icon (pages 0-4) */
void wlan_on_03(uint8_t seq, const uint8_t *p, uint16_t n)
{
    uint8_t was = g_wlan_conn;
    /* stock wlan_cmd_03 @0xC488: connected = state >= 5 (was == 5, which missed any
     * higher state), THEN immediately redraw the WLAN icon (stock 0xc4da: bl 0x11fa4)
     * so the home/menu icon flips to "connected" the moment the module reports it. */
    if (n >= 1) g_wlan_conn = (p[0] >= 5) ? 1u : 0u;
    ui_draw_menu();                                      /* redraw icon now (stock 0xc4da) */
    wlan_send(WLAN_CMD_03, seq, 0, 0);                   /* empty ACK (cmd 0x03) */
    if (!was && g_wlan_conn) wlan_request_time();        /* just connected -> get time */
}

/* cmd 0x09 (stock wlan_cmd_09 0x1977D): 1-byte payload 0x02. */
void wlan_on_09(uint8_t seq, const uint8_t *p, uint16_t n)
{
    (void)p; (void)n;
    uint8_t d = 0x02;
    wlan_send(WLAN_CMD_09, seq, &d, 1);                  /* 55 AA 00 seq 09 00 00 01 02 ck */
}

/* cmd 0x14 (stock wlan_cmd_14 @0x15904): the module REPLIES to our periodic cmd
 * 0x14 request with the location+weather string "#location#temp#code", e.g.
 * "#Himmelpforten#24#1003" (location / outdoor temperature / weather code). Stock
 * splits on '#' (delim @0x1F904, strchr 0x83b8 / strlen 0x8360 / memcpy 0x8348)
 * and stores the fields. Parsed here into globals for the clock-page display. */
/* g_loc_name/g_loc_temp/g_loc_icon relocated to stock RAM (ui_state.h macros:
 * 0x815B / 0x815A / 0x818D). g_loc_code + g_loc_valid stay as regular globals. */
volatile int16_t g_loc_code;         /* weather code (WeatherAPI condition code) */
volatile uint8_t g_loc_valid;

/* weather code -> icon index, 1:1 from stock 0x15904 tbh table (code-1000, 0..136):
 * 1000->0, 1003/1006->1, 1009->2, 1063->3, 1087->7, 1066/1114/1117->8, else none.
 * Icon glyph = g_img_table[icon+730] (32x28 @ g_img_pos = (166,446)). */
static uint8_t weather_code_to_icon(int16_t code)
{
    switch (code) {
    case 1000:                       return 0;
    case 1003: case 1006:            return 1;
    case 1009:                       return 2;
    case 1063:                       return 3;
    case 1087:                       return 7;
    case 1066: case 1114: case 1117: return 8;
    default:                         return 0xFF;
    }
}

void wlan_on_set_tz(uint8_t seq, const uint8_t *p, uint16_t n)
{
    (void)seq;
    if (n < 2 || p[0] != '#') return;
    uint16_t li = 0, fld = 1; int32_t num = 0; uint8_t have = 0, neg = 0;
    g_loc_name[0] = 0;     /* leading '#' already consumed -> first field (1) = location */
    for (uint16_t i = 1; i <= n; i++) {
        uint8_t c = (i < n) ? p[i] : (uint8_t)'#';   /* '#' terminates the last field */
        if (c == '#') {
            if (fld == 1)      g_loc_name[li] = 0;
            else if (fld == 2 && have) g_loc_temp = (int16_t)(neg ? -num : num);
            else if (fld == 3 && have) { g_loc_code = (int16_t)(neg ? -num : num);
                                         g_loc_icon = weather_code_to_icon(g_loc_code); }
            fld++; li = 0; num = 0; have = 0; neg = 0;
            if (fld > 3) break;
        } else if (fld == 1) {
            if (li < G_LOC_NAME_SZ - 1u) g_loc_name[li++] = (char)c;
        } else if (c == '-') neg = 1;
        else if (c >= '0' && c <= '9') { num = num * 10 + (c - '0'); have = 1; }
    }
    g_loc_valid = 1;
    wlan_mark_connected();   /* cmd 0x14 = cloud weather/location -> module is online */
}

/* cmd 0x0F (stock wlan_cmd_0F 0x197DD): empty ack. */
void wlan_on_0F(uint8_t seq, const uint8_t *p, uint16_t n)
{
    (void)p; (void)n;
    wlan_send(WLAN_CMD_0F, seq, 0, 0);                   /* 55 AA 00 seq 0F 00 00 00 ck */
}

/* ---- TLV response assembly (stock wlan_pack_tlv: 29 slots x 14 bytes) ---- */
#define TLV_SLOTS   29
#define TLV_DATAMAX 8
typedef struct { uint8_t id, flag, type; uint8_t used; uint16_t size; uint8_t data[TLV_DATAMAX]; } tlv_t;
static tlv_t s_tlv[TLV_SLOTS];

wlan_telemetry_t g_telemetry;

static uint8_t cks8(const uint8_t *b, uint16_t n) { uint8_t s=0; while(n--) s+=*b++; return s; }

int wlan_tlv_set(uint8_t id, uint8_t type, uint8_t size, const void *data)
{
    int slot = -1;
    for (int i = 0; i < TLV_SLOTS; i++)
        if (s_tlv[i].used && s_tlv[i].id == id) { slot = i; break; }
    if (slot < 0)
        for (int i = 0; i < TLV_SLOTS; i++) if (!s_tlv[i].used) { slot = i; break; }
    if (slot < 0) return -1;
    if (size > TLV_DATAMAX) size = TLV_DATAMAX;
    s_tlv[slot].id = id; s_tlv[slot].flag = 0; s_tlv[slot].type = type;
    s_tlv[slot].size = size; s_tlv[slot].used = 1;
    const uint8_t *s = (const uint8_t *)data;
    for (uint8_t i = 0; i < size; i++) s_tlv[slot].data[i] = s[i];
    return slot;
}

/* Serialise all set TLVs into a payload and send as `resp_cmd`. Per-TLV wire
 * format matches stock: [id][flag][type][sizeHi][sizeLo][data..]. */
void wlan_tlv_send(uint8_t resp_cmd, uint8_t seq)
{
    uint8_t frame[8 + 256 + 1];
    uint16_t n = 8;
    for (int i = 0; i < TLV_SLOTS; i++) {
        if (!s_tlv[i].used) continue;
        if (n + 5 + s_tlv[i].size > (int)sizeof(frame) - 1) break;
        /* This module's DP unit on the wire = [id][flag][type][len_hi][len_lo][value]
         * (5-byte header). Verified from the cmd 0x06 parser (wlan_on_set_config):
         * it reads id=p[+0], len=p[+3,+4], value=p[+5] -> flag@+1, type@+2. (Removing
         * the flag broke it; it must be present, value 0.) */
        frame[n++] = s_tlv[i].id;
        frame[n++] = s_tlv[i].flag;     /* +1: flag (0) */
        frame[n++] = s_tlv[i].type;     /* +2: DP type */
        frame[n++] = (uint8_t)(s_tlv[i].size >> 8);
        frame[n++] = (uint8_t)s_tlv[i].size;
        for (uint16_t k = 0; k < s_tlv[i].size; k++) frame[n++] = s_tlv[i].data[k];
        s_tlv[i].used = 0;          /* consumed */
    }
    uint16_t payload = n - 8;
    frame[0]=0x55; frame[1]=0xAA; frame[2]=0; frame[3]=seq; frame[4]=resp_cmd;
    frame[5]=0; frame[6]=(uint8_t)(payload>>8); frame[7]=(uint8_t)payload;
    frame[n] = cks8(frame, n);
    uart_send_buf(frame, n + 1);
}

static void put_u32(uint8_t *b, uint32_t v)   /* big-endian, like the protocol */
{ b[0]=(uint8_t)(v>>24); b[1]=(uint8_t)(v>>16); b[2]=(uint8_t)(v>>8); b[3]=(uint8_t)v; }

/* cmd 0x08 -> cmd 0x07 DP status report. The module EXPECTS this response: without
 * it the module retries 0x01/0x02/0x08 endlessly (verified live: removing it -> 10x
 * retries; with it -> 1x each, handshake satisfied). The DP id/format may not be a
 * perfect match to the product schema yet, but the response IS required. */
/* Build + send the Tuya DP status report (cmd 0x07). Stock 0xbb80 reports DPs 1..24:
 *   DP 1 (bool)  = on/off  = NOT standby (stock !byte_2000F3DD)
 *   DP 2 (enum)  = mode    = status_mode mapped 0->1,1->2,2->3 (stock byte_1FFF8159)
 *   DP 5.. (int) = per-port V/I/P (*100, 4-byte BE); DP 0x18.. = per-port attached flags
 * Stock sends this BOTH on a cmd 0x08 query AND proactively from ui_124F0 - the proactive
 * path is what the app relies on to ever see on/off + mode (the module rarely queries). */
void wlan_dp_report(uint8_t seq)
{
    extern int ui_is_standby(void);
    uint8_t onoff = ui_is_standby() ? 0u : 1u;             /* DP1 on/off (charging on) */
    wlan_tlv_set(0x01, 1, 1, &onoff);
    uint8_t mode = (g_status_mode <= 2u) ? (uint8_t)(g_status_mode + 1u) : 0u;
    wlan_tlv_set(0x02, 4, 1, &mode);                       /* DP2 mode (enum) */

    /* DP4 device temperature (type 2 value, 4-byte BE). ROOT CAUSE of "app 32 vs menu 27":
     * stock wlan_cmd_get_status (0xbb80) sends DP4 = fn_18CBC((uint)max(flt_1FFF8304,8308)) =
     * the RAW max NTC temp (~32), while the menu draws g_temp_display = the cal-curve-CORRECTED
     * value (0x187e8, ~27). So stock's app and menu genuinely DISAGREE. The user wants them to
     * MATCH, so we intentionally deviate from stock and report the corrected g_temp_display - the
     * exact value the charger's own temp screen shows. */
    extern uint32_t g_temp_display;
    uint8_t tb[4]; put_u32(tb, g_temp_display);
    wlan_tlv_set(0x04, 2, 4, tb);                          /* DP4 temperature (deg C) */

    /* CORRECTED DP layout (1:1 with stock wlan_cmd_get_status 0xbb80) - the app maps fixed DP
     * IDs to V/I/P fields, so the ORDER matters. Stock: DP3=total W, DP5/6/7=C1(240W) V/I/P,
     * then 3 DPs per port: DP8/9/10=C2, DP11/12/13=C3, DP14/15/16=C4, DP17-19=USB-A1, DP20-22=
     * USB-A2. My old code started per-port at DP5 -> everything shifted one port (voltage landed
     * in the watt slot, no ampere, positions swapped - exactly the user's report). */
    uint8_t buf[4];
    extern float g_power_w;
    /* DP3 = total apparent power. Stock fn_18C8C(word_1FFF829C[0]) packs the RAW integer watt
     * sum (NOT x100 - that's only the per-port fn_18CBC fields). Sending x100 made the app show
     * 300 W for 3 W. */
    put_u32(buf, (uint32_t)(g_power_w + 0.5f));     wlan_tlv_set(0x03, 2, 4, buf);
    /* DP5/6/7 = C1 (240W controller) V/I/P from g_total_power (chip 0x16 reg 0x21, masked to
     * 16 bits = e.g. 5005). Stock C1 display: V=g_total_power/1000, I mirrors it, P=V^2 capped.
     * Same source the page1 C1 slot now renders. */
    { extern volatile uint32_t g_total_power;
      uint32_t gtp = g_total_power;
      float vf = (float)(gtp & 0xFFFFu) / 1000.0f;             /* LOWORD mV  -> V */
      float af = (float)(gtp >> 16)     / 1000.0f;             /* HIWORD mA  -> A */
      float pf = vf * af; if (pf > 500.0f) pf = 0.0f;          /* P = V*I (stock >500 -> 0) */
      put_u32(buf, (uint32_t)(vf * 100.0f + 0.5f)); wlan_tlv_set(0x05, 2, 4, buf);   /* V x100 */
      put_u32(buf, (uint32_t)(af * 100.0f + 0.5f)); wlan_tlv_set(0x06, 2, 4, buf);   /* I x100 */
      put_u32(buf, (uint32_t)(pf * 100.0f + 0.5f)); wlan_tlv_set(0x07, 2, 4, buf); } /* P x100 */
    /* DP8.. = the 5 real ports C2/C3/C4/USB-A1/USB-A2 = g_telemetry[0..4], 3 consecutive DPs each. */
    uint8_t id = 0x08;
    for (int port = 0; port < 5; port++) {
        put_u32(buf, g_telemetry.v_x100[port]); wlan_tlv_set(id++, 2, 4, buf);
        put_u32(buf, g_telemetry.i_x100[port]); wlan_tlv_set(id++, 2, 4, buf);
        put_u32(buf, g_telemetry.p_x100[port]); wlan_tlv_set(id++, 2, 4, buf);
    }
    /* DP24..29 = per-port ON/OFF switch state. Report g_port_enable (defaults all-ON, matching the
     * SW3556 autonomous-on reality) - NOT the channel-mask bit (which is 0 by default and made the
     * app show every port OFF). */
    { extern uint8_t g_port_enable[];
      for (int port = 0; port < WLAN_NUM_PORTS; port++) {
          uint8_t f = g_port_enable[port];
          wlan_tlv_set((uint8_t)(0x18 + port), 1, 1, &f);
      } }
    wlan_tlv_send(0x07, seq);
}

void wlan_on_status(uint8_t seq, const uint8_t *p, uint16_t n)
{
    (void)p; (void)n;
    wlan_dp_report(seq);     /* cmd 0x08 query -> report all DPs */
}

/* cmd 1: device info string from internal flash (stock wlan_cmd_get_info). */
extern const uint8_t g_device_info[];
extern const uint16_t g_device_info_len;
void wlan_on_info(uint8_t seq, const uint8_t *p, uint16_t n)
{
    (void)p; (void)n;
    wlan_send(WLAN_CMD_INFO, seq, g_device_info, g_device_info_len);
}

/* Notify the companion module of a local button click (stock wlan_send_click_cmd). */
void wlan_send_click(void)
{
    wlan_send(0x04, 0, 0, 0);          /* 55 AA 00 00 04 00 00 00 cksum */
}

/* WLAN reset / re-pair request (stock wlan_send_reset_cmd: cmd 0x0D). */
void wlan_send_reset(void)
{
    wlan_send(WLAN_CMD_0D, 0, 0, 0);   /* 55 AA 00 00 0D 00 00 00 cksum */
}

/* Poll the companion module for status (stock wlan_cmd_get_status: cmd 0x08). */
void wlan_send_get_status(void)
{
    wlan_send(WLAN_CMD_STATUS, 0, 0, 0);
}

/* update_outputs (stock 0x166a4): consolidate the per-port enables into the channel-OFF bitmask
 * and gate the C1 charger. Port->bit map (from the RAM-diff + stock update_outputs):
 *   g_port_enable[0]=C1 -> charge_set_enable (i2c_DAB0); [1..5]=C2,C3,C4,A1,A2 -> mask bit (i-1).
 * mask bit SET = that port OFF (sync_channel_state writes reg 0xA0=1 / 0x78 disable); all-on = 0. */
/* (Removed dead smart_distribute(): reg 0xA0 / per-port limits are owned exclusively
 * by pd_maintain_limits() now - see the note in update_outputs below.) */

void update_outputs(void)
{
    extern uint8_t g_channel_mask;
    extern int charge_set_enable(uint8_t on);
    uint8_t mask = 0;
    for (int i = 1; i <= 5; i++)
        if (!g_port_enable[i]) mask |= (uint8_t)(1u << (i - 1));
    g_channel_mask = mask;
    charge_set_enable(g_port_enable[0] ? 1u : 0u);

    /* C1 (240W) power limit on the 0x16 chip. TEST: only write reg 3 when the app EXPLICITLY caps C1
     * (Individual mode). Stock's boot capture showed it never writes reg 3 in steady state (only
     * reg1=1); writing reg3=240000 at boot may mis-limit the shared bus (units?) and prevent C2-C4
     * from sustaining a 20V contract (-> PD error recovery). Leave reg 3 at the 0x16 power-on default
     * otherwise. */
    /* C1 (240W) power limit on the 0x16 reg 3. MATCH STOCK EXACTLY (update_outputs 0x166a4): stock
     * writes reg 3 (fn_133C8) ONLY in Individual mode (byte_1FFF8159==2) and ONLY when the app set a
     * C1 cap. In Smart/Raserei stock NEVER writes reg 3 - it just enables the master (i2c_DAB0) and
     * the 0x16 negotiates 20V AUTONOMOUSLY at its full power-on default (= full 240W, which is what
     * Raserei wants). Forcing reg3=240000 in Smart/Raserei disrupted the 0x16's own PD negotiation
     * mid-attach -> C1 came up at 5V / oscillated. So: Individual -> explicit cap; otherwise leave
     * reg 3 untouched. Skipped while thermally throttled (g_hipwr2). */
    extern void pd_apply_c1_limit(uint32_t mW);
    extern uint8_t g_hipwr2;
    if (g_port_enable[0] && !g_hipwr2 &&
        g_status_mode == 2u && g_port_maxw[0] > 0.0f && g_port_maxw[0] < 250.0f)
        pd_apply_c1_limit((uint32_t)(g_port_maxw[0] * 1000.0f));   /* Individual only, as stock */

    /* reg 0xA0 (per-C-port current limit) is owned EXCLUSIVELY by pd_maintain_limits() now. The old
     * code ALSO wrote reg 0xA0 here (pd_set_port_flag / smart_distribute) every Smart re-eval (~2 s) -
     * each extra bit-bang reg-0xA0 write GLITCHED the SW3566 port and made the device DETACH (att
     * reset to 0) -> the 0/5/20 V oscillation. Stock writes reg 0xA0 only on a real mode/enable
     * CHANGE (edge), never on a timer. The maintainer's per-mode value logic mirrors stock, so this
     * function no longer touches reg 0xA0 at all - it only sets the channel mask, master enable, and
     * the C1 limit. */
}

/* WLAN cmd 0x06 SET_CONFIG (stock wlan_cmd_set_config 0xB348). Parses TLV records
 * (wire: [id, type, b2, len(2 BE), value...] per stock buf_17BE0). Handles the
 * charge on/off item (id 1): enable/disable charging + reset the channel mask.
 * Per-port limit/mode items (id 2,5+) TODO. Replies ACK. */
void wlan_on_set_config(uint8_t seq, const uint8_t *p, uint16_t n)
{
    uint16_t off = 0;
    while ((uint32_t)off + 5u <= n) {
        uint8_t  id  = p[off];
        uint16_t len = (uint16_t)((p[off + 3] << 8) | p[off + 4]);   /* big-endian */
        if ((uint32_t)off + 5u + len > n) break;
        const uint8_t *val = &p[off + 5];
        if (id == 1 && len >= 1) {                  /* charge master on/off (stock case 1) */
            /* stock: on -> byte_8413=0 (all channels on) + charger on; off -> standby + all off.
             * Force all per-port enables to the master state, then update_outputs() rebuilds the
             * mask (on=0 autonomous, off=0x1F all-killed) and gates the C1 charger. */
            uint8_t on = val[0] ? 1u : 0u;
            for (int k = 0; k < WLAN_NUM_PORTS; k++) g_port_enable[k] = on;
            update_outputs();
            ui_set_charge(on);                      /* display/standby flag (cmd 0x07 report) */
            g_telemetry.charge_enable = on;
            wlan_dp_report(0);                      /* echo the new state back immediately */
        } else if (id == 2 && len >= 1) {           /* display mode (stock case 2) */
            switch (val[0]) {
            case 0:  g_wlan_suspend = 1; break;     /* suspend display updates */
            case 1:  mode_select(0); break;         /* Raserei  (fire anim -> home) */
            case 2:  mode_select(1); break;         /* Smart    (page 18  -> home)  */
            case 3:  mode_select(2); break;         /* Individual (page 19 -> home) */
            default: break;
            }
            wlan_dp_report(0);                      /* echo the active mode (DP2) back */
        } else if (id >= 5 && id <= 0x16 && len >= 4) {  /* per-port V/I/power limits (cases 5..0x16) */
            uint32_t raw = ((uint32_t)val[0] << 24) | ((uint32_t)val[1] << 16) |
                           ((uint32_t)val[2] << 8)  |  (uint32_t)val[3];   /* bswap32(BE) */
            float v = (float)raw / 100.0f;
            if ((uint32_t)(id - 5) < 12u) g_port_limit[id - 5] = v;       /* keep V/I params */
            /* power-limit DPs (7,0xA,0xD,0x10,0x13,0x16 = 7+3*port) = per-port max-watt */
            if (id >= 7 && ((id - 7) % 3) == 0) {
                uint8_t port = (uint8_t)((id - 7) / 3);
                if (port < WLAN_NUM_PORTS) g_port_maxw[port] = v;
            }
            update_outputs();                    /* re-apply (Individual mode) */
        } else if (id >= 0x18 && id <= 0x1D && len >= 1) {  /* per-port on/off (DP24..29) */
            /* Per-port on/off (stock DP 0x18..0x1D -> flt_1FFF809C[1/5/9/13/17/21] -> update_outputs).
             * RAM-diff confirmed: stock applies these to HW via byte_1FFF8413 + sync_channel_state.
             * The earlier "display only" workaround was a POLARITY bug - the mask bit means port OFF,
             * so all-on = mask 0 (boot-safe, autonomous sourcing); only an OFF port writes reg 0xA0=1. */
            uint8_t port = (uint8_t)(id - 0x18);
            if (port < WLAN_NUM_PORTS) {
                uint8_t on = val[0] ? 1u : 0u;
                g_port_enable[port] = on;
                /* A1 (port 4) and A2 (port 5) are ONE physical USB-A output with a single
                 * combined switch in the app (it sends only the A1 DP 0x1C). Keep both in
                 * sync so disabling "A" turns BOTH off (and the debug page shows both). */
                if (port == 4u || port == 5u) g_port_enable[4] = g_port_enable[5] = on;
            }
            update_outputs();                    /* rebuild g_channel_mask + gate C1 charger */
            wlan_dp_report(0);                   /* echo the new per-port state to cloud */
        }
        off = (uint16_t)(off + 5u + len);
    }
    wlan_send(WLAN_CMD_ACK, seq, 0, 0);
}
