// Chord alias floor + harmonic droop for naive / 2-pt / 4-pt polyBLEP (linear chain, double precision).
// Alias metric: energy more than 3 bins from any harmonic of ANY chord note, relative to total energy.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FS 44100.0
#define N 32768

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

static void fft(double *re, double *im, int n) {
    for (int i = 1, j = 0; i < n; i++) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) { double a = re[i]; re[i] = re[j]; re[j] = a; a = im[i]; im[i] = im[j]; im[j] = a; }
    }
    for (int len = 2; len <= n; len <<= 1) {
        double ang = -2.0 * M_PI / len, wr = cos(ang), wi = sin(ang);
        for (int i = 0; i < n; i += len) {
            double cr = 1, ci = 0;
            for (int k = 0; k < len / 2; k++) {
                int a = i + k, b = i + k + len / 2;
                double xr = re[b] * cr - im[b] * ci, xi = re[b] * ci + im[b] * cr;
                re[b] = re[a] - xr; im[b] = im[a] - xi; re[a] += xr; im[a] += xi;
                double t = cr * wr - ci * wi; ci = cr * wi + ci * wr; cr = t;
            }
        }
    }
}

static double sawsample(int mode, double t, double dt) {
    double naive = 2.0 * t - 1.0;
    if (mode == 1) return naive - blep2(t, dt);
    if (mode == 2) return naive - 2.0 * r4(t, dt);
    return naive;
}

static void render(int mode, const double *f0, int nn, double *y) {
    double t[16], dt[16];
    for (int v = 0; v < nn; v++) { t[v] = fmod(0.1234 + 0.2718 * v, 1.0); dt[v] = f0[v] / FS; }
    for (int n = 0; n < N; n++) {
        double s = 0;
        for (int v = 0; v < nn; v++) { s += sawsample(mode, t[v], dt[v]); t[v] += dt[v]; if (t[v] >= 1.0) t[v] -= 1.0; }
        y[n] = s / sqrt((double)nn) * (0.5 - 0.5 * cos(2.0 * M_PI * n / N));
    }
}

static void spectrum(const double *y, double *mag2) {
    static double re[N], im[N];
    memcpy(re, y, sizeof re); memset(im, 0, sizeof im);
    fft(re, im, N);
    for (int k = 0; k < N / 2; k++) mag2[k] = re[k] * re[k] + im[k] * im[k];
}

static double alias_db(int mode, const double *f0, int nn) {
    static double y[N], m2[N / 2];
    render(mode, f0, nn, y); spectrum(y, m2);
    double tot = 0, bad = 0;
    for (int k = 1; k < N / 2; k++) {
        double f = k * FS / N, near = 0;
        for (int v = 0; v < nn && !near; v++) { double h = f / f0[v]; if (fabs(h - round(h)) * f0[v] / (FS / N) <= 3.0) near = 1; }
        tot += m2[k]; if (!near) bad += m2[k];
    }
    return 10.0 * log10(bad / tot + 1e-30);
}

static double peak(const double *m2, double f) {   // amplitude near frequency f
    int c = (int)lround(f * N / FS); double best = 0;
    for (int k = c - 3; k <= c + 3; k++) if (m2[k] > best) best = m2[k];
    return sqrt(best);
}

int main(void) {
    double single[] = {130.81};
    double chord5[] = {130.81, 196.00, 329.63, 493.88, 587.33};        // C3 G3 E4 B4 D5
    double chord8[] = {130.81, 164.81, 196.00, 261.63, 329.63, 392.00, 523.25, 659.26};
    double hi4[]    = {1046.5, 1318.5, 1568.0, 2093.0};                // C6 E6 G6 C7
    printf("Linear chain, off-harmonic energy vs total (dB):\n%-22s %8s %8s %8s\n", "case", "naive", "2-pt", "4-pt");
    struct { const char *n; double *f; int k; } cs[] = {{"single C3", single, 1}, {"5-note chord low", chord5, 5}, {"8-note chord", chord8, 8}, {"4-note chord high", hi4, 4}};
    for (int i = 0; i < 4; i++)
        printf("%-22s %8.1f %8.1f %8.1f\n", cs[i].n, alias_db(0, cs[i].f, cs[i].k), alias_db(1, cs[i].f, cs[i].k), alias_db(2, cs[i].f, cs[i].k));

    printf("\nHarmonic droop at f0=1000.3 Hz (dB vs ideal 1/h, relative to fundamental):\n%-6s %8s %8s %8s %10s %10s\n", "harm", "Hz", "naive", "2-pt", "4-pt", "sinc^4 th.");
    double f0 = 1000.3, ff[1] = {f0};
    static double y[N], m2[3][N / 2];
    for (int m = 0; m < 3; m++) { render(m, ff, 1, y); spectrum(y, m2[m]); }
    int hs[] = {3, 5, 8, 10, 15, 18};
    for (int i = 0; i < 6; i++) {
        double h = hs[i], r[3];
        for (int m = 0; m < 3; m++) r[m] = 20.0 * log10((peak(m2[m], h * f0) / peak(m2[m], f0)) * h);
        double x = h * f0 / FS, s = sin(M_PI * x) / (M_PI * x), s1 = sin(M_PI * f0 / FS) / (M_PI * f0 / FS);
        printf("%-6d %8.0f %8.1f %8.1f %10.1f %10.1f\n", hs[i], h * f0, r[0], r[1], r[2], 20.0 * log10(pow(s / s1, 4)));
    }
    return 0;
}
