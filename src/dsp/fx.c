/* Crest effects. Algorithms adapted from Amino's src/dsp/fx.c (MIT licence). */
#include "fx.h"

#include <math.h>
#include <string.h>

#include "dsp_util.h"

#define FX_CLEAR_CHUNK 65536
#define FX_IDLE_HOLD 88200 /* 2 s */
/* Slow modulation (chorus taps, reverb line wobble) is evaluated at 16-sample points and
 * interpolated linearly in between. */
#define FX_MOD_CHUNK 16

enum { FZ_CHO = 0, FZ_DLY = 1, FZ_REV = 2 };
enum { W_DIST = 0, W_CHO = 1, W_DLY = 2, W_REV = 3 };

/* ------------------------------------------------------------------ helpers */

static float lin(float x, float lo, float hi) { return lo + (hi - lo) * clampf(x, 0.0f, 1.0f); }
static float exps(float x, float lo, float hi) { return lo * powf(hi / lo, clampf(x, 0.0f, 1.0f)); }

static float one_pole_coef(float fc, float sr) {
    if (fc < 1.0f) fc = 1.0f;
    if (fc > sr * 0.45f) fc = sr * 0.45f;
    return 1.0f - expf(-CREST_TAU * fc / sr);
}

/* Delay-line lengths in beats for dly_sync (index 0 = Off): the Peak's 16 divisions (manual p.37). */
static const float SYNC_BEATS[17] = {0.0f,       1.0f / 12.0f, 0.125f, 1.0f / 6.0f, 0.25f,
                                     0.375f,     1.0f / 3.0f,  0.5f,   0.75f,       2.0f / 3.0f,
                                     1.0f,       1.5f,         4.0f / 3.0f, 2.0f,   8.0f / 3.0f, 3.0f, 4.0f};

static const int REVERB_BASE[8] = {1557, 1617, 1491, 1422, 1277, 1356, 1188, 1116};

/* ------------------------------------------------------------------ delay line */

static void dl_setup(dl_t *d, float *buf, int n) {
    d->buf = buf;
    d->size = n;
    d->w = 0;
}

static void dl_write(dl_t *d, float x) {
    d->buf[d->w] = x;
    d->w++;
    if (d->w >= d->size) d->w = 0;
}

/* Fractional read `delay` samples behind the write head; cubic Hermite when there is room. */
static float dl_read(const dl_t *d, float delay) {
    float pos = (float)d->w - 1.0f - delay;
    int size = d->size;
    if (pos < 0.0f) {
        pos += (float)size;
        while (pos < 0.0f) pos += (float)size;
    }
    int i = (int)pos;
    float f = pos - (float)i;
    if (i >= size) i -= size;
    const float *b = d->buf;
    float x0 = b[i];
    int i1 = i + 1 < size ? i + 1 : 0;
    float x1 = b[i1];
    if (f == 0.0f) return x0;
    if (delay < 2.0f) return x0 + (x1 - x0) * f;
    int im = i - 1;
    if (im < 0) im += size;
    int i2 = i1 + 1 < size ? i1 + 1 : 0;
    float xm1 = b[im], x2 = b[i2];
    float c1 = 0.5f * (x1 - xm1);
    float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
    float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
    return ((c3 * f + c2) * f + c1) * f + x0;
}

/* ------------------------------------------------------------------ init / activity */

static void state_reset(fx_t *f, int z) {
    if (z == FZ_CHO) {
        f->c_phase = 0.0f;
        for (int c = 0; c < 2; c++) f->c_lp[c] = f->c_hp[c] = 0.0f;
    } else if (z == FZ_DLY) {
        f->y_dt = -1.0f;
        f->y_dstart = f->y_dtarget = f->y_rt = f->y_rstart = f->y_rtarget = 0.0f;
        f->y_dfade = 1.0f;
        for (int c = 0; c < 2; c++) f->y_lp[c] = f->y_hp[c] = 0.0f;
    } else {
        for (int k = 0; k < 8; k++) {
            f->r_damp[k] = 0.0f;
            f->r_mph[k] = (float)k / 8.0f;
        }
        f->r_hp[0] = f->r_hp[1] = 0.0f;
        for (int c = 0; c < 2; c++) f->r_olp[c] = f->r_ohp[c] = 0.0f;
    }
}

