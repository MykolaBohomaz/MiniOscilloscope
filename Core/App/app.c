/**
 * @file    app.c
 * @brief   Cooperative, event-driven superloop (no RTOS).
 *
 * Every pass services independent state machines in a fixed order; none of
 * them blocks. Time-critical work (sampling, SPI and UART transfers) happens
 * in hardware/DMA, ISRs only publish flags (see hal_callbacks.c).
 *
 *   1. acquisition   acq_poll(): trigger search, stop, record hand-off
 *   2. PC link RX    parse command frames
 *   3. UI input      debounced buttons, encoder
 *   4. PC link TX    capture streaming, start next TX DMA
 *   5. display       render into back buffer, flush with SPI DMA
 *   6. housekeeping  watchdog, heartbeat LED, loop-time statistics
 */
#include "app.h"

#include "acq_engine.h"
#include "acq_hw.h"
#include "adc.h"
#include "board.h"
#include "calib.h"
#include "decimate.h"
#include "input.h"
#include "iwdg.h"
#include "lcd_st75256.h"
#include "link_uart.h"
#include "measure.h"
#include "protocol.h"
#include "scope_config.h"
#include "scope_settings.h"
#include "settings_store.h"
#include "siggen.h"
#include "tim.h"
#include "timebase.h"
#include "ui_menu.h"
#include "ui_render.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------------ */
/* State                                                                     */
/* ------------------------------------------------------------------------ */

static acq_engine_t     s_acq;
static scope_settings_t s_set;
static range_cal_t      s_cal[RANGE_COUNT];
static proto_parser_t   s_parser;
static float            s_vdda_mv;

/* last processed record */
static meas_t   s_meas;
static bool     s_meas_valid;
static bool     s_last_forced;
static bool     s_frozen_valid;      /* ADC buffer still holds that record */
static uint16_t s_col_min[LCD_WIDTH];
static uint16_t s_col_max[LCD_WIDTH];
static uint16_t s_trig_col;

/* Metadata of the last processed record, frozen at hand-off so a later
 * settings change cannot alter what a capture of that record reports. */
typedef struct {
    uint32_t    start_abs;
    uint32_t    n;
    uint32_t    pre_len;
    float       trig_frac;
    bool        forced;
    uint16_t    trig_level;
    trig_edge_t edge;
    trig_mode_t mode;
    uint8_t     tb_index;
} record_meta_t;
static record_meta_t s_rec;

/* display */
static uint8_t  s_fb[2][LCD_FB_BYTES];
static uint8_t  s_back;               /* index of the buffer we draw into */
static bool     s_back_ready;
static bool     s_dirty = true;
static uint32_t s_last_render_ms;
static menu_item_t s_sel = MENU_TIMEBASE;
static trig_mode_t s_mode_before_single = TRIG_MODE_AUTO;

/* PC link */
static uint32_t s_last_rx_ms = 0xFFFF0000u;
static uint8_t  s_unsolicited_seq;

typedef struct {
    bool     pending;       /* requested, waiting for the next record */
    bool     active;        /* streaming chunks of the frozen record   */
    uint8_t  seq;
    uint16_t id;
    uint32_t start_abs;
    uint32_t n;
    uint32_t sent;
} capture_tx_t;
static capture_tx_t s_cap;

/* diagnostics */
static uint32_t s_loop_max_cycles;
static uint32_t s_loop_count;
static uint32_t s_boot_faults;
static bool     s_fault_mode;        /* boot self-check failed: link only */

enum {
    FAULT_ADC_DMA_NOT_CIRCULAR = 1u << 0,
    FAULT_ADC_CLOCK_ASYNC      = 1u << 1,
    FAULT_SPI_NOT_8BIT         = 1u << 2,
    FAULT_SPI_NO_DMA           = 1u << 3,
    FAULT_SYSCLK               = 1u << 4,
    FAULT_MSI_PLL_OFF          = 1u << 5,
    FAULT_ADC_INIT             = 1u << 6,
    FAULT_HW_CRC               = 1u << 7,
    FAULT_LINKER_LAYOUT        = 1u << 8,
};

/* ------------------------------------------------------------------------ */
/* Helpers                                                                   */
/* ------------------------------------------------------------------------ */

static const timebase_t *cur_tb(void)
{
    return &g_timebases[s_set.tb_index];
}

static const range_cal_t *cur_cal(void)
{
    return &s_cal[s_set.range];
}

