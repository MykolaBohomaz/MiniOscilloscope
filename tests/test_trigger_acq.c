/*
 * Trigger search and the acquisition state machine, driven by a simulated
 * DMA writer that fills a circular buffer from a signal generator function.
 */
#include <string.h>
#include "unit.h"
#include "acq_engine.h"
#include "scope_config.h"
#include "trigger.h"

#define N ACQ_BUF_LEN
static uint16_t buf[N];

/* ---------------------------------------------------------------- trigger */

static void test_rising_falling(void)
{
    /* ramp 0..4095 then back down */
    for (uint32_t i = 0; i < 4096; i++) buf[i] = (uint16_t)i;
    for (uint32_t i = 0; i < 4096; i++) buf[4096 + i] = (uint16_t)(4095 - i);
    trig_cfg_t c = { .level = 1000, .hyst = 50, .edge = TRIG_EDGE_RISING };
    trig_state_t st;
    uint32_t hit = 0;
    trig_reset(&st);
    CHECK(trig_search(buf, N, 0, 8192, &c, &st, &hit));
    CHECK_EQ_U(hit, 1000);

    c.edge = TRIG_EDGE_FALLING;
    trig_reset(&st);
    CHECK(trig_search(buf, N, 0, 8192, &c, &st, &hit));
    CHECK_EQ_U(hit, 4096 + 3095);          /* first sample <= 1000 going down */
}

static void test_hysteresis_rejects_noise(void)
{
    /* +-20 code noise around the level must never trigger with hyst = 40 */
    for (uint32_t i = 0; i < N; i++) buf[i] = (uint16_t)(2048 + ((i * 7919u) % 41u) - 20);
    trig_cfg_t c = { .level = 2048, .hyst = 40, .edge = TRIG_EDGE_RISING };
    trig_state_t st;
    uint32_t hit;
    trig_reset(&st);
    CHECK(!trig_search(buf, N, 0, N, &c, &st, &hit));
    /* with no hysteresis the same noise triggers */
    c.hyst = 0;
    trig_reset(&st);
    CHECK(trig_search(buf, N, 0, N, &c, &st, &hit));
}

static void test_wrap_and_split_search(void)
{
    /* low at the end of the buffer, high at the start: edge across the wrap */
    for (uint32_t i = 0; i < N; i++) buf[i] = (i < 100) ? 3000 : 500;
    trig_cfg_t c = { .level = 2000, .hyst = 100, .edge = TRIG_EDGE_RISING };
    trig_state_t st;
    uint32_t hit;
    trig_reset(&st);
    /* absolute positions N-50 .. N+50 map to indices N-50..N-1, 0..49 */
    CHECK(trig_search(buf, N, N - 50, N + 50, &c, &st, &hit));
    CHECK_EQ_U(hit, N);
    /* split into two calls: arming state must carry over */
    trig_reset(&st);
    CHECK(!trig_search(buf, N, N - 50, N - 1, &c, &st, &hit));
    CHECK(st.armed);
    CHECK(trig_search(buf, N, N - 1, N + 10, &c, &st, &hit));
    CHECK_EQ_U(hit, N);
}

static void test_interpolation(void)
{
    buf[10] = 1000; buf[11] = 2000;
    CHECK_NEAR(trig_interpolate(buf, N, 11, 1500), -0.5, 1e-6);
    CHECK_NEAR(trig_interpolate(buf, N, 11, 2000), 0.0, 1e-6);
}

/* --------------------------------------------------------- engine + sim */

typedef struct {
    uint32_t pos;          /* absolute write position */
    bool     running;
    uint32_t period;       /* square/sine period in samples, 0 = DC */
    uint16_t dc;
    uint32_t start_calls, stop_calls;
    uint32_t stop_latency; /* samples written after a stop request */
} sim_t;

static uint16_t sig(const sim_t *s, uint32_t abs)
{
    if (s->period == 0) return s->dc;
    const double ph = (double)(abs % s->period) / (double)s->period;
    return (uint16_t)(2048.0 + 1500.0 * sin(6.283185307179586 * ph));
}

static void sim_advance(sim_t *s, uint32_t n)
{
    if (!s->running) return;
    for (uint32_t i = 0; i < n; i++) {
        buf[s->pos % N] = sig(s, s->pos);
        s->pos++;
    }
}