void fx_init(fx_t *f, int was_used) {
    float *m = f->mem;
    f->z_off[FZ_CHO] = 0;
    for (int c = 0; c < 2; c++) {
        dl_setup(&f->c_d[c], m, FX_CHO_N);
        m += FX_CHO_N;
    }
    f->z_bytes[FZ_CHO] = FX_CHO_FLOATS * sizeof(float);
    f->z_off[FZ_DLY] = f->z_off[FZ_CHO] + f->z_bytes[FZ_CHO];
    for (int c = 0; c < 2; c++) {
        dl_setup(&f->y_d[c], m, FX_DLY_N);
        m += FX_DLY_N;
    }
    f->z_bytes[FZ_DLY] = FX_DLY_FLOATS * sizeof(float);
    f->z_off[FZ_REV] = f->z_off[FZ_DLY] + f->z_bytes[FZ_DLY];
    for (int k = 0; k < 8; k++) {
        dl_setup(&f->r_line[k], m, FX_RL_N);
        m += FX_RL_N;
    }
    for (int c = 0; c < 2; c++)
        for (int k = 0; k < 4; k++) {
            dl_setup(&f->r_diff[c][k], m, FX_DF_N);
            m += FX_DF_N;
        }
    for (int c = 0; c < 2; c++) {
        dl_setup(&f->r_pre[c], m, FX_PRE_N);
        m += FX_PRE_N;
    }
    f->z_bytes[FZ_REV] = FX_REV_FLOATS * sizeof(float);
    for (int z = 0; z < 3; z++) {
        f->z[z].dirty = was_used ? 1 : 0;
        state_reset(f, z);
    }
}

void fx_reset(fx_t *f) {
    for (int z = 0; z < 3; z++) {
        if (f->z[z].active || f->z[z].dirty) f->z[z].dirty = 1;
        f->z[z].active = 0;
        f->z[z].need_clear = 0;
    }
    for (int c = 0; c < 2; c++) {
        f->d_hp[c] = f->d_lp[c] = f->d_xp[c] = f->d_fp[c] = f->d_hold[c] = 0.0f;
    }
    f->d_cnt = 0.0f;
    for (int i = 0; i < 4; i++) f->wet_last[i] = 0.0f;
}

/* Decide whether effect z may run this block. An effect silent for FX_IDLE_HOLD is switched off (its tail
 * is cut); switching it back on first clears its buffers, 64 KB per block, staying dry meanwhile. The
 * hold keeps a level that modulation dips to 0 (an LFO on Delay Level, say) from wiping the echoes. */
static int fx_gate(fx_t *f, int z, int want, int n) {
    fxz_t *s = &f->z[z];
    if (!s->active) {
        if (!want) return 0;
        s->active = 1;
        s->idle = 0;
        state_reset(f, z);
        if (s->dirty) {
            s->need_clear = 1;
            s->pos = 0;
            s->len = f->z_bytes[z];
        }
    }
    if (s->need_clear) {
        uint32_t left = s->len - s->pos;
        uint32_t chunk = left > FX_CLEAR_CHUNK ? FX_CLEAR_CHUNK : left;
        memset((char *)f->mem + f->z_off[z] + s->pos, 0, chunk);
        s->pos += chunk;
        if (s->pos >= s->len) {
            s->need_clear = 0;
            s->dirty = 0;
        }
        return 0;
    }
    s->dirty = 1;
    if (want) {
        s->idle = 0;
    } else if ((s->idle += (uint32_t)n) >= FX_IDLE_HOLD) {
        s->active = 0;
        return 0;
    }
    return 1;
}

/* ------------------------------------------------------------------ mixing */

/* Equal-power dry/wet for the chorus and distortion (wet = m 0..1), ramped across the block. */
static void mix_xfade(float *l, float *r, const float *dl, const float *dr, int n, float m0, float m1,
                      int equal_power) {
    for (int i = 0; i < n; i++) {
        float m = m0 + (m1 - m0) * (float)(i + 1) / (float)n;
        float dg, wg;
        if (equal_power) {
            dg = cosf(m * CREST_PI * 0.5f);
            wg = sinf(m * CREST_PI * 0.5f);
        } else {
            dg = 1.0f - m;
            wg = m;
        }
        l[i] = dl[i] * dg + l[i] * wg;
        r[i] = dr[i] * dg + r[i] * wg;
    }
}

