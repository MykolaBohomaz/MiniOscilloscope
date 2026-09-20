/**
 * @file    acq_engine.c
 */
#include "acq_engine.h"
#include <string.h>

/* Samples that may still land after the sample clock is stopped (a conversion
 * already in flight). Added to the overwrite check for safety. */
#define ACQ_STOP_SLACK 4u

static inline int32_t sdiff(uint32_t a, uint32_t b)
{
    return (int32_t)(a - b);
}

void acq_init(acq_engine_t *e, const uint16_t *buf, uint32_t buf_len,
              uint32_t guard, const acq_hw_ops_t *hw)
{
    memset(e, 0, sizeof *e);
    e->buf = buf;
    e->buf_len = buf_len;
    e->guard = guard;
    e->hw = *hw;
    e->max_search_chunk = 4096u;
    e->mode = TRIG_MODE_AUTO;
    e->trig.level = 2048u;
    e->trig.hyst = 40u;
    e->trig.edge = TRIG_EDGE_RISING;
    e->state = ACQ_STOPPED;
    acq_set_record(e, buf_len / 2u, 50u);
    e->auto_timeout = buf_len;
}

void acq_set_record(acq_engine_t *e, uint32_t record_len, uint32_t pretrig_pct)
{
    const uint32_t max_len = e->buf_len - e->guard;
    if (record_len > max_len) {
        record_len = max_len;
    }
    if (record_len < 2u) {
        record_len = 2u;
    }
    if (pretrig_pct > 100u) {
        pretrig_pct = 100u;
    }
    e->record_len = record_len;
    e->pre_len = (uint32_t)(((uint64_t)record_len * pretrig_pct) / 100u);
    if (e->pre_len >= record_len) {
        e->pre_len = record_len - 1u;   /* keep at least the trigger sample */
    }
}

void acq_arm(acq_engine_t *e)
{
    e->hw.set_sampling(e->hw.ctx, true);
    e->arm_abs = e->hw.write_pos(e->hw.ctx);
    e->search_abs = e->arm_abs;
    e->forced = false;
    e->trig_frac = 0.0f;
    trig_reset(&e->ts);
    e->state = ACQ_ARMED;
}

void acq_stop(acq_engine_t *e)
{
    e->hw.set_sampling(e->hw.ctx, false);
    e->state = ACQ_STOPPED;
}

static void enter_posttrig(acq_engine_t *e, uint32_t t, bool forced)
{
    e->trig_abs = t;
    e->forced = forced;
    if (forced) {
        e->stats.auto_triggers++;
        e->trig_frac = 0.0f;
    } else {
        e->stats.triggers++;
        e->trig_frac = trig_interpolate(e->buf, e->buf_len, t, e->trig.level);
    }
    e->state = ACQ_POSTTRIG;
}

static void poll_armed(acq_engine_t *e)
{
    const uint32_t w = e->hw.write_pos(e->hw.ctx);
    uint32_t lo = e->search_abs;

    /* A trigger needs pre_len samples of history acquired after arming. */
    const uint32_t min_lo = e->arm_abs + e->pre_len;
    if (sdiff(lo, min_lo) < 0) {
        lo = min_lo;
    }

    /* Overwrite protection: a trigger at t is only usable if, when sampling
     * stops (~max(w, t+post) + latency), the record start t-pre is still in
     * the buffer. For a lagging search that means t >= w + guard + pre - len. */
    const uint32_t max_lag = e->buf_len - e->guard - e->pre_len;
    if (sdiff(w, lo) > (int32_t)max_lag) {
        lo = w - max_lag;
        trig_reset(&e->ts);
        e->stats.search_skips++;
    }

    if (sdiff(w, lo) > 0) {
        uint32_t hi = w;
        if ((w - lo) > e->max_search_chunk) {
            hi = lo + e->max_search_chunk;   /* bounded work per poll */
        }
        uint32_t hit;
        if (trig_search(e->buf, e->buf_len, lo, hi, &e->trig, &e->ts, &hit)) {
            enter_posttrig(e, hit, false);
            return;
        }
        lo = hi;
    }
    e->search_abs = lo;

    if (e->mode == TRIG_MODE_AUTO &&
        sdiff(w, e->arm_abs) >= (int32_t)(e->pre_len + e->auto_timeout)) {
        enter_posttrig(e, w, true);
    }
}

static bool poll_posttrig(acq_engine_t *e)
{
    const uint32_t post_len = e->record_len - e->pre_len;
    const uint32_t w = e->hw.write_pos(e->hw.ctx);
    if (sdiff(w, e->trig_abs) < (int32_t)post_len) {
        return false;
    }

    e->hw.set_sampling(e->hw.ctx, false);
    const uint32_t w_stop = e->hw.write_pos(e->hw.ctx);
    e->stop_abs = w_stop;

    const uint32_t overshoot = w_stop - (e->trig_abs + post_len);
    if (overshoot > e->stats.max_stop_overshoot) {
        e->stats.max_stop_overshoot = overshoot;
    }

    const uint32_t span = w_stop - acq_record_start(e) + ACQ_STOP_SLACK;
    if (span > e->buf_len) {
        /* The writer lapped the start of the record: never show it. */
        e->stats.records_discarded++;
        acq_arm(e);
        return false;
    }
    e->stats.records++;
    e->state = ACQ_READY;
    return true;
}

bool acq_poll(acq_engine_t *e)
{
    switch (e->state) {
    case ACQ_ARMED:
        poll_armed(e);
        if (e->state != ACQ_POSTTRIG) {
            return false;
        }
        /* Fall through: the post-trigger may already be complete. */
        return poll_posttrig(e);
    case ACQ_POSTTRIG:
        return poll_posttrig(e);
    case ACQ_STOPPED:
    case ACQ_READY:
    default:
        return false;
    }
}

void acq_release(acq_engine_t *e)
{
    if (e->state != ACQ_READY) {
        return;
    }
    if (e->mode == TRIG_MODE_SINGLE) {
        e->state = ACQ_STOPPED;        /* HOLD: sampling already stopped */
    } else {
        acq_arm(e);
    }
}

const char *acq_state_name(acq_state_t s)
{
    switch (s) {
    case ACQ_STOPPED:  return "STOP";
    case ACQ_ARMED:    return "ARM";
    case ACQ_POSTTRIG: return "TRIG";
    case ACQ_READY:    return "RDY";
    default:           return "?";
    }
}