static void log_text(const char *msg)
{
    const size_t n = strlen(msg);
    (void)link_send(MSG_LOG, s_unsolicited_seq++, msg, (uint16_t)(n > 200u ? 200u : n));
}

/**
 * Regression guard for CubeMX regenerations: verify the settings the firmware
 * depends on (handoff sections 5 and 10). Cheap, runs once at boot.
 */
static uint32_t board_selfcheck(void)
{
    extern DMA_HandleTypeDef hdma_adc1;
    extern SPI_HandleTypeDef hspi1;
    uint32_t f = 0u;
    if (hdma_adc1.Init.Mode != DMA_CIRCULAR)                   { f |= FAULT_ADC_DMA_NOT_CIRCULAR; }
    if (hadc1.Init.ClockPrescaler != ADC_CLOCK_SYNC_PCLK_DIV1) { f |= FAULT_ADC_CLOCK_ASYNC; }
    if (hspi1.Init.DataSize != SPI_DATASIZE_8BIT)              { f |= FAULT_SPI_NOT_8BIT; }
    if (hspi1.hdmatx == NULL)                                  { f |= FAULT_SPI_NO_DMA; }
    if (SystemCoreClock != SCOPE_TIMER_CLK_HZ)                 { f |= FAULT_SYSCLK; }
    if ((RCC->CR & RCC_CR_MSIPLLEN) == 0u)                     { f |= FAULT_MSI_PLL_OFF; }

    /* Linker script edits (64 KB RAM, settings page reserved) are not stored in
     * the .ioc; detect a regenerated/default script. */
    extern uint32_t _estack, _sidata, _sdata, _edata;
    const uintptr_t image_end = (uintptr_t)&_sidata + ((uintptr_t)&_edata - (uintptr_t)&_sdata);
    if ((uintptr_t)&_estack != 0x20010000u || image_end > SETTINGS_FLASH_ADDR) {
        f |= FAULT_LINKER_LAYOUT;
    }
    return f;
}

static void cycle_counter_init(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0u;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

/* ------------------------------------------------------------------------ */
/* Acquisition control                                                       */
/* ------------------------------------------------------------------------ */

static void engine_apply_trigger(void)
{
    s_acq.trig.level = s_set.trig_level;
    s_acq.trig.hyst = s_set.trig_hyst;
    s_acq.trig.edge = s_set.edge;
    s_acq.mode = s_set.mode;
    acq_set_record(&s_acq, cur_tb()->record_len, s_set.pretrig_pct);

    /* AUTO timeout: 100 ms of samples (at least 2 records) */
    const uint32_t fs = SCOPE_TIMER_CLK_HZ / tb_period_cycles(cur_tb());
    uint32_t t = fs / 10u;
    if (t < 2u * s_acq.record_len) {
        t = 2u * s_acq.record_len;
    }
    s_acq.auto_timeout = t;
}

static void capture_abort(void)
{
    s_cap.pending = false;
    s_cap.active = false;
}

static void acquisition_start(void)
{
    s_frozen_valid = false;
    acq_arm(&s_acq);
}

static void acquisition_stop(void)
{
    if (s_acq.state != ACQ_READY && s_acq.state != ACQ_STOPPED) {
        acq_stop(&s_acq);
    } else if (s_acq.state == ACQ_READY) {
        s_acq.state = ACQ_STOPPED;       /* keep the frozen record */
    }
}

/** Handoff 8.3: full stop -> reconfigure -> restart. */
static void acquisition_reconfigure(void)
{
    capture_abort();
    acquisition_stop();
    s_frozen_valid = false;
    s_meas_valid = false;
    if (!acq_hw_configure(cur_tb())) {
        log_text("timebase configure failed");
    }
    engine_apply_trigger();
    if (s_set.running || s_set.mode == TRIG_MODE_SINGLE) {
        acquisition_start();
    }
}

static void apply_changes(uint32_t chg)
{
    if (chg & CHG_TIMEBASE) {
        acquisition_reconfigure();
    } else if (chg & (CHG_TRIGGER | CHG_RUN)) {
        engine_apply_trigger();
        const bool want_run = s_set.running || s_set.mode == TRIG_MODE_SINGLE;
        if (!want_run) {
            capture_abort();
            acquisition_stop();
        } else if (chg & CHG_RUN) {
            if (!s_cap.active) {
                acquisition_start();
            }
        } else if (s_acq.state == ACQ_ARMED || s_acq.state == ACQ_POSTTRIG ||
                   (s_acq.state == ACQ_READY && !s_cap.active)) {
            acquisition_start();       /* restart the search with new settings */
        }
    }
    if (chg & CHG_GEN) {
        siggen_apply(s_set.gen_wave, s_set.gen_freq, s_vdda_mv);
    }
    if (chg & CHG_BACKLIGHT) {
        __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_4,
                              (BACKLIGHT_PWM_MAX + 1u) * s_set.backlight / 10u);
    }
    s_dirty = true;
}