/* Wet return added on top of the dry signal. */
static void mix_return(float *l, float *r, const float *dl, const float *dr, int n, float m0, float m1) {
    for (int i = 0; i < n; i++) {
        float m = m0 + (m1 - m0) * (float)(i + 1) / (float)n;
        l[i] = dl[i] + l[i] * m;
        r[i] = dr[i] + r[i] * m;
    }
}

/* ------------------------------------------------------------------ distortion */

static float logcosh(float x) {
    float ax = fabsf(x);
    return ax + log1pf(expf(-2.0f * ax)) - 0.6931471805599453f;
}

/* First-order antiderivative anti-aliasing for soft (tanh), hard (clip) and fold (sin). */
static float shaper(int type, float x) {
    switch (type) {
    case 0: return fast_tanh(x);
    case 1: return clampf(x, -1.0f, 1.0f);
    default: return sinf(x);
    }
}

static float adaa_F(int type, float x) {
    switch (type) {
    case 0: return logcosh(x);
    case 1: return fabsf(x) > 1.0f ? fabsf(x) - 0.5f : 0.5f * x * x;
    default: return -cosf(x);
    }
}

static void fx_dist(fx_t *f, const float *pv, float *l, float *r, int n) {
    float lvl = pv[P_DST_LVL];
    float m1 = pv[P_DST_MIX] * (lvl * 20.0f > 1.0f ? 1.0f : lvl * 20.0f);
    float m0 = f->wet_last[W_DIST];
    f->wet_last[W_DIST] = m1;
    if (m1 <= 0.0005f && m0 <= 0.0005f) return;
    int type = (int)pv[P_DST_TYPE];
    float g = 1.0f + 30.0f * lvl * lvl;
    float trim = 1.0f / sqrtf(g);
    float tc = one_pole_coef(1500.0f * powf(20000.0f / 1500.0f, pv[P_DST_TONE]), CREST_FS);
    float hpc = one_pole_coef(20.0f, CREST_FS);
    float levels = powf(2.0f, 12.0f - 8.0f * lvl) * 0.5f;
    float rate = 1.0f / (1.0f + 7.0f * lvl);
    float dry[2][256];
    float *bufs[2] = {l, r};
    memcpy(dry[0], l, (size_t)n * sizeof(float));
    memcpy(dry[1], r, (size_t)n * sizeof(float));
    for (int i = 0; i < n; i++) {
        int take = 0;
        if (type == 3) {
            f->d_cnt += rate;
            if (f->d_cnt >= 1.0f) {
                f->d_cnt -= 1.0f;
                take = 1;
            }
        }
        for (int c = 0; c < 2; c++) {
            float xin = bufs[c][i];
            f->d_hp[c] += (xin - f->d_hp[c]) * hpc;
            float x = (xin - f->d_hp[c]) * g;
            if (type < 3) {
                float F = adaa_F(type, x);
                float dx = x - f->d_xp[c];
                float y = fabsf(dx) > 1e-5f ? (F - f->d_fp[c]) / dx : shaper(type, 0.5f * (x + f->d_xp[c]));
                f->d_xp[c] = x;
                f->d_fp[c] = F;
                x = y;
            } else {
                if (take) f->d_hold[c] = roundf(fast_tanh(x) * levels) / levels;
                x = f->d_hold[c];
            }
            f->d_lp[c] += (x - f->d_lp[c]) * tc;
            bufs[c][i] = f->d_lp[c] * trim;
        }
    }
    mix_xfade(l, r, dry[0], dry[1], n, m0, m1, 0);
}

/* ------------------------------------------------------------------ chorus */

