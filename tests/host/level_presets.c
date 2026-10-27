/* Loudness calibration for imported presets.
 *   build/crest_level <first_index> [last_index]     prints "<index> <patch_vol>" per preset
 * The solve itself is src/dsp/level.c (shared with the browser import and banks loaded on the Move):
 * each preset is rendered as a held chord (or a short phrase for mono modes) at a probe volume, then
 * patch_vol is solved for a peak of -3 dBFS or an RMS of -17 dBFS, whichever is quieter.
 * tools/peak/level.py writes the result back into the presets/peak bank files.
 * CREST_SET="key=value,key=value" applies overrides after the preset loads (for debugging). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "crest.h"
#include "level.h"

int main(int argc, char **argv) {
    int first = argc > 1 ? atoi(argv[1]) : 0;
    host_api_v1_t host;
    memset(&host, 0, sizeof host);
    plugin_api_v2_t *api = move_plugin_init_v2(&host);
    int last = argc > 2 ? atoi(argv[2]) + 1 : CREST_FACTORY_COUNT;
    static float v[P_COUNT];
    for (int i = first; i < last && i < CREST_FACTORY_COUNT; i++) {
        memcpy(v, CREST_FACTORY_VALUES[i], sizeof v);
        const char *ov = getenv("CREST_SET");
        if (ov) { /* apply the overrides through set_param on a scratch instance, then copy the values out */
            void *h = api->create_instance(".", "");
            char s[16], tmp[512], *sp = NULL;
            snprintf(s, sizeof s, "%d", i);
            api->set_param(h, "preset", s);
            snprintf(tmp, sizeof tmp, "%s", ov);
            for (char *t = strtok_r(tmp, ",", &sp); t; t = strtok_r(NULL, ",", &sp)) {
                char *eq = strchr(t, '=');
                if (!eq) continue;
                *eq = 0;
                api->set_param(h, t, eq + 1);
            }
            memcpy(v, ((crest_inst_t *)h)->v, sizeof v);
            api->destroy_instance(h);
        }
        printf("%d %.4f\n", i, crest_level_solve(api, v, 0));
    }
    return 0;
}
