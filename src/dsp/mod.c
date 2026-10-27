#include <string.h>

#include "crest.h"

/* Sum every active matrix slot and macro assignment into per-destination accumulators (plan/07 1).
 * A slot contributes depth * source1 * source2 (source 2 Off counts as 1). src holds MS_COUNT
 * source values; acc receives MD_COUNT sums in normalised destination units. */
void mod_eval(const bparams_t *bp, const float *src, float *acc) {
    memset(acc, 0, sizeof(float) * MD_COUNT);
    for (int i = 0; i < bp->nslots; i++) {
        const mm_slot_t *s = &bp->slot[i];
        float c = s->dep * src[s->s1];
        if (s->s2) c *= src[s->s2];
        acc[s->dst] += c;
    }
    for (int m = 0; m < 8; m++) {
        float mv = src[MS_MAC1 + m];
        if (mv == 0.0f) continue;
        for (int k = 0; k < 4; k++)
            if (bp->mac[m][k].dst) acc[bp->mac[m][k].dst] += mv * bp->mac[m][k].amt;
    }
}