static void fx_chorus_run(fx_t *f, const float *pv, float *l, float *r, int n) {
    float sr = CREST_FS;
    int nv = (int)pv[P_CHO_TYPE] + 1;
    int ens = pv[P_CHO_TYPE] > 1.5f;
    float rate = exps(pv[P_CHO_RATE], 0.05f, 8.0f);
    float base = lin(pv[P_CHO_DLY], 3.0f, 30.0f) * 0.001f * sr;
    float dep = pv[P_CHO_DEPTH] * 0.006f * sr;
    if (dep > 0.9f * base) dep = 0.9f * base;
    float fb = pv[P_CHO_FB] * 0.7f;
    float inc = rate / sr;
    float inv_nv = 1.0f / (float)nv;
    /* LoPass / HiPass: a simple EQ on the chorus output (manual p.32). */
    float c_lp = one_pole_coef(exps(pv[P_CHO_LP], 500.0f, 20000.0f), sr);
    float c_hp = one_pole_coef(exps(pv[P_CHO_HP], 20.0f, 1000.0f), sr);
    float maxd = (float)(FX_CHO_N - 6);
    for (int i0 = 0; i0 < n; i0 += FX_MOD_CHUNK) {
        int m = n - i0;
        if (m > FX_MOD_CHUNK) m = FX_MOD_CHUNK;
        float ph0 = f->c_phase * CREST_TAU;
        float ph1 = (f->c_phase + inc * (float)m) * CREST_TAU;
        float tap[2][3], step[2][3];
        for (int c = 0; c < 2; c++) {
            for (int k = 0; k < nv; k++) {
                float o = (float)k * CREST_TAU / (float)nv + (float)c * (CREST_PI * 0.5f);
                float a = sinf(ph0 + o), b = sinf(ph1 + o);
                if (ens) {
                    a += 0.2f * sinf(6.3f * ph0 + o);
                    b += 0.2f * sinf(6.3f * ph1 + o);
                }
                a = clampf(base + dep * a, 2.0f, maxd);
                b = clampf(base + dep * b, 2.0f, maxd);
                tap[c][k] = a;
                step[c][k] = (b - a) / (float)m;
            }
        }
        for (int j = 0; j < m; j++) {
            int i = i0 + j;
            float out[2];
            for (int c = 0; c < 2; c++) {
                float acc = 0.0f;
                for (int k = 0; k < nv; k++) acc += dl_read(&f->c_d[c], tap[c][k] + step[c][k] * (float)j);
                out[c] = acc * inv_nv;
            }
            dl_write(&f->c_d[0], l[i] + fb * out[0]);
            dl_write(&f->c_d[1], r[i] + fb * out[1]);
            for (int c = 0; c < 2; c++) {
                f->c_lp[c] += (out[c] - f->c_lp[c]) * c_lp;
                f->c_hp[c] += (f->c_lp[c] - f->c_hp[c]) * c_hp;
            }
            l[i] = f->c_lp[0] - f->c_hp[0];
            r[i] = f->c_lp[1] - f->c_hp[1];
            f->c_phase += inc;
            if (f->c_phase >= 1.0f) f->c_phase -= floorf(f->c_phase);
        }
    }
}

static void fx_chorus(fx_t *f, const float *pv, float *l, float *r, int n) {
    float m1 = pv[P_CHO_LVL], m0 = f->wet_last[W_CHO];
    f->wet_last[W_CHO] = m1;
    if (!fx_gate(f, FZ_CHO, m1 > 0.0005f || m0 > 0.0005f, n)) return;
    float dry[2][256];
    memcpy(dry[0], l, (size_t)n * sizeof(float));
    memcpy(dry[1], r, (size_t)n * sizeof(float));
    fx_chorus_run(f, pv, l, r, n);
    mix_return(l, r, dry[0], dry[1], n, m0, m1); /* Level adds the chorus to the dry signal (manual p.31) */
}

/* ------------------------------------------------------------------ delay */

/* Right-channel delay time relative to the left for each L/R Ratio option ("a/b" = left : right). */
static const float LR_RIGHT[11] = {1.0f, 0.75f, 4.0f / 3.0f, 2.0f / 3.0f, 1.5f, 0.5f, 2.0f, 1.0f / 3.0f, 3.0f, 0.25f, 4.0f};

