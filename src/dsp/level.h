/* Loudness calibration of a patch (plan/18 "Loudness"), shared by the factory tool
 * (tests/host/level_presets.c), the browser's Peak import worker and banks loaded on the Move.
 * Each measurement renders offline on its own fresh instance from the pool (created and destroyed
 * here), so it needs a free slot and must not run on the audio thread. */
#ifndef CREST_LEVEL_H
#define CREST_LEVEL_H

#include "plugin_api.h"

/* Render values v at patch volume vol the way the factory solve does (a held chord, or a three-note
 * phrase in the mono modes, 6 s in all) and report the peak and RMS in dBFS. */
int crest_level_measure(const plugin_api_v2_t *api, const float *v, double vol, double *pk_db, double *rms_db);

/* patch_vol for a peak of -3 dBFS or an RMS of -17 dBFS, whichever is quieter (0.02 .. 1). Below the
 * output clip the master gain is linear, so one probe usually settles it: with fast set, the probe is
 * repeated only when it ran hot. A silent patch keeps the probe volume. Returns < 0 when no instance
 * was free. */
double crest_level_solve(const plugin_api_v2_t *api, const float *v, int fast);

#endif
