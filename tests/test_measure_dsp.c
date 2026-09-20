/* Measurements, decimation, calibration, timebase table. */
#include <string.h>
#include "unit.h"
#include "calib.h"
#include "decimate.h"
#include "measure.h"
#include "scope_config.h"
#include "timebase.h"

#define N ACQ_BUF_LEN
static uint16_t buf[N];

static void fill_sine(double period, double amp, double mid, uint32_t start, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) {
        double v = mid + amp * sin(6.283185307179586 * (double)i / period);
        buf[(start + i) % N] = (uint16_t)lround(v);
    }
}

static void test_sine(void)
{
    /* 1 kHz at 1 MS/s -> 1000 samples/period, 10 periods, starting mid-buffer
     * so the record wraps around the end of the ring. */
    const uint32_t start = N - 3000;
    fill_sine(1000.0, 1500.0, 2048.0, start, 10000);
    meas_t m;
    meas_compute(buf, N, start, 10000, &m);
    CHECK_EQ_U(m.min, 548);
    CHECK_EQ_U(m.max, 3548);
    CHECK_NEAR(m.mean, 2048.0, 0.5);
    CHECK_NEAR(m.ac_rms, 1500.0 / sqrt(2.0), 1.0);
    CHECK(m.periodic);
    CHECK_NEAR(m.period, 1000.0, 0.05);       /* interpolated edges */
    CHECK_NEAR(m.duty, 0.5, 0.01);
    CHECK(!m.clip_low && !m.clip_high);
}

static void test_non_integer_period(void)
{
    fill_sine(333.3, 1000.0, 2048.0, 0, 10000);
    meas_t m;
    meas_compute(buf, N, 0, 10000, &m);
    CHECK(m.periodic);
    CHECK_NEAR(m.period, 333.3, 0.005);     /* needs sub-sample edge interpolation */
}

static void test_square_duty_and_clip(void)
{
    for (uint32_t i = 0; i < 8000; i++) buf[i] = ((i % 400) < 100) ? 4095 : 0;
    meas_t m;
    meas_compute(buf, N, 0, 8000, &m);
    CHECK(m.periodic);
    CHECK_NEAR(m.period, 400.0, 0.01);
    CHECK_NEAR(m.duty, 0.25, 0.005);
    CHECK(m.clip_low && m.clip_high);
}

static void test_dc_and_noise_not_periodic(void)
{
    for (uint32_t i = 0; i < 5000; i++) buf[i] = (uint16_t)(1234 + (i % 3));
    meas_t m;
    meas_compute(buf, N, 0, 5000, &m);
    CHECK(!m.periodic);
    CHECK_NEAR(m.mean, 1235.0, 0.01);
    CHECK_EQ_U(m.max - m.min, 2);
}

static void test_decimate_keeps_spike(void)
{
    for (uint32_t i = 0; i < 15000; i++) buf[i] = 100;
    buf[7777] = 4000;                         /* one-sample glitch */
    uint16_t lo[LCD_WIDTH], hi[LCD_WIDTH];
    decimate_minmax(buf, N, 0, 15000, lo, hi, LCD_WIDTH);
    int spikes = 0;
    for (uint32_t c = 0; c < LCD_WIDTH; c++) {
        CHECK_EQ_U(lo[c], 100);
        if (hi[c] == 4000) spikes++;
    }
    CHECK_EQ_U(spikes, 1);
}

static void test_decimate_partition_and_small_n(void)
{
    for (uint32_t i = 0; i < 1000; i++) buf[i] = (uint16_t)i;
    uint16_t lo[LCD_WIDTH], hi[LCD_WIDTH];
    decimate_minmax(buf, N, 0, 960, lo, hi, LCD_WIDTH);   /* 5 per column */
    for (uint32_t c = 0; c < LCD_WIDTH; c++) {
        CHECK_EQ_U(lo[c], 5 * c);
        CHECK_EQ_U(hi[c], 5 * c + 4);
    }
    decimate_minmax(buf, N, 0, 100, lo, hi, LCD_WIDTH);   /* fewer samples than columns */
    CHECK_EQ_U(lo[0], 0);
    CHECK_EQ_U(hi[LCD_WIDTH - 1], 99);
    for (uint32_t c = 1; c < LCD_WIDTH; c++) CHECK(lo[c] >= lo[c - 1]);
}

static void test_calib(void)
{
    /* VREFINT_CAL ~ 1.212 V at 3.0 V -> 1655 codes; same reading at 3.3 V -> 1504 */
    CHECK_NEAR(calib_vdda_mv(1655, 1655), 3000.0, 0.01);
    CHECK_NEAR(calib_vdda_mv(1504, 1655), 3301.2, 0.5);
    const range_cal_t *d = &g_ranges[RANGE_DIRECT].nominal;
    CHECK_NEAR(calib_raw_to_mv(4095, 3300.0f, d), 3300.0, 0.01);
    CHECK_EQ_U(calib_mv_to_raw(1650.0f, 3300.0f, d), 2048);
    /* +-30 V range: 0 V input sits near mid-scale, round trip within 1 LSB */
    const range_cal_t *r30 = &g_ranges[RANGE_30V].nominal;
    const uint16_t z = calib_mv_to_raw(0.0f, 3300.0f, r30);
    CHECK(z > 1900 && z < 2000);
    CHECK_NEAR(calib_raw_to_mv(calib_mv_to_raw(12000.0f, 3300.0f, r30), 3300.0f, r30), 12000.0, 30.0);
    CHECK_EQ_U(calib_mv_to_raw(1e9f, 3300.0f, d), 4095);
    CHECK_EQ_U(calib_mv_to_raw(-1e9f, 3300.0f, d), 0);
}

static void test_timebase_table(void)
{
    CHECK(g_timebase_count >= 10);
    CHECK(TIMEBASE_DEFAULT_INDEX < g_timebase_count);
    for (uint32_t i = 0; i < g_timebase_count; i++) {
        const timebase_t *tb = &g_timebases[i];
        CHECK(tb_is_valid(tb));
        CHECK(tb_sample_rate_hz(tb) <= 5.334e6f);
        if (i) CHECK(tb->ns_per_div > g_timebases[i - 1].ns_per_div);
        /* on-screen window matches the label within one sample */
        const double window_ns = (double)tb->record_len * tb_period_cycles(tb) * 1e9 / SCOPE_TIMER_CLK_HZ;
        CHECK_NEAR(window_ns, 8.0 * tb->ns_per_div, 1e9 * tb_period_cycles(tb) / SCOPE_TIMER_CLK_HZ);
    }
    /* a physically impossible entry is rejected */
    const timebase_t bad = { 1000, 0, 9, SMP_2C5, 1000 };          /* 8 MS/s */
    CHECK(!tb_is_valid(&bad));
    const timebase_t bad2 = { 1000, 0, 19, SMP_12C5, 1000 };       /* 12.5+12.5 > 20 */
    CHECK(!tb_is_valid(&bad2));
    char s[16];
    tb_format_div(s, sizeof s, 5000);        CHECK_STR(s, "5us");
    tb_format_div(s, sizeof s, 200000000);   CHECK_STR(s, "200ms");
    tb_format_div(s, sizeof s, 1000000000);  CHECK_STR(s, "1s");
}

int main(void)
{
    RUN(test_sine);
    RUN(test_non_integer_period);
    RUN(test_square_duty_and_clip);
    RUN(test_dc_and_noise_not_periodic);
    RUN(test_decimate_keeps_spike);
    RUN(test_decimate_partition_and_small_n);
    RUN(test_calib);
    RUN(test_timebase_table);
    UNIT_REPORT();
}
