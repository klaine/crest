// Alias floor: naive, 2-pt polyBLEP, 4-pt polyBLEP, mip-mapped table (linear / cubic read).
// Metric: energy more than 4 bins from any harmonic of f0, relative to total energy (Hann window).
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define FS 44100.0
#define N 8192

static double blep2(double t, double dt) {
    if (t < dt)       { double x = t / dt;         return x + x - x * x - 1.0; }
    if (t > 1.0 - dt) { double x = (t - 1.0) / dt; return x * x + x + x + 1.0; }
    return 0.0;
}

static double r4(double t, double dt) {
    if (t < 2.0 * dt) {
        double x = t / dt;
        if (x < 1.0) return -0.5 + x * (2.0 / 3.0 + x * x * (-1.0 / 3.0 + x * 0.125));
        double u = 2.0 - x; return -(u * u * u * u) / 24.0;
    }
    if (t > 1.0 - 2.0 * dt) {
        double x = (t - 1.0) / dt;
        if (x > -1.0) return 0.5 + x * (2.0 / 3.0 + x * x * (-1.0 / 3.0 - x * 0.125));
        double u = 2.0 + x; return (u * u * u * u) / 24.0;
    }
    return 0.0;
}

// Mip level as in plan/04 §5: level k covers up to 43.07*2^k Hz, H = floor(22050/top) harmonics, L = max(4H,64).
static double tab[8192];
static int tab_len;
static void build_table(double f0) {
    int k = (int)ceil(log2(f0 / 43.07)); if (k < 0) k = 0;
    double top = 43.07 * pow(2.0, k);
    int H = (int)floor(22050.0 / top); if (H < 1) H = 1;
    int L = 4 * H; if (L < 64) L = 64;
    tab_len = L;
    for (int i = 0; i < L; i++) {
        double s = 0;
        for (int h = 1; h <= H; h++) s += ((h & 1) ? 1.0 : -1.0) * sin(2.0 * M_PI * h * i / L) / h;
        tab[i] = s * 2.0 / M_PI;
    }
}
static double tab_read(double t, int cubic) {
    double p = t * tab_len; int i = (int)p; double f = p - i;
    #define T(j) tab[((i + (j)) % tab_len + tab_len) % tab_len]
    if (!cubic) return T(0) + f * (T(1) - T(0));
    double y0 = T(-1), y1 = T(0), y2 = T(1), y3 = T(2);
    return y1 + 0.5 * f * (y2 - y0 + f * (2.0 * y0 - 5.0 * y1 + 4.0 * y2 - y3 + f * (3.0 * (y1 - y2) + y3 - y0)));
}

static double alias_db(int mode, double f0) {
    static double y[N];
    double dt = f0 / FS, t = 0.1234;
    if (mode >= 3) build_table(f0);
    for (int n = 0; n < N; n++) {
        double naive = 2.0 * t - 1.0, v = naive;
        if (mode == 1) v = naive - blep2(t, dt);
        if (mode == 2) v = naive - 2.0 * r4(t, dt);
        if (mode == 3) v = tab_read(t, 0);
        if (mode == 4) v = tab_read(t, 1);
        y[n] = v * (0.5 - 0.5 * cos(2.0 * M_PI * n / N));
        t += dt; if (t >= 1.0) t -= 1.0;
    }
    double tot = 0, bad = 0;
    for (int k = 1; k < N / 2; k++) {
        double re = 0, im = 0;
        for (int n = 0; n < N; n++) { double a = 2.0 * M_PI * (double)k * n / N; re += y[n] * cos(a); im += y[n] * sin(a); }
        double e = re * re + im * im;
        double h = (k * FS / N) / f0, d = fabs(h - round(h));
        tot += e;
        if (d * f0 / (FS / N) > 4.0) bad += e;
    }
    return 10.0 * log10(bad / tot + 1e-30);
}

int main(void) {
    double f0s[] = {261.63, 523.25, 1046.5, 2093.0, 3077.7, 4186.0, 8000.3};
    printf("%8s %8s %8s %8s %10s %10s   (off-harmonic energy vs total, dB)\n", "f0 Hz", "naive", "2-pt", "4-pt", "tbl-lin", "tbl-cubic");
    for (int i = 0; i < 7; i++)
        printf("%8.0f %8.1f %8.1f %8.1f %10.1f %10.1f\n", f0s[i], alias_db(0, f0s[i]), alias_db(1, f0s[i]),
               alias_db(2, f0s[i]), alias_db(3, f0s[i]), alias_db(4, f0s[i]));
    return 0;
}
