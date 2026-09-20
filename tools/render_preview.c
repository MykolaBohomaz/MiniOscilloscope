/**
 * render_preview.c - run the real firmware pipeline on the host.
 *
 *   simulated DMA writer -> acq_engine (trigger) -> meas_compute ->
 *   decimate_minmax -> ui_render -> PBM image
 *
 * The images in docs/img/ are produced by this program (see
 * tools/make_screenshots.py). They show exactly what the firmware draws,
 * but the input signal is synthetic - they are not photos of hardware.
 *
 * usage: render_preview <out_dir>
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "acq_engine.h"
#include "calib.h"
#include "decimate.h"
#include "measure.h"
#include "scope_config.h"
#include "scope_settings.h"
#include "timebase.h"
#include "ui_render.h"

static uint16_t buf[ACQ_BUF_LEN];

typedef enum { W_SINE, W_SQUARE, W_TRI } wave_t;

typedef struct {
    uint32_t pos;
    bool running;
    double fs, f, lo_mv, hi_mv, duty, noise_mv, vdda;
    wave_t wave;
    uint32_t rng;
} sim_t;

static double noise(sim_t *s)
{
    s->rng = s->rng * 1664525u + 1013904223u;
    return ((double)(s->rng >> 8) / 16777216.0 - 0.5) * 2.0;
}

static uint16_t sample(sim_t *s, uint32_t abs)
{
    const double t = (double)abs / s->fs;
    double ph = fmod(t * s->f, 1.0), v;
    const double mid = 0.5 * (s->lo_mv + s->hi_mv), amp = 0.5 * (s->hi_mv - s->lo_mv);
    switch (s->wave) {
    case W_SQUARE: v = ph < s->duty ? s->hi_mv : s->lo_mv; break;
    case W_TRI:    v = ph < 0.5 ? s->lo_mv + 2 * ph * (s->hi_mv - s->lo_mv)
                                : s->hi_mv - 2 * (ph - 0.5) * (s->hi_mv - s->lo_mv); break;
    default:       v = mid + amp * sin(2 * M_PI * ph); break;
    }
    v += s->noise_mv * noise(s);
    long code = lround(v * 4095.0 / s->vdda);
    if (code < 0) code = 0;
    if (code > 4095) code = 4095;
    return (uint16_t)code;
}

static uint32_t wp(void *c) { return ((sim_t *)c)->pos; }
static void run(void *c, bool r) { ((sim_t *)c)->running = r; }

static void write_pbm(const char *path, const uint8_t *fb)
{
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); exit(1); }
    fprintf(f, "P4\n%u %u\n", LCD_WIDTH, LCD_HEIGHT);
    for (uint32_t y = 0; y < LCD_HEIGHT; y++) {
        uint8_t row[LCD_WIDTH / 8] = {0};
        for (uint32_t x = 0; x < LCD_WIDTH; x++) {
            if ((fb[(y >> 3) * LCD_WIDTH + x] >> (y & 7)) & 1u) {
                row[x >> 3] |= (uint8_t)(0x80u >> (x & 7));
            }
        }
        fwrite(row, 1, sizeof row, f);
    }
    fclose(f);
    printf("wrote %s\n", path);
}

static void scene(const char *dir, const char *name, scope_settings_t *set, menu_item_t sel,
                  sim_t sim)
{
    const timebase_t *tb = &g_timebases[set->tb_index];
    sim.fs = tb_sample_rate_hz(tb);
    sim.vdda = 3300.0;
    acq_engine_t e;
    const acq_hw_ops_t ops = { wp, run, &sim };
    acq_init(&e, buf, ACQ_BUF_LEN, ACQ_GUARD_SAMPLES, &ops);
    e.trig.level = set->trig_level;
    e.trig.hyst = set->trig_hyst;
    e.trig.edge = set->edge;
    e.mode = set->mode;
    e.auto_timeout = 4u * tb->record_len;
    acq_set_record(&e, tb->record_len, set->pretrig_pct);
    acq_arm(&e);
    while (!acq_poll(&e)) {
        for (int i = 0; i < 64 && sim.running; i++) {
            buf[sim.pos % ACQ_BUF_LEN] = sample(&sim, sim.pos);
            sim.pos++;
        }
    }
    const uint32_t start = acq_record_start(&e);
    meas_t m;
    meas_compute(buf, ACQ_BUF_LEN, start, e.record_len, &m);
    static uint16_t lo[LCD_WIDTH], hi[LCD_WIDTH];
    decimate_minmax(buf, ACQ_BUF_LEN, start, e.record_len, lo, hi, LCD_WIDTH);

    const range_cal_t *cal = &g_ranges[RANGE_DIRECT].nominal;
    const float lsb = 3300.0f / 4095.0f;
    ui_view_t v = {0};
    v.set = set;
    v.sel = sel;
    v.acq_state = ACQ_POSTTRIG;
    v.last_forced = e.forced;
    v.have_trace = true;
    v.col_min = lo;
    v.col_max = hi;
    v.trig_col = (uint16_t)(e.pre_len * LCD_WIDTH / e.record_len);
    v.meas_valid = true;
    v.periodic = m.periodic;
    v.clipped = m.clip_low || m.clip_high;
    v.vpp_mv = (float)(m.max - m.min) * lsb;
    v.vavg_mv = calib_raw_to_mv(m.mean, 3300.0f, cal);
    v.vrms_mv = m.rms * lsb;
    v.freq_hz = m.periodic ? (float)sim.fs / m.period : 0.0f;
    v.duty_pct = 100.0f * m.duty;
    v.trig_level_mv = calib_raw_to_mv((float)set->trig_level, 3300.0f, cal);

    static uint8_t fb[LCD_FB_BYTES];
    ui_render(fb, &v);
    char path[512];
    snprintf(path, sizeof path, "%s/%s.pbm", dir, name);
    write_pbm(path, fb);
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : ".";
    scope_settings_t s;

    /* 1 kHz sine from the built-in DAC generator, 500 us/div */
    scope_settings_default(&s);
    s.tb_index = 6;
    s.gen_wave = GEN_SINE;
    scene(dir, "screen_sine_1k", &s, MENU_TIMEBASE,
          (sim_t){ .f = 1000, .lo_mv = 400, .hi_mv = 2900, .noise_mv = 6, .wave = W_SINE, .rng = 1 });

    /* 10 kHz, 25 % duty square, 50 us/div, 20 % pre-trigger, falling edge */
    scope_settings_default(&s);
    s.tb_index = 3;
    s.pretrig_pct = 20;
    s.edge = TRIG_EDGE_FALLING;
    s.trig_level = 1500;
    scene(dir, "screen_square_10k", &s, MENU_PRETRIG,
          (sim_t){ .f = 10000, .lo_mv = 300, .hi_mv = 3000, .duty = 0.25, .noise_mv = 8,
                   .wave = W_SQUARE, .rng = 2 });

    /* clipped triangle, trigger level being edited */
    scope_settings_default(&s);
    s.tb_index = 7;
    s.trig_level = 3000;
    scene(dir, "screen_clip_tri", &s, MENU_TRIG_LEVEL,
          (sim_t){ .f = 300, .lo_mv = -200, .hi_mv = 3600, .noise_mv = 4, .wave = W_TRI, .rng = 3 });
    return 0;
}