/* ------------------------------------------------------------------------ */
/* Record processing                                                         */
/* ------------------------------------------------------------------------ */

static void send_capture_header(void);

static void on_record_ready(void)
{
    DBG1_HIGH();
    const uint32_t start = acq_record_start(&s_acq);
    const uint32_t n = s_acq.record_len;

    meas_compute(g_adc_buf, ACQ_BUF_LEN, start, n, &s_meas);
    decimate_minmax(g_adc_buf, ACQ_BUF_LEN, start, n, s_col_min, s_col_max, LCD_WIDTH);
    s_trig_col = (uint16_t)(((uint64_t)s_acq.pre_len * LCD_WIDTH) / n);
    s_meas_valid = true;
    s_last_forced = s_acq.forced;
    s_frozen_valid = true;
    s_rec = (record_meta_t){
        .start_abs = start, .n = n, .pre_len = s_acq.pre_len,
        .trig_frac = s_acq.trig_frac, .forced = s_acq.forced,
        .trig_level = s_acq.trig.level, .edge = s_acq.trig.edge,
        .mode = s_acq.mode, .tb_index = s_set.tb_index,
    };
    s_dirty = true;
    DBG1_LOW();

    if (s_cap.pending) {
        s_cap.pending = false;
        s_cap.active = true;
        s_cap.start_abs = s_rec.start_abs;
        s_cap.n = s_rec.n;
        s_cap.sent = 0u;
        s_cap.id++;
        send_capture_header();
    }
    if (!s_cap.active) {
        acq_release(&s_acq);
        if (s_acq.state != ACQ_STOPPED) {
            s_frozen_valid = false;      /* sampling restarted */
        }
    }
}

/* ------------------------------------------------------------------------ */
/* PC protocol                                                               */
/* ------------------------------------------------------------------------ */

static void send_ack(uint8_t cmd, uint8_t seq, ack_status_t st)
{
    const uint8_t p[2] = { cmd, (uint8_t)st };
    (void)link_send(MSG_ACK, seq, p, sizeof p);
}

static void send_info(uint8_t seq)
{
    uint8_t *p0 = link_tx_begin(MSG_INFO, seq, (uint16_t)(24u + 11u * g_timebase_count + 12u));
    if (p0 == NULL) {
        return;
    }
    uint8_t *p = p0;
    p = put_u8(p, FW_VERSION_MAJOR);
    p = put_u8(p, FW_VERSION_MINOR);
    p = put_u8(p, FW_VERSION_PATCH);
    p = put_u8(p, PROTOCOL_VERSION);
    p = put_u32(p, SCOPE_TIMER_CLK_HZ);
    p = put_u32(p, ACQ_BUF_LEN);
    p = put_u32(p, ACQ_RECORD_MAX);
    p = put_u16(p, LCD_WIDTH);
    p = put_u16(p, LCD_HEIGHT);
    p = put_u8(p, (uint8_t)RANGE_COUNT);
    p = put_u8(p, g_timebase_count);
    p = put_u16(p, LINK_CAPTURE_CHUNK);
    for (uint32_t i = 0; i < g_timebase_count; i++) {
        const timebase_t *tb = &g_timebases[i];
        p = put_u32(p, tb->ns_per_div);
        p = put_u32(p, tb_period_cycles(tb));
        p = put_u16(p, tb->record_len);
        p = put_u8(p, tb->smp);
    }
    const uint32_t *uid = (const uint32_t *)UID_BASE;
    p = put_u32(p, uid[0]);
    p = put_u32(p, uid[1]);
    p = put_u32(p, uid[2]);
    link_tx_commit();
}