static void fx_delay_run(fx_t *f, const float *pv, float bpm, float *l, float *r, int n) {
    float sr = CREST_FS;
    int sync = (int)pv[P_DLY_SYNC];
    float tsec;
    if (sync == 0) tsec = exps(pv[P_DLY_TIME], 0.010f, 2.0f);
    else tsec = SYNC_BEATS[sync] * 60.0f / (bpm < 20.0f ? 120.0f : bpm);
    /* L/R Ratio: the right echoes run at a simple ratio of the left ones (manual p.32). The longer side
     * must fit the 2 s line; halving keeps a synced pair on the grid. */
    float rr = LR_RIGHT[clampi((int)pv[P_DLY_RATIO], 0, 10)];
    float tl = tsec, tr = tsec * rr;
    while ((tl > tr ? tl : tr) > 2.0f) {
        tl *= 0.5f;
        tr *= 0.5f;
    }
    float tgl = tl * sr, tgr = tr * sr;
    if (f->y_dt < 0.0f) {
        f->y_dt = f->y_dstart = f->y_dtarget = tgl;
        f->y_rt = f->y_rstart = f->y_rtarget = tgr;
        f->y_dfade = 1.0f;
    } else if (sync != 0) {
        if (fabsf(tgl - f->y_dtarget) > 1.0f || fabsf(tgr - f->y_rtarget) > 1.0f) {
            /* A tempo or division change must not sweep the read heads (that Doppler-glides every echo):
             * crossfade between two fixed taps instead. */
            float fr = clampf(f->y_dfade, 0.0f, 1.0f);
            f->y_dstart += (f->y_dtarget - f->y_dstart) * fr;
            f->y_rstart += (f->y_rtarget - f->y_rstart) * fr;
            f->y_dtarget = tgl;
            f->y_rtarget = tgr;
            f->y_dfade = 0.0f;
        }
    } else if (f->y_dfade < 1.0f) { /* free time: no crossfade, the read heads slew from where they are */
        f->y_dfade = 1.0f;
    }
    /* Slew Rate: how slowly a free delay time follows its knob; the moving read head shifts the pitch of
     * the echoes. 0 = 10 ms (a strong, quick bend), 1 = 3 s (barely any pitch shift). */
    float cs = 1.0f - expf(-1.0f / (0.010f * powf(300.0f, clampf(pv[P_DLY_SLEW], 0.0f, 1.0f)) * sr));
    float width = clampf(pv[P_DLY_WIDTH], 0.0f, 1.0f);
    /* Feedback 0.98 x^1.5: the Peak's middle setting gives about 5 or 6 audible echoes (-9 dB per
     * repeat) and full feedback barely decays (manual p.31). */
    float fbx = clampf(pv[P_DLY_FB], 0.0f, 1.0f);
    float fb = 0.98f * fbx * sqrtf(fbx);
    int mode = (int)pv[P_DLY_MODE]; /* 0 Stereo, 1 PingPong, 2 Mono */
    float c_lp = one_pole_coef(exps(pv[P_DLY_LP], 1000.0f, 20000.0f), sr);
    float c_hp = one_pole_coef(exps(pv[P_DLY_HP], 20.0f, 2000.0f), sr);
    float fade_step = 1.0f / (0.050f * sr);
    for (int i = 0; i < n; i++) {
        float yl, yr;
        if (sync == 0) {
            f->y_dt += (tgl - f->y_dt) * cs;
            f->y_rt += (tgr - f->y_rt) * cs;
            f->y_dstart = f->y_dtarget = f->y_dt;
            f->y_rstart = f->y_rtarget = f->y_rt;
            yl = dl_read(&f->y_d[0], f->y_dt);
            yr = dl_read(&f->y_d[1], f->y_rt);
        } else {
            float tap_fade = clampf(f->y_dfade, 0.0f, 1.0f);
            if (tap_fade >= 1.0f) {
                yl = dl_read(&f->y_d[0], f->y_dtarget);
                yr = dl_read(&f->y_d[1], f->y_rtarget);
            } else {
                float ol = dl_read(&f->y_d[0], f->y_dstart), orr = dl_read(&f->y_d[1], f->y_rstart);
                float nl = dl_read(&f->y_d[0], f->y_dtarget), nr = dl_read(&f->y_d[1], f->y_rtarget);
                yl = ol + (nl - ol) * tap_fade;
                yr = orr + (nr - orr) * tap_fade;
            }
            f->y_dt = f->y_dstart + (f->y_dtarget - f->y_dstart) * tap_fade;
            f->y_rt = f->y_rstart + (f->y_rtarget - f->y_rstart) * tap_fade;
            if (f->y_dfade < 1.0f) {
                f->y_dfade += fade_step;
                if (f->y_dfade >= 1.0f) {
                    f->y_dfade = 1.0f;
                    f->y_dstart = f->y_dt = f->y_dtarget;
                    f->y_rstart = f->y_rt = f->y_rtarget;
                }
            }
        }
        /* Feedback path: low-pass then high-pass. The tap itself is heard unfiltered. */
        f->y_lp[0] += (yl - f->y_lp[0]) * c_lp;
        f->y_lp[1] += (yr - f->y_lp[1]) * c_lp;
        f->y_hp[0] += (f->y_lp[0] - f->y_hp[0]) * c_hp;
        f->y_hp[1] += (f->y_lp[1] - f->y_hp[1]) * c_hp;
        float fl = f->y_lp[0] - f->y_hp[0];
        float fr2 = f->y_lp[1] - f->y_hp[1];
        float mono = 0.5f * (l[i] + r[i]);
        float inl, inr, wl, wr;
        if (mode == 1) { /* ping-pong: mono in on the left, feedback crosses over */
            inl = mono;
            inr = 0.0f;
            wl = inl + fb * fr2;
            wr = inr + fb * fl;
        } else {
            inl = mode == 2 ? mono : l[i];
            inr = mode == 2 ? mono : r[i];
            wl = inl + fb * fl;
            wr = inr + fb * fr2;
        }
        dl_write(&f->y_d[0], fast_tanh(wl));
        dl_write(&f->y_d[1], fast_tanh(wr));
        /* Width narrows the echoes towards the centre (manual p.32): 1 = as they are, 0 = mono. */
        float mid = 0.5f * (yl + yr), side = 0.5f * (yl - yr) * width;
        l[i] = mid + side;
        r[i] = mid - side;
    }
}

