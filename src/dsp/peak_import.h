/* Novation Peak SysEx patches -> Crest parameter values (plan/18).
 *
 * The C twin of tools/peak/peak_syx.py + import_banks.py, used where Python is not: the browser build
 * (crest.wasm) and banks loaded on the Move (peak_bank.c). tests/peak/test_import.py checks that both
 * give the same values for every patch in reference_novation/. Not realtime code: call it from a
 * worker or the browser's main thread. */
#ifndef CREST_PEAK_IMPORT_H
#define CREST_PEAK_IMPORT_H

#include <stdint.h>

#include "params_gen.h"

#define PEAK_MSG_LEN 527
#define PEAK_BANK_MAX 128
#define PEAK_NAME_MAX 17 /* 16 characters + NUL */

/* Find the Peak patch messages (527-byte F0 ... F7) in a file's bytes: a bank dump (128), a single
 * patch, or any mix. Writes up to max byte offsets; returns how many were found. */
int peak_scan(const uint8_t *buf, int len, int *offs, int max);

/* Convert one message: v[P_COUNT] receives the Crest values (defaults for anything the Peak patch
 * does not set; patch_vol from the Peak's Patch Level, before any loudness solve). name receives the
 * patch name, trimmed, "Unnamed" when blank. */
void peak_convert(const uint8_t *msg, float *v, char name[PEAK_NAME_MAX]);

#endif
