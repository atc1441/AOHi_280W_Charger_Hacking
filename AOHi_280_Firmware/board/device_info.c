/* device_info.c - device identity string returned by WLAN cmd 1 (get_info).
 * Extracted from stock internal flash @0x1F909 ("001+BWX468+1.1.6"): a
 * NUL-terminated model+version string read with strlen by wlan_cmd_get_info. */
#include <stdint.h>

const uint8_t  g_device_info[]  = "001+BWX468+1.1.6";
const uint16_t g_device_info_len = sizeof("001+BWX468+1.1.6") - 1;   /* 16, no NUL */