static void fx_delay(fx_t *f, const float *pv, float bpm, float *l, float *r, int n) {
    float m1 = pv[P_DLY_LVL], m0 = f->wet_last[W_DLY];
    f->wet_last[W_DLY] = m1;
    if (!fx_gate(f, FZ_DLY, m1 > 0.0005f || m0 > 0.0005f, n)) return;
    float dry[2][256];
    memcpy(dry[0], l, (size_t)n * sizeof(float));
    memcpy(dry[1], r, (size_t)n * sizeof(float));
    fx_delay_run(f, pv, bpm, l, r, n);
    mix_return(l, r, dry[0], dry[1], n, m0, m1);
}

/* ------------------------------------------------------------------ reverb */

static void fx_reverb_run(fx_t *f, const float *pv, float *l, float *r, int n) {
    static const int MOD_RATE[8] = {61, 83, 71, 97, 53, 89, 67, 77}; /* x0.01 Hz */
    static const float difftaps[2][4] = {{142, 107, 379, 277}, {151, 113, 389, 293}};
    /* Room / Hall / Plate: line-length scale, diffuser gain, wobble depth in samples. */
    static const float TYPE_SCALE[3] = {0.9f, 1.5f, 1.2f};
    static const float TYPE_DIFF[3] = {0.6f, 0.6f, 0.75f};
    static const float TYPE_MOD[3] = {6.0f, 12.0f, 18.0f};
    float sr = CREST_FS;
    int type = clampi((int)pv[P_REV_TYPE], 0, 2);
    float pre = pv[P_REV_PRE] * 0.5f * sr; /* up to half a second, as the Peak's PreDelay (manual p.32) */
    /* ModDepth scales the line wobble (0.5 = the type's own depth), ModRate its speed (manual p.32). */
    float mod = TYPE_MOD[type] * 2.0f * clampf(pv[P_REV_MDEP], 0.0f, 1.0f);
    float mrate = exp2f(5.0f * clampf(pv[P_REV_MRATE], 0.0f, 1.0f));
    /* LoPass / HiPass: an EQ on the reverb output, apart from the damping inside the tank. */
    float c_olp = one_pole_coef(exps(pv[P_REV_LPF], 500.0f, 20000.0f), sr);
    float c_ohp = one_pole_coef(exps(pv[P_REV_HPF], 20.0f, 1000.0f), sr);
    float dgain = TYPE_DIFF[type];
    float t60 = exps(pv[P_REV_DECAY], 0.2f, 20.0f);
    float damp = pv[P_REV_DAMP];
    float dc = one_pole_coef(500.0f + 19500.0f * (1.0f - damp) * (1.0f - damp), sr);
    float hc = one_pole_coef(exps(pv[P_REV_HP], 20.0f, 1000.0f), sr);
    float size_scale = (0.6f + 1.4f * clampf(pv[P_REV_SIZE], 0.0f, 1.0f)) * TYPE_SCALE[type];
    for (int k = 0; k < 8; k++) {
        f->r_len[k] = (int)((float)REVERB_BASE[k] * size_scale);
        f->r_gain[k] = powf(10.0f, -3.0f * (float)f->r_len[k] / (t60 * sr));
    }
    float mod_pos[8], mod_step[8], mod_inc[8];
    for (int k = 0; k < 8; k++) mod_inc[k] = (float)MOD_RATE[k] * 0.01f * mrate / sr;
    for (int i = 0; i < n; i++) {
        if (i % FX_MOD_CHUNK == 0) {
            int m = n - i;
            if (m > FX_MOD_CHUNK) m = FX_MOD_CHUNK;
            for (int k = 0; k < 8; k++) {
                float a = mod * sinf(CREST_TAU * f->r_mph[k]);
                float b = mod * sinf(CREST_TAU * (f->r_mph[k] + mod_inc[k] * (float)m));
                mod_pos[k] = a;
                mod_step[k] = (b - a) / (float)m;
            }
        }
        float xs[2];
        for (int c = 0; c < 2; c++) {
            float *src = c == 0 ? l : r;
            dl_write(&f->r_pre[c], src[i]);
            float x = dl_read(&f->r_pre[c], pre);
            f->r_hp[c] += (x - f->r_hp[c]) * hc; /* input low cut */
            x -= f->r_hp[c];
            for (int k = 0; k < 4; k++) {
                float y = dl_read(&f->r_diff[c][k], difftaps[c][k] - 1.0f);
                float w = x + dgain * y;
                dl_write(&f->r_diff[c][k], w);
                x = y - dgain * w;
            }
            xs[c] = x;
        }
        float o[8];
        for (int k = 0; k < 8; k++) {
            o[k] = dl_read(&f->r_line[k], (float)f->r_len[k] - 1.0f + mod_pos[k]);
            mod_pos[k] += mod_step[k];
            f->r_mph[k] += mod_inc[k];
            if (f->r_mph[k] >= 1.0f) f->r_mph[k] -= floorf(f->r_mph[k]);
            f->r_damp[k] += (o[k] - f->r_damp[k]) * dc;
            o[k] = f->r_damp[k];
        }
        /* 8x8 Hadamard feedback matrix as a fast Walsh-Hadamard butterfly. */
        float a = o[0] + o[1], b = o[0] - o[1];
        float c2 = o[2] + o[3], d = o[2] - o[3];
        float e = o[4] + o[5], g = o[4] - o[5];
        float h = o[6] + o[7], q = o[6] - o[7];
        float a2 = a + c2, b2 = b + d, c3 = a - c2, d2 = b - d;
        float e2 = e + h, f2 = g + q, g2 = e - h, h2 = g - q;
        float mix[8];
        mix[0] = a2 + e2; mix[1] = b2 + f2; mix[2] = c3 + g2; mix[3] = d2 + h2;
        mix[4] = a2 - e2; mix[5] = b2 - f2; mix[6] = c3 - g2; mix[7] = d2 - h2;
        for (int k = 0; k < 8; k++)
            dl_write(&f->r_line[k], mix[k] * 0.35355339f * f->r_gain[k] + xs[k & 1]);
        float out[2] = {(o[0] + o[2] + o[4] + o[6]) * 0.3f, (o[1] + o[3] + o[5] + o[7]) * 0.3f};
        for (int c = 0; c < 2; c++) {
            f->r_olp[c] += (out[c] - f->r_olp[c]) * c_olp;
            f->r_ohp[c] += (f->r_olp[c] - f->r_ohp[c]) * c_ohp;
        }
        l[i] = f->r_olp[0] - f->r_ohp[0];
        r[i] = f->r_olp[1] - f->r_ohp[1];
    }
}