static void send_status(uint8_t seq)
{
    uint8_t *p0 = link_tx_begin(MSG_STATUS, seq, 96u);
    if (p0 == NULL) {
        return;
    }
    uint8_t *p = p0;
    p = put_u8(p, (uint8_t)s_acq.state);
    p = put_u8(p, (uint8_t)s_set.mode);
    p = put_u8(p, s_set.running ? 1u : 0u);
    p = put_u8(p, s_set.tb_index);
    p = put_u16(p, s_set.trig_level);
    p = put_u16(p, s_set.trig_hyst);
    p = put_u8(p, (uint8_t)s_set.edge);
    p = put_u8(p, s_set.pretrig_pct);
    p = put_u8(p, (uint8_t)s_set.range);
    p = put_u8(p, (uint8_t)s_set.gen_wave);
    p = put_f32(p, s_vdda_mv);
    p = put_u32(p, HAL_GetTick());
    p = put_u32(p, s_acq.stats.records);
    p = put_u32(p, s_acq.stats.triggers);
    p = put_u32(p, s_acq.stats.auto_triggers);
    p = put_u32(p, s_acq.stats.search_skips);
    p = put_u32(p, s_acq.stats.records_discarded);
    p = put_u32(p, s_acq.stats.max_stop_overshoot);
    p = put_u32(p, g_acq_hw_stats.adc_overruns);
    p = put_u32(p, g_acq_hw_stats.dma_events);
    p = put_u32(p, lcd_frames_sent());
    p = put_u32(p, g_link_stats.tx_packets);
    p = put_u32(p, g_link_stats.tx_dropped);
    p = put_u32(p, s_parser.stats.frames_ok);
    p = put_u32(p, s_parser.stats.crc_errors);
    p = put_u32(p, s_parser.stats.len_errors);
    p = put_u32(p, s_loop_max_cycles / (SystemCoreClock / 1000000u));  /* us */
    p = put_u32(p, s_loop_count);
    p = put_u32(p, s_boot_faults);
    p = put_u32(p, g_link_stats.tx_queue_hwm);
    p = put_u32(p, g_acq_hw_stats.adc_errors);
    while (p < p0 + 96u) {
        *p++ = 0u;                       /* reserved */
    }
    link_tx_commit();
}

static void meas_physical(float *vpp, float *vavg, float *vrms, float *freq, float *duty)
{
    const range_cal_t *cal = cur_cal();
    const float lsb_mv = s_vdda_mv / (float)ADC_FULL_SCALE;
    *vpp = (float)(s_meas.max - s_meas.min) * lsb_mv * cal->gain;
    *vavg = calib_raw_to_mv(s_meas.mean, s_vdda_mv, cal);
    const float ac = s_meas.ac_rms * lsb_mv;                  /* mV at ADC */
    const float dc = s_meas.mean * lsb_mv - cal->offset_mv;   /* mV at ADC */
    *vrms = cal->gain * sqrtf(ac * ac + dc * dc);
    const float fs = tb_sample_rate_hz(cur_tb());
    *freq = s_meas.periodic ? fs / s_meas.period : 0.0f;
    *duty = s_meas.periodic ? 100.0f * s_meas.duty : 0.0f;
}

static void send_meas(uint8_t seq)
{
    uint8_t buf[26];
    float vpp = 0, vavg = 0, vrms = 0, f = 0, d = 0;
    if (s_meas_valid) {
        meas_physical(&vpp, &vavg, &vrms, &f, &d);
    }
    uint8_t *p = buf;
    p = put_f32(p, vpp);
    p = put_f32(p, vavg);
    p = put_f32(p, vrms);
    p = put_f32(p, f);
    p = put_f32(p, d);
    p = put_u8(p, (uint8_t)((s_meas_valid ? 1u : 0u) | (s_meas.periodic ? 2u : 0u) |
                            ((s_meas.clip_low || s_meas.clip_high) ? 4u : 0u)));
    p = put_u8(p, 0u);
    p = put_u16(p, s_meas.min);
    (void)put_u16(p, s_meas.max);
    (void)link_send(MSG_MEAS, seq, buf, sizeof buf);
}

static void send_cal(uint8_t seq, uint8_t range)
{
    uint8_t buf[9];
    uint8_t *p = put_u8(buf, range);
    p = put_f32(p, s_cal[range].gain);
    (void)put_f32(p, s_cal[range].offset_mv);
    (void)link_send(MSG_CAL, seq, buf, sizeof buf);
}

