#!/usr/bin/env python3
"""Static realtime-safety check of the built dsp.so (plan/14 section 4).

Reads the ELF dynamic symbol table without external tools and verifies:
  * the file is a 64-bit little-endian aarch64 shared object,
  * it exports move_plugin_init_v2,
  * every undefined symbol is on the whitelist (no malloc, file I/O, printf, threads...).
"""
import struct
import sys

ALLOWED = {
    # string / memory
    "memcpy", "memset", "memmove", "memcmp", "strcmp", "strncmp", "strlen", "strcpy", "strncpy",
    # number parsing / formatting (no allocation, no I/O)
    "strtof", "strtol", "atoi", "snprintf",
    # libm
    "sinf", "cosf", "sincosf", "tanf", "expf", "exp2f", "logf", "powf", "sqrtf", "floorf", "fabsf", "roundf",
    "lrintf", "log1pf", "fminf", "fmaxf", "tanhf",
    # compiler / runtime plumbing
    "__stack_chk_fail", "__stack_chk_guard", "__cxa_finalize", "__gmon_start__",
    "_ITM_deregisterTMCloneTable", "_ITM_registerTMCloneTable", "__errno_location",
    "__isnanf", "__isinff", "__finitef", "__fpclassifyf",
    # clock for the debug-only cpu_us key
    "clock_gettime",
}
# Used only by the Peak bank worker thread (src/dsp/peak_bank.c), never on the audio callback: it is
# started from the loader thread, reads the chosen .syx file, and solves loudness offline (level.c).
WORKER = {
    "pthread_create", "pthread_attr_init", "pthread_attr_destroy", "pthread_attr_setdetachstate",
    "pthread_attr_setstacksize", "pthread_join", "sched_setscheduler", "sched_setaffinity", "nanosleep",
    "fopen", "fopen64", "fread", "fclose",
    # double-precision maths of the converter and the loudness solve
    "pow", "exp2", "log", "log10", "sqrt", "cbrt", "floor", "fmod", "fmin", "fabs",
}
ALLOWED |= WORKER


def main(path):
    d = open(path, "rb").read()
    if d[:4] != b"\x7fELF" or d[4] != 2 or d[5] != 1:
        sys.exit("%s: not a 64-bit little-endian ELF" % path)
    e_type, e_machine = struct.unpack_from("<HH", d, 16)
    if e_machine != 183:
        sys.exit("%s: machine %d is not aarch64 (183)" % (path, e_machine))
    if e_type != 3:
        sys.exit("%s: not a shared object" % path)
    e_shoff, = struct.unpack_from("<Q", d, 40)
    e_shentsize, e_shnum, e_shstrndx = struct.unpack_from("<HHH", d, 58)
    secs = []
    for i in range(e_shnum):
        o = e_shoff + i * e_shentsize
        name, typ, flags, addr, off, size, link, info, align, entsize = struct.unpack_from("<IIQQQQIIQQ", d, o)
        secs.append(dict(type=typ, off=off, size=size, link=link, entsize=entsize))
    dynsym = next((s for s in secs if s["type"] == 11), None)  # SHT_DYNSYM
    if dynsym is None:
        sys.exit("no .dynsym")
    strtab = secs[dynsym["link"]]
    undefined, exported = [], []
    for i in range(dynsym["size"] // dynsym["entsize"]):
        o = dynsym["off"] + i * dynsym["entsize"]
        st_name, st_info, st_other, st_shndx, st_value, st_size = struct.unpack_from("<IBBHQQ", d, o)
        if st_name == 0:
            continue
        s = strtab["off"] + st_name
        name = d[s:d.index(b"\0", s)].decode()
        (undefined if st_shndx == 0 else exported).append(name)
    bad = sorted(n for n in undefined if n not in ALLOWED)
    if "move_plugin_init_v2" not in exported:
        sys.exit("move_plugin_init_v2 is not exported")
    if bad:
        sys.exit("forbidden undefined symbols in dsp.so: " + ", ".join(bad))
    print("dsp.so ok: aarch64, exports move_plugin_init_v2, %d undefined symbols all whitelisted" % len(undefined))


if __name__ == "__main__":
    main(sys.argv[1])