static void fx_reverb(fx_t *f, const float *pv, float *l, float *r, int n) {
    float m1 = pv[P_REV_LVL], m0 = f->wet_last[W_REV];
    f->wet_last[W_REV] = m1;
    if (!fx_gate(f, FZ_REV, m1 > 0.0005f || m0 > 0.0005f, n)) return;
    float dry[2][256];
    memcpy(dry[0], l, (size_t)n * sizeof(float));
    memcpy(dry[1], r, (size_t)n * sizeof(float));
    fx_reverb_run(f, pv, l, r, n);
    mix_return(l, r, dry[0], dry[1], n, m0, m1);
}

static void fx_stage(fx_t *f, int which, const float *pv, float bpm, float *l, float *r, int n) {
    switch (which) {
    case 0: fx_chorus(f, pv, l, r, n); break;
    case 1: fx_delay(f, pv, bpm, l, r, n); break;
    default: fx_reverb(f, pv, l, r, n); break;
    }
}

/* fx_route: 0 parallel, then the six serial orders of delay (1), reverb (2) and chorus (0). */
static const uint8_t ROUTE_ORDER[7][3] = {{0, 1, 2}, {1, 2, 0}, {1, 0, 2}, {2, 1, 0},
                                          {2, 0, 1}, {0, 1, 2}, {0, 2, 1}};