/** CRC over the record's sample bytes, in time order (may wrap the ring). */
static uint32_t record_crc(uint32_t start_abs, uint32_t n)
{
    const uint32_t i0 = start_abs % ACQ_BUF_LEN;
    const uint32_t first = (n <= ACQ_BUF_LEN - i0) ? n : ACQ_BUF_LEN - i0;
    return link_crc_hw_2seg((const uint8_t *)&g_adc_buf[i0], 2u * first,
                            (const uint8_t *)&g_adc_buf[0], 2u * (n - first));
}

static void send_capture_header(void)
{
    uint8_t *p0 = link_tx_begin(MSG_CAPTURE_HDR, s_cap.seq, 50u);
    if (p0 == NULL) {
        capture_abort();                 /* host will time out and retry */
        return;
    }
    const range_cal_t *cal = cur_cal();
    const timebase_t *tb = &g_timebases[s_rec.tb_index];
    uint8_t *p = p0;
    p = put_u16(p, s_cap.id);
    p = put_u32(p, s_cap.n);
    p = put_u32(p, s_rec.pre_len);                  /* trigger sample index */
    p = put_f32(p, s_rec.trig_frac);
    p = put_u32(p, SCOPE_TIMER_CLK_HZ);
    p = put_u32(p, tb_period_cycles(tb));
    p = put_u32(p, tb->ns_per_div);
    p = put_u8(p, tb->smp);
    p = put_u8(p, (uint8_t)s_set.range);
    p = put_u8(p, (uint8_t)((s_rec.forced ? 1u : 0u) | (s_rec.edge == TRIG_EDGE_FALLING ? 2u : 0u)));
    p = put_u8(p, (uint8_t)s_rec.mode);
    p = put_u16(p, s_rec.trig_level);
    p = put_u16(p, LINK_CAPTURE_CHUNK);
    p = put_f32(p, s_vdda_mv);
    p = put_f32(p, cal->gain);
    p = put_f32(p, cal->offset_mv);
    (void)put_u32(p, record_crc(s_cap.start_abs, s_cap.n));
    link_tx_commit();
}

/** Queue as many capture chunks as there are free TX slots. */
static void capture_service(void)
{
    while (s_cap.active && link_tx_free() > 0u) {
        uint32_t cnt = s_cap.n - s_cap.sent;
        if (cnt > LINK_CAPTURE_CHUNK) {
            cnt = LINK_CAPTURE_CHUNK;
        }
        uint8_t *p = link_tx_begin(MSG_CAPTURE_DATA, s_cap.seq, (uint16_t)(8u + 2u * cnt));
        if (p == NULL) {
            return;
        }
        p = put_u16(p, s_cap.id);
        p = put_u32(p, s_cap.sent);
        p = put_u16(p, (uint16_t)cnt);
        uint32_t idx = (s_cap.start_abs + s_cap.sent) % ACQ_BUF_LEN;
        for (uint32_t i = 0; i < cnt; i++) {
            p = put_u16(p, g_adc_buf[idx]);
            if (++idx == ACQ_BUF_LEN) {
                idx = 0u;
            }
        }
        link_tx_commit();
        s_cap.sent += cnt;
        if (s_cap.sent >= s_cap.n) {
            s_cap.active = false;
            if (s_acq.state == ACQ_READY) {
                acq_release(&s_acq);
                if (s_acq.state != ACQ_STOPPED) {
                    s_frozen_valid = false;
                }
            }
        }
    }
}

