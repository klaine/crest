#ifndef CREST_WASM
#define _GNU_SOURCE /* sched_setaffinity, CPU_SET */
#endif
#include "peak_bank.h"

#include <stdatomic.h>
#include <string.h>

#include "crest.h"
#include "level.h"

#ifdef CREST_WASM

void peak_bank_start(const plugin_api_v2_t *api) { (void)api; }
void peak_bank_request(int slot, const char *path) { (void)slot, (void)path; }
int peak_bank_path(int slot, char *buf, int cap) {
    (void)slot;
    if (cap > 0) buf[0] = 0;
    return 0;
}
int peak_bank_patch(int slot, int idx, float *v, char name[PEAK_NAME_MAX]) {
    (void)slot, (void)idx, (void)v, (void)name;
    return 0;
}
float peak_bank_vol(int slot, int idx) {
    (void)slot, (void)idx;
    return -1.0f;
}
int peak_bank_count(int slot) {
    (void)slot;
    return 0;
}
int peak_bank_ready(int slot) {
    (void)slot;
    return 0;
}

#else

#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <time.h>

#define PATH_CAP 256

typedef struct {
    /* written by the audio thread */
    char req[PATH_CAP];
    _Atomic int req_gen;
    _Atomic int loaded_gen; /* the req_gen the worker last finished loading */
    _Atomic int want; /* the patch to level first */
    /* written by the worker; readers check seq (odd = being written) */
    _Atomic int seq;
    int count;
    char names[PEAK_BANK_MAX][PEAK_NAME_MAX];
    float vals[PEAK_BANK_MAX][P_COUNT];
    _Atomic uint32_t vol[PEAK_BANK_MAX]; /* float bits; negative = not solved yet */
} bank_t;

static bank_t g_bank[CREST_MAX_INSTANCES];
static const plugin_api_v2_t *g_api;
static _Atomic int g_started;
static _Atomic int g_stop; /* set when the library is unloaded: the worker must be gone before its code is */
static pthread_t g_thread;

static uint32_t fbits(float f) {
    uint32_t u;
    memcpy(&u, &f, 4);
    return u;
}
static float bitsf(uint32_t u) {
    float f;
    memcpy(&f, &u, 4);
    return f;
}

static int ok_slot(int slot) { return slot >= 0 && slot < CREST_MAX_INSTANCES; }

/* ------------------------------------------------------------------ audio-thread side */

void peak_bank_request(int slot, const char *path) {
    if (!ok_slot(slot)) return;
    bank_t *b = &g_bank[slot];
    strncpy(b->req, path ? path : "", PATH_CAP - 1);
    b->req[PATH_CAP - 1] = 0;
    atomic_fetch_add_explicit(&b->req_gen, 1, memory_order_release);
}

int peak_bank_path(int slot, char *buf, int cap) {
    if (!ok_slot(slot) || cap < 1) return -1;
    int n = (int)strlen(g_bank[slot].req);
    if (n > cap - 1) n = cap - 1;
    memcpy(buf, g_bank[slot].req, (size_t)n);
    buf[n] = 0;
    return n;
}

int peak_bank_count(int slot) {
    if (!ok_slot(slot)) return 0;
    bank_t *b = &g_bank[slot];
    int s = atomic_load_explicit(&b->seq, memory_order_acquire);
    if (s & 1) return 0;
    int n = b->count;
    atomic_thread_fence(memory_order_acquire);
    return atomic_load_explicit(&b->seq, memory_order_relaxed) == s ? n : 0;
}

int peak_bank_ready(int slot) {
    if (!ok_slot(slot)) return 0;
    bank_t *b = &g_bank[slot];
    return atomic_load_explicit(&b->loaded_gen, memory_order_acquire) ==
           atomic_load_explicit(&b->req_gen, memory_order_acquire);
}

int peak_bank_patch(int slot, int idx, float *v, char name[PEAK_NAME_MAX]) {
    if (!ok_slot(slot)) return 0;
    bank_t *b = &g_bank[slot];
    int s = atomic_load_explicit(&b->seq, memory_order_acquire);
    if ((s & 1) || idx < 0 || idx >= b->count) return 0;
    memcpy(v, b->vals[idx], sizeof(float) * P_COUNT);
    memcpy(name, b->names[idx], PEAK_NAME_MAX);
    atomic_thread_fence(memory_order_acquire);
    return atomic_load_explicit(&b->seq, memory_order_relaxed) == s;
}

float peak_bank_vol(int slot, int idx) {
    if (!ok_slot(slot) || idx < 0 || idx >= PEAK_BANK_MAX) return -1.0f;
    atomic_store_explicit(&g_bank[slot].want, idx, memory_order_relaxed);
    return bitsf(atomic_load_explicit(&g_bank[slot].vol[idx], memory_order_acquire));
}

/* ------------------------------------------------------------------ worker */