void fx_process(fx_t *f, const float *pv, float bpm, float *l, float *r, int n) {
    if (n <= 0) return;
    if (n > 256) n = 256;
    float wet = pv[P_FX_WET], dry = pv[P_FX_DRY];
    float in[2][256];
    int mix = wet < 0.999f || dry < 0.999f;
    fx_dist(f, pv, l, r, n);
    if (mix) { /* the dry/wet blend covers the three time-domain effects, not the distortion */
        memcpy(in[0], l, (size_t)n * sizeof(float));
        memcpy(in[1], r, (size_t)n * sizeof(float));
    }
    int route = clampi((int)pv[P_FX_ROUTE], 0, 6);
    if (route == 0) { /* parallel: every effect sees the same input; only what each adds is summed */
        float src[2][256], acc[2][256], t[2][256];
        memcpy(src[0], l, (size_t)n * sizeof(float));
        memcpy(src[1], r, (size_t)n * sizeof(float));
        for (int s = 0; s < 3; s++) {
            memcpy(t[0], src[0], (size_t)n * sizeof(float));
            memcpy(t[1], src[1], (size_t)n * sizeof(float));
            fx_stage(f, s, pv, bpm, t[0], t[1], n);
            for (int i = 0; i < n; i++) {
                if (s == 0) {
                    acc[0][i] = t[0][i] - src[0][i];
                    acc[1][i] = t[1][i] - src[1][i];
                } else {
                    acc[0][i] += t[0][i] - src[0][i];
                    acc[1][i] += t[1][i] - src[1][i];
                }
            }
        }
        for (int i = 0; i < n; i++) {
            l[i] = src[0][i] + acc[0][i];
            r[i] = src[1][i] + acc[1][i];
        }
    } else {
        for (int k = 0; k < 3; k++) fx_stage(f, ROUTE_ORDER[route][k], pv, bpm, l, r, n);
    }
    if (mix) { /* dry keeps the pre-effect signal, wet scales everything the chain added or changed */
        for (int i = 0; i < n; i++) {
            l[i] = in[0][i] * dry + (l[i] - in[0][i]) * wet;
            r[i] = in[1][i] * dry + (r[i] - in[1][i]) * wet;
        }
    }
}
