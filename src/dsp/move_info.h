/*
 * Public Move set-info reader copied from Schwung's src/host/move_info.h.
 * Used by Crest to resolve the arp's Scale = Set option against Move's root/scale.
 * Schwung: https://github.com/charlesvestal/schwung, MIT License,
 * Copyright (c) 2025-2026 Charles Vestal. See THIRD_PARTY_NOTICES.md.
 */
#ifndef MOVE_INFO_H
#define MOVE_INFO_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define MOVE_INFO_VERSION  1
#define MOVE_INFO_SHM_NAME "/schwung-move-info"
#define MOVE_INFO_MAGIC    "MOVEINF"
#define MOVE_INFO_TRACKS   4

typedef struct {
    char name[32];
    int16_t color_id;
    int8_t type;
    uint8_t muted, soloed, selected, _pad[2];
    float volume_db;
} move_info_track_t;

typedef struct {
    uint32_t size, version, changes;
    uint8_t valid, playing, metronome_on, midi_clock_sync, input_monitoring;
    int8_t root_note, selected_track, global_quant;
    uint8_t ts_upper, ts_lower, _pad[2];
    float tempo, groove, master_db;
    char scale[24];
    char global_quant_name[24];
    double song_beats;
    move_info_track_t track[MOVE_INFO_TRACKS];
} move_info_t;

typedef struct {
    char magic[8];
    uint32_t seq, reserved;
    move_info_t info;
} move_info_shm_t;

static inline int move_info_copy(const volatile move_info_shm_t *shm,
                                 move_info_t *out, size_t cap) {
    if (!shm || !out || cap < 8) return 0;
    if (memcmp((const void *)shm->magic, MOVE_INFO_MAGIC,
               sizeof MOVE_INFO_MAGIC) != 0) return 0;
    for (int tries = 0; tries < 8; tries++) {
        uint32_t s0 = __atomic_load_n(&shm->seq, __ATOMIC_ACQUIRE);
        if (s0 & 1) continue;
        size_t have = shm->info.size;
        if (have < 8 || have > 65536) return 0;
        size_t n = have < cap ? have : cap;
        memcpy(out, (const void *)&shm->info, n);
        if (n < cap) memset((char *)out + n, 0, cap - n);
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        if (__atomic_load_n(&shm->seq, __ATOMIC_RELAXED) == s0) return 1;
    }
    return 0;
}

#ifndef MOVE_INFO_NO_READER
#include <dlfcn.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#ifndef RTLD_DEFAULT
#define RTLD_DEFAULT ((void *)0)
#endif

typedef int (*move_info_fn)(move_info_t *out, size_t cap);

static inline int move_info_read(move_info_t *out) {
    static move_info_fn fn;
    static const volatile move_info_shm_t *shm;
    static int state;
    if (!out) return 0;
    if (state == 0) {
        fn = (move_info_fn)dlsym(RTLD_DEFAULT, "schwung_move_info");
        if (fn) state = 1;
        else {
            int fd = shm_open(MOVE_INFO_SHM_NAME, O_RDONLY, 0);
            if (fd >= 0) {
                void *p = mmap(NULL, sizeof(move_info_shm_t), PROT_READ,
                               MAP_SHARED, fd, 0);
                close(fd);
                if (p != MAP_FAILED) {
                    shm = (const volatile move_info_shm_t *)p;
                    state = 2;
                }
            }
            if (state == 0) state = 3;
        }
    }
    if (state == 1) return fn(out, sizeof *out);
    if (state == 2) return move_info_copy(shm, out, sizeof *out);
    return 0;
}
#endif

#endif
