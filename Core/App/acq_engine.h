/**
 * @file    acq_engine.h
 * @brief   Hardware-independent acquisition / trigger state machine.
 *
 * The hardware path (TIM6 -> ADC1 -> DMA1 Ch1, circular) runs continuously and
 * never needs the CPU per sample. This engine only *observes* the DMA write
 * position, searches newly written samples for a trigger, and stops the sample
 * clock once enough post-trigger samples exist. The record is then frozen in
 * the circular buffer and handed to the consumer (measurements, display, PC).
 *
 *                 arm()            trigger found /           post-trigger
 *    STOPPED ------------> ARMED ----- AUTO timeout -----> POSTTRIG ------> READY
 *       ^                    ^                                                |
 *       |                    +---------------- release() (AUTO/NORMAL) -------+
 *       +-------------------------------------- release() (SINGLE => hold) ---+
 *
 * Record integrity rule: the record [t - pre, t + post) must never have been
 * overwritten by the time sampling stops. The engine enforces it twice:
 *   - it never accepts a trigger so old that the writer would lap the record's
 *     first sample before post-trigger completes (search is skipped forward and
 *     the skip is counted), and
 *   - after stopping, it re-checks the final write position and discards the
 *     record if the stop latency ate into the guard band.
 *
 * The hardware is reached only through acq_hw_ops_t, so the engine runs
 * unchanged in host unit tests against a simulated DMA writer.
 */
#ifndef ACQ_ENGINE_H
#define ACQ_ENGINE_H

#include <stdbool.h>
#include <stdint.h>
#include "trigger.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ACQ_STOPPED = 0,   /**< sample clock off, nothing pending (RUN off / HOLD) */
    ACQ_ARMED,         /**< sampling, searching for a trigger                  */
    ACQ_POSTTRIG,      /**< trigger found, collecting post-trigger samples     */
    ACQ_READY,         /**< record frozen, waiting for the consumer            */
} acq_state_t;

typedef enum {
    TRIG_MODE_AUTO = 0,   /**< free-run after a timeout without trigger */
    TRIG_MODE_NORMAL,     /**< only update on a valid trigger           */
    TRIG_MODE_SINGLE,     /**< one record, then hold                    */
    TRIG_MODE_COUNT
} trig_mode_t;

typedef struct {
    /** Absolute number of samples written by the DMA so far (monotonic). */
    uint32_t (*write_pos)(void *ctx);
    /** Start (true) or stop (false) the sample clock. */
    void     (*set_sampling)(void *ctx, bool run);
    void     *ctx;
} acq_hw_ops_t;

typedef struct {
    uint32_t records;            /**< records delivered                        */
    uint32_t triggers;           /**< real edge triggers                       */
    uint32_t auto_triggers;      /**< forced by AUTO timeout                   */
    uint32_t search_skips;       /**< search fell behind the writer            */
    uint32_t records_discarded;  /**< stop latency overran the guard band      */
    uint32_t max_stop_overshoot; /**< worst samples written past post-trigger  */
} acq_stats_t;

typedef struct {
    /* buffer + hardware */
    const uint16_t *buf;
    uint32_t        buf_len;
    acq_hw_ops_t    hw;

    /* configuration */
    uint32_t    record_len;
    uint32_t    pre_len;          /**< samples before the trigger point        */
    uint32_t    guard;            /**< samples reserved for stop latency       */
    uint32_t    auto_timeout;     /**< AUTO: samples to wait after arming      */
    uint32_t    max_search_chunk; /**< cap on samples scanned per poll         */
    trig_cfg_t  trig;
    trig_mode_t mode;

    /* runtime */
    acq_state_t  state;
    uint32_t     arm_abs;
    uint32_t     search_abs;
    uint32_t     trig_abs;
    uint32_t     stop_abs;
    float        trig_frac;       /**< sub-sample trigger offset, (-1, 0]      */
    bool         forced;          /**< record came from AUTO timeout           */
    trig_state_t ts;
    acq_stats_t  stats;
} acq_engine_t;

void acq_init(acq_engine_t *e, const uint16_t *buf, uint32_t buf_len,
              uint32_t guard, const acq_hw_ops_t *hw);

/** Set record length and pre-trigger share (0..100 % of the record). */
void acq_set_record(acq_engine_t *e, uint32_t record_len, uint32_t pretrig_pct);

/** Start sampling and search for a new trigger. */
void acq_arm(acq_engine_t *e);

/** Stop sampling immediately and drop any partial record. */
void acq_stop(acq_engine_t *e);

/**
 * Advance the state machine. Call from the main loop as often as possible.
 * @return true exactly once per record, on the transition to ACQ_READY.
 */
bool acq_poll(acq_engine_t *e);

/** Consumer is done with the READY record: re-arm, or hold in SINGLE mode. */
void acq_release(acq_engine_t *e);

/** Absolute position of the first sample of the frozen record. */
static inline uint32_t acq_record_start(const acq_engine_t *e)
{
    return e->trig_abs - e->pre_len;
}

const char *acq_state_name(acq_state_t s);

#ifdef __cplusplus
}
#endif
#endif /* ACQ_ENGINE_H */
