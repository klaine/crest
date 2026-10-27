/* Convert Peak SysEx files with the C converter (src/dsp/peak_import.c) and print one JSON object per
 * patch: {"file":..., "index":..., "name":..., "values": [v0, v1, ...]} in parameter order.
 * tests/peak/test_import.py compares the result with the Python converter. */
#include <stdio.h>
#include <stdlib.h>

#include "peak_import.h"

int main(int argc, char **argv) {
    static uint8_t buf[1 << 20];
    static int offs[1024];
    for (int a = 1; a < argc; a++) {
        FILE *f = fopen(argv[a], "rb");
        if (!f) {
            fprintf(stderr, "cannot open %s\n", argv[a]);
            return 1;
        }
        int len = (int)fread(buf, 1, sizeof buf, f);
        fclose(f);
        int n = peak_scan(buf, len, offs, 1024);
        for (int i = 0; i < n; i++) {
            float v[P_COUNT];
            char name[PEAK_NAME_MAX];
            peak_convert(buf + offs[i], v, name);
            printf("{\"file\": %d, \"index\": %d, \"values\": [", a - 1, i);
            for (int k = 0; k < P_COUNT; k++) printf("%s%.6g", k ? "," : "", (double)v[k]);
            printf("], \"name\": \"");
            for (const char *c = name; *c; c++) printf(*c == '"' || *c == '\\' ? "\\%c" : "%c", *c);
            printf("\"}\n");
        }
    }
    return 0;
}