static void on_frame(const proto_frame_t *f)
{
    const uint8_t *a = f->payload;
    s_last_rx_ms = HAL_GetTick();

    if (s_fault_mode && f->type != MSG_PING && f->type != MSG_GET_INFO &&
        f->type != MSG_GET_STATUS) {
        send_ack(f->type, f->seq, ACK_FAILED);   /* diagnostics only */
        return;
    }

    switch (f->type) {
    case MSG_PING:
        (void)link_send(MSG_PONG, f->seq, f->payload, f->len);
        break;

    case MSG_GET_INFO:
        send_info(f->seq);
        break;

    case MSG_GET_STATUS:
        send_status(f->seq);
        break;

    case MSG_GET_MEAS:
        send_meas(f->seq);
        break;

    case MSG_SET_RUN:
        if (f->len != 1u || a[0] > 2u) { send_ack(f->type, f->seq, ACK_BAD_ARG); break; }
        if (a[0] == 2u) {
            if (s_set.mode != TRIG_MODE_SINGLE) { s_mode_before_single = s_set.mode; }
            s_set.mode = TRIG_MODE_SINGLE;
            s_set.running = false;
        } else {
            if (s_set.mode == TRIG_MODE_SINGLE) { s_set.mode = s_mode_before_single; }
            s_set.running = (a[0] == 1u);
        }
        capture_abort();
        apply_changes(CHG_RUN);
        send_ack(f->type, f->seq, ACK_OK);
        break;

    case MSG_SET_TIMEBASE:
        if (f->len != 1u || a[0] >= g_timebase_count) { send_ack(f->type, f->seq, ACK_BAD_ARG); break; }
        s_set.tb_index = a[0];
        apply_changes(CHG_TIMEBASE);
        send_ack(f->type, f->seq, ACK_OK);
        break;

    case MSG_SET_TRIGGER: {
        if (f->len != 7u) { send_ack(f->type, f->seq, ACK_BAD_LEN); break; }
        scope_settings_t n = s_set;
        n.trig_level = get_u16(&a[0]);
        n.trig_hyst = get_u16(&a[2]);
        n.edge = (trig_edge_t)a[4];
        n.mode = (trig_mode_t)a[5];
        n.pretrig_pct = a[6];
        if (n.trig_level > ADC_FULL_SCALE || a[4] > 1u || a[5] >= TRIG_MODE_COUNT ||
            n.pretrig_pct < 10u || n.pretrig_pct > 90u || n.trig_hyst > 512u) {
            send_ack(f->type, f->seq, ACK_BAD_ARG);
            break;
        }
        if (n.mode == TRIG_MODE_SINGLE && s_set.mode != TRIG_MODE_SINGLE) {
            s_mode_before_single = s_set.mode;
        }
        s_set = n;
        capture_abort();
        apply_changes(CHG_TRIGGER);
        send_ack(f->type, f->seq, ACK_OK);
        break;
    }

    case MSG_SET_SIGGEN:
        if (f->len != 2u || a[0] >= GEN_WAVE_COUNT || a[1] >= GEN_FREQ_COUNT) {
            send_ack(f->type, f->seq, ACK_BAD_ARG);
            break;
        }
        s_set.gen_wave = (gen_wave_t)a[0];
        s_set.gen_freq = a[1];
        apply_changes(CHG_GEN);
        send_ack(f->type, f->seq, ACK_OK);
        break;

    case MSG_GET_CAPTURE:
        if (s_cap.active || s_cap.pending) {
            send_ack(f->type, f->seq, ACK_BUSY);
            break;
        }
        s_cap.seq = f->seq;
        if (f->len >= 1u && (a[0] & 1u) && s_frozen_valid) {
            /* Send the record that is frozen in the buffer right now. */
            s_cap.active = true;
            s_cap.start_abs = s_rec.start_abs;
            s_cap.n = s_rec.n;
            s_cap.sent = 0u;
            s_cap.id++;
            send_capture_header();
        } else if (s_acq.state == ACQ_ARMED || s_acq.state == ACQ_POSTTRIG) {
            s_cap.pending = true;        /* next record is streamed */
        } else {
            send_ack(f->type, f->seq, ACK_BUSY);   /* stopped, nothing frozen */
        }
        break;

    case MSG_CAL_GET:
        if (f->len != 1u || a[0] >= RANGE_COUNT) { send_ack(f->type, f->seq, ACK_BAD_ARG); break; }
        send_cal(f->seq, a[0]);
        break;

    case MSG_CAL_SET: {
        if (f->len != 9u || a[0] >= RANGE_COUNT) { send_ack(f->type, f->seq, ACK_BAD_ARG); break; }
        const float g = get_f32(&a[1]);
        const float o = get_f32(&a[5]);
        if (!(g > 0.01f && g < 1000.0f) || !(o > -5000.0f && o < 5000.0f)) {
            send_ack(f->type, f->seq, ACK_BAD_ARG);
            break;
        }
        s_cal[a[0]].gain = g;
        s_cal[a[0]].offset_mv = o;
        s_dirty = true;
        send_ack(f->type, f->seq, ACK_OK);
        break;
    }

    case MSG_CAL_SAVE: {
        /* The page erase stalls the CPU (and every ISR) for ~22 ms while the
         * DMA keeps writing; lap counting would be lost. Stop sampling first. */
        capture_abort();
        acquisition_stop();
        s_frozen_valid = false;
        const bool ok = settings_save(&s_set, s_cal);
        if (s_set.running || s_set.mode == TRIG_MODE_SINGLE) {
            acquisition_start();
        }
        send_ack(f->type, f->seq, ok ? ACK_OK : ACK_FAILED);
        break;
    }

    case MSG_RESET_STATS:
        memset(&s_acq.stats, 0, sizeof s_acq.stats);
        g_acq_hw_stats.dma_events = 0u;
        g_acq_hw_stats.adc_overruns = 0u;
        g_acq_hw_stats.adc_errors = 0u;
        memset(&s_parser.stats, 0, sizeof s_parser.stats);
        s_loop_max_cycles = 0u;
        send_ack(f->type, f->seq, ACK_OK);
        break;

    default:
        send_ack(f->type, f->seq, ACK_UNKNOWN);
        break;
    }
}

