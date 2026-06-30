/* temp.c - device temperature via the MCU's own ADC1 (two external NTC sensors).
 *
 * Byte-exact replica of the stock chain reverse-engineered in IDA:
 *   adc1_read_temp(ch)  @0x1579c : start ADC1 (STR), poll EOC, read DR, convert
 *   adc1_read_dr(ch)    @0x8c4c  : *(u16*)(0x40040050 + ch*2)  (DR0=+0x50, DR1=+0x52)
 *   ntc_convert(raw)    @0x13028 : NTC beta equation -> degrees C
 *   temp_correct(T)     @0x187e8 : subtract a 110-entry calibration curve
 *   <caller>            @0xbc00  : displayed = (u32)( max(t0,t1) - cal_interp(max) )
 *
 * NTC: beta = 3950, R0 = 10k @ 25 C (T0 = 298.15 K), Vref = 3.3 V, 12-bit ADC.
 * The conversion needs ADC1 to actually run a conversion - our gpio_8CA0 set the
 * registers up identically to stock but never triggered STR, so DR0/DR1 read 0
 * (stock had 1676/1729). Writing STR=1 each cycle produces the live sensor values. */
#include "temp.h"

#define ADC1     0x40040000u
#define A8(o)    (*(volatile uint8_t  *)(ADC1 + (o)))
#define H16(o)   (*(volatile uint16_t *)(ADC1 + (o)))

float    g_temp_sensor[2];
uint32_t g_temp_display;

/* Natural log (double), self-contained - no libm in this build. atanh series on
 * the [1,2) mantissa; t=(x-1)/(x+1) <= 1/3 so t^15/15 is < 1e-8 -> ~1e-9 accurate. */
static double t_ln(double x)
{
    if (x <= 0.0) return -1.0e30;
    int e = 0;
    while (x >= 2.0) { x *= 0.5; e++; }
    while (x <  1.0) { x *= 2.0; e--; }
    double t  = (x - 1.0) / (x + 1.0);
    double t2 = t * t;
    double term = t, s = t;
    for (int k = 3; k <= 15; k += 2) { term *= t2; s += term / (double)k; }
    return (double)e * 0.6931471805599453 + 2.0 * s;
}

/* stock ntc_convert @0x13028 */
static float ntc_convert(uint16_t raw)
{
    if (raw >= 4095) return -100.0f;             /* open/short -> sentinel */
    float v = (float)raw * 0.0008056640509f;     /* raw * 3.3 / 4096 = voltage */
    float d = 3.3f - v;
    if (d < 0.001f) d = 0.001f;
    float r = (v * 10.0f) / d;
    if (r <= 0.0f) return -100.0f;
    double ratio = (double)(r / 10.0f);          /* = v/d = R_ntc / R0 */
    double T = 1.0 / (0.003354016435 + t_ln(ratio) / 3950.0);   /* beta equation */
    return (float)(T - 273.15);                  /* Kelvin -> Celsius */
}

/* Read one NTC channel (ch 0 or 1): trigger a conversion, wait for EOC, convert. */
static float adc1_read_temp(int ch)
{
    if (A8(0x00) != 1u) A8(0x00) = 1u;           /* STR = start (stock 0x8d4c) */
    int ok = 0;
    for (int i = 0; i < 1000; i++) {
        if (A8(0x46) & 1u) { A8(0x46) = 2u; ok = 1; break; }  /* EOC -> clear (0x8c14/0x8b78) */
    }
    if (!ok) { A8(0x00) = 0u; return 0.0f; }     /* timeout -> stop (0x8d78) */
    return ntc_convert(H16(0x50 + ch * 2));      /* DR0=+0x50, DR1=+0x52 (0x8c4c) */
}

/* stock temp_correct @0x187e8 : per-degree calibration curve (110 floats @0x1F748).
 * displayed = T - interp(cal, T). Brings the hot-side NTC reading to case temp. */
static const float s_cal[110] = {
    0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
    0.2f, 0.71f, 1.2f, 1.98f, 2.65f,
    3.25f, 3.7f, 4.3f, 4.6f, 5.15f,
    5.54f, 5.9f, 6.3f, 6.7f, 7.02f,
    7.36f, 7.65f, 8.15f, 8.4f, 8.74f,
    9.0f, 9.35f, 9.5f, 10.1f, 10.6f,
    10.7f, 11.14f, 11.58f, 11.8f, 12.14f,
    12.5f, 12.7f, 13.3f, 13.5f, 14.1f,
    14.25f, 14.6f, 14.9f, 15.15f, 15.5f,
    16.0f, 16.4f, 16.95f, 17.09f, 17.5f,
    17.9f, 18.5f, 18.7f, 18.96f, 19.45f,
    19.65f, 20.0f, 20.2f, 20.5f, 20.9f,
    21.4f, 22.0f, 22.5f, 23.18f, 23.32f,
    23.61f, 23.96f, 24.18f, 24.95f, 25.12f,
    25.44f, 26.0f, 26.27f, 27.15f, 27.5f,
    27.61f, 27.9f, 28.38f, 28.6f, 28.8f,
    29.05f, 29.3f, 29.7f, 30.1f, 30.55f,
    30.98f, 31.3f, 31.75f, 32.1f, 32.54f,
};

static float temp_correct(float T)
{
    if (T < 0.0f)    return T;
    if (T >= 110.0f) return T - 32.88f;
    int   idx  = (int)T;
    float frac = T - (float)idx;
    float nxt  = (idx <= 108) ? s_cal[idx + 1] : s_cal[idx];
    float interp = s_cal[idx] + frac * (nxt - s_cal[idx]);
    return T - interp;
}

void temp_update(void)
{
    g_temp_sensor[1] = adc1_read_temp(1);    /* ch1 -> stock flt_1FFF8304 */
    g_temp_sensor[0] = adc1_read_temp(0);    /* ch0 -> stock flt_1FFF8308 */
    float mx = (g_temp_sensor[1] > g_temp_sensor[0]) ? g_temp_sensor[1]
                                                     : g_temp_sensor[0];
    float disp = temp_correct(mx);
    g_temp_display = (disp < 0.0f) ? 0u : (uint32_t)disp;
    /* stock temp_update (0x10d4a) stores the two sensor temps as floats in the UI
     * struct at +0x70/+0x74; mirror them so device RAM matches stock byte-for-byte
     * (the temp bar / case-3 dual bars read these). */
    *(volatile float *)0x1FFF8304u = g_temp_sensor[1];   /* UI struct +0x70 (ch1) */
    *(volatile float *)0x1FFF8308u = g_temp_sensor[0];   /* UI struct +0x74 (ch0) */
}