static uint32_t sim_write_pos(void *ctx) { return ((sim_t *)ctx)->pos; }

static void sim_set_sampling(void *ctx, bool run)
{
    sim_t *s = ctx;
    if (!run && s->running) {
        s->stop_calls++;
        sim_advance(s, s->stop_latency);    /* conversions still in flight */
    }
    if (run && !s->running) s->start_calls++;
    s->running = run;
}

static void engine_setup(acq_engine_t *e, sim_t *s, uint32_t start_pos)
{
    memset(s, 0, sizeof *s);
    s->pos = start_pos;
    const acq_hw_ops_t ops = { sim_write_pos, sim_set_sampling, s };
    acq_init(e, buf, N, ACQ_GUARD_SAMPLES, &ops);
}

/* Run until READY or until max_samples have been produced. */
static bool run_until_ready(acq_engine_t *e, sim_t *s, uint32_t step, uint32_t max_samples)
{
    for (uint32_t produced = 0; produced < max_samples; produced += step) {
        sim_advance(s, step);
        if (acq_poll(e)) return true;
    }
    return false;
}

static void check_record_trigger(const acq_engine_t *e)
{
    const uint32_t t = e->trig_abs;
    const uint16_t before = buf[(t - 1) % N], at = buf[t % N];
    if (e->trig.edge == TRIG_EDGE_RISING) {
        CHECK(before < e->trig.level);
        CHECK(at >= e->trig.level);
    }
    /* whole record must be intact: every sample equals the generator */
    const uint32_t s0 = acq_record_start(e);
    uint32_t bad = 0;
    for (uint32_t i = 0; i < e->record_len; i++) {
        const uint32_t a = s0 + i;
        const double ph = (double)(a % 1000) / 1000.0;
        const uint16_t want = (uint16_t)(2048.0 + 1500.0 * sin(6.283185307179586 * ph));
        if (buf[a % N] != want) bad++;
    }
    CHECK_EQ_U(bad, 0);
}

static void test_normal_trigger_record(void)
{
    acq_engine_t e; sim_t s;
    engine_setup(&e, &s, 0);
    s.period = 1000;
    e.mode = TRIG_MODE_NORMAL;
    e.trig.level = 2048; e.trig.hyst = 50; e.trig.edge = TRIG_EDGE_RISING;
    acq_set_record(&e, 4000, 25);
    acq_arm(&e);
    CHECK(s.running);
    CHECK(run_until_ready(&e, &s, 64, 20000));
    CHECK(e.state == ACQ_READY);
    CHECK(!s.running);
    CHECK_EQ_U(e.pre_len, 1000);
    CHECK(!e.forced);
    CHECK_EQ_U(e.stats.triggers, 1);
    CHECK(e.trig_abs - e.arm_abs >= e.pre_len);   /* pre-trigger fully acquired */
    check_record_trigger(&e);
    /* stable phase: trigger lands on the sine's upward zero crossing */
    CHECK_EQ_U(e.trig_abs % 1000, 0);
}

static void test_auto_fires_on_dc_normal_does_not(void)
{
    acq_engine_t e; sim_t s;
    engine_setup(&e, &s, 0);
    s.dc = 1000;
    e.mode = TRIG_MODE_AUTO;
    e.auto_timeout = 5000;
    acq_set_record(&e, 2000, 50);
    acq_arm(&e);
    CHECK(run_until_ready(&e, &s, 100, 20000));
    CHECK(e.forced);
    CHECK_EQ_U(e.stats.auto_triggers, 1);

    engine_setup(&e, &s, 0);
    s.dc = 1000;
    e.mode = TRIG_MODE_NORMAL;
    acq_set_record(&e, 2000, 50);
    acq_arm(&e);
    CHECK(!run_until_ready(&e, &s, 100, 200000));
    CHECK(e.state == ACQ_ARMED);
}

static void test_lagging_search_skips_but_stays_valid(void)
{
    acq_engine_t e; sim_t s;
    engine_setup(&e, &s, 0);
    s.period = 1000;
    e.mode = TRIG_MODE_NORMAL;
    e.trig.level = 2048; e.trig.hyst = 50;
    acq_set_record(&e, 8000, 50);
    acq_arm(&e);
    /* The main loop "stalls" for 3 full buffer laps between polls. */
    sim_advance(&s, 3 * N);
    bool ready = acq_poll(&e);
    for (int i = 0; i < 200 && !ready; i++) {
        sim_advance(&s, 100);
        ready = acq_poll(&e);
    }
    CHECK(ready);
    CHECK(e.stats.search_skips >= 1);
    check_record_trigger(&e);
}

