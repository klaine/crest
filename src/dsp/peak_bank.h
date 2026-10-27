/* Peak SysEx banks loaded on the Move (plan/18 "Banks on the Move").
 *
 * The user picks a .syx file with the Peak Bank file browser (key syx_file); a worker thread reads and
 * converts it (peak_import.c) and then solves each patch's loudness (level.c), the selected patch
 * first. The audio thread only posts requests and copies finished patches out:
 *   - no file I/O, allocation or locks on the audio thread: requests are a path copy plus an atomic
 *     counter, results are read under a sequence counter (a torn read is retried on the next block);
 *   - the worker is started once from move_plugin_init_v2 / create_instance (the loader thread, not the
 *     callback), runs SCHED_OTHER off core 3, and polls every 50 ms.
 * Storage is static, one bank per pool slot. The browser build (CREST_WASM) has no worker: its import
 * runs in a Web Worker instead (web/peak_worker.js), and these calls are stubs. */
#ifndef CREST_PEAK_BANK_H
#define CREST_PEAK_BANK_H

#include "peak_import.h"
#include "plugin_api.h"

/* Start the worker (once per process; later calls do nothing). */
void peak_bank_start(const plugin_api_v2_t *api);

/* Audio thread: ask for the bank at path to be loaded into slot ("" empties it). */
void peak_bank_request(int slot, const char *path);
/* Audio thread: the path of the bank asked for (or loaded); returns its length. */
int peak_bank_path(int slot, char *buf, int cap);
/* Audio thread: copy patch idx out of a finished bank. Returns 1 on success, 0 when the bank is not
 * ready, idx is out of range, or the worker was writing (try again later). */
int peak_bank_patch(int slot, int idx, float *v, char name[PEAK_NAME_MAX]);
/* Audio thread: the solved patch_vol of patch idx, or < 0 while it is not known yet. Asking also
 * moves that patch to the front of the levelling queue. */
float peak_bank_vol(int slot, int idx);
/* Number of patches in the loaded bank (0 while loading or empty). */
int peak_bank_count(int slot);
/* 1 once the worker has finished the latest request (so patch/count describe that file). */
int peak_bank_ready(int slot);

#endif