/* ------------------------------------------------------------------------ */
/* UI                                                                        */
/* ------------------------------------------------------------------------ */

static void handle_input(void)
{
    uint32_t chg = CHG_NONE;
    const int det = input_poll_encoder();
    if (det != 0) {
        chg |= ui_menu_adjust(&s_set, s_sel, det);
    }

    switch (input_poll_button()) {
    case BTN_MENU:
        s_sel = (menu_item_t)((s_sel + 1) % MENU_COUNT);
        s_dirty = true;
        break;
    case BTN_RUN:
        if (s_set.mode == TRIG_MODE_SINGLE) {
            s_set.mode = s_mode_before_single;
            s_set.running = true;
        } else {
            s_set.running = !s_set.running;
        }
        capture_abort();
        chg |= CHG_RUN;
        break;
    case BTN_SINGLE:
        if (s_set.mode != TRIG_MODE_SINGLE) {
            s_mode_before_single = s_set.mode;
        }
        s_set.mode = TRIG_MODE_SINGLE;
        s_set.running = false;
        capture_abort();
        chg |= CHG_RUN;
        break;
    case BTN_ENC:
        /* Auto level: trigger at the middle of the last record. */
        if (s_meas_valid && (uint32_t)(s_meas.max - s_meas.min) >= MEAS_MIN_PP_CODES) {
            s_set.trig_level = (uint16_t)((s_meas.min + s_meas.max) / 2u);
            chg |= CHG_TRIGGER;
        }
        break;
    case BTN_NONE:
    default:
        break;
    }
    if (chg != CHG_NONE) {
        apply_changes(chg);
    }
}

static void render_back_buffer(void)
{
    ui_view_t v;
    memset(&v, 0, sizeof v);
    v.set = &s_set;
    v.sel = s_sel;
    v.acq_state = s_acq.state;
    v.last_forced = s_last_forced;
    v.link_active = (HAL_GetTick() - s_last_rx_ms) < 2000u;
    v.have_trace = s_meas_valid;
    v.col_min = s_col_min;
    v.col_max = s_col_max;
    v.trig_col = s_trig_col;
    v.meas_valid = s_meas_valid;
    v.periodic = s_meas.periodic;
    v.clipped = s_meas.clip_low || s_meas.clip_high;
    v.trig_level_mv = calib_raw_to_mv((float)s_set.trig_level, s_vdda_mv, cur_cal());
    if (s_meas_valid) {
        meas_physical(&v.vpp_mv, &v.vavg_mv, &v.vrms_mv, &v.freq_hz, &v.duty_pct);
    }
    ui_render(s_fb[s_back], &v);
}

/**
 * True while the post-trigger phase is about to end. Rendering is deferred
 * then, because a long main-loop pass right before the stop would push the
 * stop latency past the guard band and cost the record.
 */
static bool stop_imminent(void)
{
    if (s_acq.state != ACQ_POSTTRIG) {
        return false;
    }
    const uint32_t post = s_acq.record_len - s_acq.pre_len;
    const uint32_t done = g_acq_hw_ops.write_pos(NULL) - s_acq.trig_abs;
    const uint32_t left = (done < post) ? post - done : 0u;
    /* < 2 ms of samples left */
    return (uint64_t)left * tb_period_cycles(cur_tb()) < (uint64_t)SCOPE_TIMER_CLK_HZ / 500u;
}