static uint8_t g_file[PEAK_BANK_MAX * PEAK_MSG_LEN * 2]; /* worker only */
static int g_offs[PEAK_BANK_MAX];
static uint8_t g_done[CREST_MAX_INSTANCES][PEAK_BANK_MAX]; /* levelled */

static void nap(long ms) { /* in short slices so that stopping the worker never waits long */
    for (; ms > 0 && !atomic_load_explicit(&g_stop, memory_order_relaxed); ms -= 10) {
        struct timespec t = {0, 10 * 1000000L};
        nanosleep(&t, NULL);
    }
}

/* Read and convert the requested file into the slot (empty path or an unreadable file: empty bank). */
static void load(int slot) {
    bank_t *b = &g_bank[slot];
    char path[PATH_CAP];
    int g;
    for (;;) { /* a consistent copy of the request, even if it changes meanwhile */
        g = atomic_load_explicit(&b->req_gen, memory_order_acquire);
        memcpy(path, b->req, PATH_CAP);
        path[PATH_CAP - 1] = 0;
        if (atomic_load_explicit(&b->req_gen, memory_order_acquire) == g) break;
    }
    int len = 0;
    if (path[0]) {
        FILE *f = fopen(path, "rb");
        if (f) {
            len = (int)fread(g_file, 1, sizeof g_file, f);
            fclose(f);
        }
    }
    int n = len > 0 ? peak_scan(g_file, len, g_offs, PEAK_BANK_MAX) : 0;
    atomic_fetch_add_explicit(&b->seq, 1, memory_order_acq_rel); /* odd: readers back off */
    for (int i = 0; i < n; i++) peak_convert(g_file + g_offs[i], b->vals[i], b->names[i]);
    for (int i = 0; i < PEAK_BANK_MAX; i++) atomic_store_explicit(&b->vol[i], fbits(-1.0f), memory_order_relaxed);
    b->count = n;
    atomic_fetch_add_explicit(&b->seq, 1, memory_order_release);
    memset(g_done[slot], 0, sizeof g_done[slot]);
    atomic_store_explicit(&b->loaded_gen, g, memory_order_release);
}

/* Solve the loudness of one patch of the slot, the wanted one first. Returns 1 if there was work. */
static int level_one(int slot) {
    bank_t *b = &g_bank[slot];
    int n = b->count; /* only the worker writes it */
    if (n == 0) return 0;
    int idx = atomic_load_explicit(&b->want, memory_order_relaxed);
    if (idx < 0 || idx >= n || g_done[slot][idx]) {
        idx = -1;
        for (int i = 0; i < n && idx < 0; i++)
            if (!g_done[slot][i]) idx = i;
    }
    if (idx < 0) return 0;
    double vol = crest_level_solve(g_api, b->vals[idx], 1);
    if (vol < 0.0) { /* no free instance to render on: try again in a while */
        nap(1000);
        return 1;
    }
    g_done[slot][idx] = 1;
    atomic_store_explicit(&b->vol[idx], fbits((float)vol), memory_order_release);
    return 1;
}

static void *worker(void *arg) {
    (void)arg;
#ifdef __linux__
    /* Created from the loader thread, so already SCHED_OTHER; also keep core 3 free for the audio. */
    struct sched_param sp = {.sched_priority = 0};
    sched_setscheduler(0, SCHED_OTHER, &sp);
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(0, &set);
    CPU_SET(1, &set);
    CPU_SET(2, &set);
    sched_setaffinity(0, sizeof set, &set);
#endif
    static int seen[CREST_MAX_INSTANCES];
    while (!atomic_load_explicit(&g_stop, memory_order_relaxed)) {
        int busy = 0;
        for (int s = 0; s < CREST_MAX_INSTANCES; s++) {
            int g = atomic_load_explicit(&g_bank[s].req_gen, memory_order_acquire);
            if (g != seen[s]) {
                seen[s] = g;
                load(s);
                busy = 1;
            }
        }
        if (!busy) /* one patch per pass, so a new request is never kept waiting long */
            for (int s = 0; s < CREST_MAX_INSTANCES && !busy; s++) busy = level_one(s);
        if (!busy) nap(50);
    }
    return NULL;
}

void peak_bank_start(const plugin_api_v2_t *api) {
    if (atomic_exchange(&g_started, 1)) return;
    g_api = api;
    pthread_attr_t at;
    pthread_attr_init(&at);
    pthread_attr_setstacksize(&at, 256 * 1024);
    atomic_store(&g_stop, 0);
    if (pthread_create(&g_thread, &at, worker, NULL) != 0) atomic_store(&g_started, 0);
    pthread_attr_destroy(&at);
}

/* The host unloads dsp.so (dlclose) when a project switch leaves no Crest on a track. A worker still
 * running then executes unmapped code and takes Move down, so join it before the library goes. */
__attribute__((destructor)) static void peak_bank_stop(void) {
    if (!atomic_exchange(&g_started, 0)) return;
    atomic_store(&g_stop, 1);
    pthread_join(g_thread, NULL);
}

#endif