static void test_stop_latency_overrun_discards(void)
{
    acq_engine_t e; sim_t s;
    engine_setup(&e, &s, 0);
    s.period = 1000;
    s.stop_latency = ACQ_GUARD_SAMPLES + 10;  /* stop arrives far too late */
    e.mode = TRIG_MODE_NORMAL;
    e.trig.level = 2048; e.trig.hyst = 50;
    acq_set_record(&e, ACQ_RECORD_MAX, 50);
    acq_arm(&e);
    for (int i = 0; i < 100; i++) {
        sim_advance(&s, 256);
        CHECK(!acq_poll(&e));
    }
    CHECK(e.stats.records_discarded >= 1);
    CHECK_EQ_U(e.stats.records, 0);

    /* small latency inside the guard band is accepted and reported */
    engine_setup(&e, &s, 0);
    s.period = 1000;
    s.stop_latency = 3;
    e.mode = TRIG_MODE_NORMAL;
    e.trig.level = 2048; e.trig.hyst = 50;
    acq_set_record(&e, ACQ_RECORD_MAX, 50);
    acq_arm(&e);
    CHECK(run_until_ready(&e, &s, 1, 100000));
    CHECK_EQ_U(e.stats.max_stop_overshoot, 3);
    check_record_trigger(&e);
}

static void test_abs_counter_wraparound(void)
{
    acq_engine_t e; sim_t s;
    /* start just below 2^32 but aligned to the signal period */
    const uint32_t start = 4294962000u;       /* 2^32 - 5296, multiple of 1000 */
    engine_setup(&e, &s, start);
    s.period = 1000;
    e.mode = TRIG_MODE_NORMAL;
    e.trig.level = 2048; e.trig.hyst = 50;
    acq_set_record(&e, 8000, 50);
    acq_arm(&e);
    CHECK(run_until_ready(&e, &s, 50, 30000));
    CHECK(e.stop_abs < start);          /* the counter wrapped during capture */
    CHECK_EQ_U(e.trig_abs % 1000, 0);
}

static void test_single_holds(void)
{
    acq_engine_t e; sim_t s;
    engine_setup(&e, &s, 0);
    s.period = 1000;
    e.mode = TRIG_MODE_SINGLE;
    e.trig.level = 2048; e.trig.hyst = 50;
    acq_set_record(&e, 2000, 50);
    acq_arm(&e);
    CHECK(run_until_ready(&e, &s, 64, 20000));
    acq_release(&e);
    CHECK(e.state == ACQ_STOPPED);
    CHECK(!s.running);
    CHECK(!run_until_ready(&e, &s, 64, 20000));

    /* AUTO/NORMAL re-arm and keep producing records */
    engine_setup(&e, &s, 0);
    s.period = 1000;
    e.mode = TRIG_MODE_NORMAL;
    e.trig.level = 2048; e.trig.hyst = 50;
    acq_set_record(&e, 2000, 50);
    acq_arm(&e);
    for (int k = 0; k < 10; k++) {
        CHECK(run_until_ready(&e, &s, 64, 20000));
        check_record_trigger(&e);
        acq_release(&e);
    }
    CHECK_EQ_U(e.stats.records, 10);
}

static void test_set_record_clamps(void)
{
    acq_engine_t e; sim_t s;
    engine_setup(&e, &s, 0);
    acq_set_record(&e, 1u << 20, 150);
    CHECK_EQ_U(e.record_len, N - ACQ_GUARD_SAMPLES);
    CHECK(e.pre_len < e.record_len);
}

int main(void)
{
    RUN(test_rising_falling);
    RUN(test_hysteresis_rejects_noise);
    RUN(test_wrap_and_split_search);
    RUN(test_interpolation);
    RUN(test_normal_trigger_record);
    RUN(test_auto_fires_on_dc_normal_does_not);
    RUN(test_lagging_search_skips_but_stays_valid);
    RUN(test_stop_latency_overrun_discards);
    RUN(test_abs_counter_wraparound);
    RUN(test_single_holds);
    RUN(test_set_record_clamps);
    UNIT_REPORT();
}