static void service_display(uint32_t now)
{
    if (stop_imminent()) {
        return;
    }
    /* Draw the next frame into the back buffer while the previous one may
     * still be going out over SPI DMA; flush as soon as the bus is free. */
    if (!s_back_ready && (now - s_last_render_ms) >= UI_FRAME_PERIOD_MS &&
        (s_dirty || (now - s_last_render_ms) >= 250u)) {
        render_back_buffer();
        s_back_ready = true;
        s_dirty = false;
        s_last_render_ms = now;
    }
    if (s_back_ready && !lcd_busy()) {
        if (lcd_flush_async(s_fb[s_back])) {
            s_back ^= 1u;
            s_back_ready = false;
        }
    }
}

/* ------------------------------------------------------------------------ */
/* Public                                                                    */
/* ------------------------------------------------------------------------ */

void app_init(void)
{
    cycle_counter_init();
    __HAL_DBGMCU_FREEZE_IWDG();          /* watchdog pauses at breakpoints */

    s_boot_faults = board_selfcheck();

    scope_settings_default(&s_set);
    for (uint32_t r = 0; r < RANGE_COUNT; r++) {
        s_cal[r] = g_ranges[r].nominal;
    }
    const bool loaded = settings_load(&s_set, s_cal);

    proto_parser_init(&s_parser);
    link_init();
    if (g_link_stats.hw_crc_mismatch != 0u) {
        s_boot_faults |= FAULT_HW_CRC;
    }

    if (!acq_hw_init()) {
        s_boot_faults |= FAULT_ADC_INIT;
    }
    s_vdda_mv = acq_hw_vdda_mv();
    acq_init(&s_acq, g_adc_buf, ACQ_BUF_LEN, ACQ_GUARD_SAMPLES, &g_acq_hw_ops);

    (void)HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_4);
    (void)HAL_TIM_Encoder_Start(&htim1, TIM_CHANNEL_ALL);
    input_init();
    lcd_init();

    char msg[96];
    snprintf(msg, sizeof msg, "boot fw %u.%u.%u faults=0x%02lx vdda=%ldmV settings=%s",
             FW_VERSION_MAJOR, FW_VERSION_MINOR, FW_VERSION_PATCH,
             (unsigned long)s_boot_faults, (long)s_vdda_mv, loaded ? "flash" : "default");
    log_text(msg);

    if ((s_boot_faults & (FAULT_ADC_DMA_NOT_CIRCULAR | FAULT_SPI_NOT_8BIT |
                          FAULT_SPI_NO_DMA | FAULT_ADC_INIT)) != 0u) {
        /* Configuration regression (usually a CubeMX regeneration): refuse to
         * run acquisition rather than show wrong data. Blink fast, keep the
         * PC link alive so the fault can be read with `scope.py status`. */
        s_fault_mode = true;
        for (;;) {
            LED_TOGGLE();
            const uint32_t t0 = HAL_GetTick();
            while (HAL_GetTick() - t0 < 100u) {
                link_poll_rx(&s_parser, on_frame);
                link_poll_tx();
            }
        }
    }

    apply_changes(CHG_TIMEBASE | CHG_GEN | CHG_BACKLIGHT);

    MX_IWDG_Init();                      /* started last: init is not refreshed */
}

void app_loop_once(void)
{
    const uint32_t t0 = DWT->CYCCNT;
    const uint32_t now = HAL_GetTick();

    /* 1. acquisition */
    if (acq_hw_take_fault()) {
        /* ADC overrun / DMA error: the current capture is invalid. */
        capture_abort();
        acquisition_reconfigure();
    }
    if (acq_poll(&s_acq)) {
        on_record_ready();
    }

    /* 2. commands */
    link_poll_rx(&s_parser, on_frame);

    /* 3. user input */
    handle_input();

    /* 4. PC data */
    capture_service();
    link_poll_tx();
    if (s_acq.state == ACQ_READY && !s_cap.active) {
        /* Safety net: a record must never stay parked without a consumer. */
        acq_release(&s_acq);
        if (s_acq.state != ACQ_STOPPED) {
            s_frozen_valid = false;
        }
    }

    /* 5. display */
    service_display(now);

    /* 6. housekeeping */
    (void)HAL_IWDG_Refresh(&hiwdg);
    if ((now & 0x3FFu) < 0x200u) { LED_ON(); } else { LED_OFF(); }   /* ~1 Hz heartbeat */

    const uint32_t dt = DWT->CYCCNT - t0;
    if (dt > s_loop_max_cycles) {
        s_loop_max_cycles = dt;
    }
    s_loop_count++;
}
