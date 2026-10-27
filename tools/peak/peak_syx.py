"""Decoder for Novation Peak SysEx bank dumps (128 patches of 527 bytes each).

The patch layout is not published by Novation. It was derived from the NRPN/CC table in the Peak 1.2
manual (appendix "MIDI parameters list"), the "Init Patch" parameter table, and per-byte statistics
over 1024 patches (see plan/18-peak-import.md). Offsets are absolute within one 527-byte message.
A "pair" is a 14-bit style value: first byte = high 7 bits, second byte bit 6 = LSB (value 0..255).
"""

MSG_LEN = 527
NAME = (16, 32)

# single-byte fields: name -> offset
BYTE = {
    "category": 32,
    "voice_mode": 48, "unison": 49, "uni_detune": 50, "uni_spread": 51, "octave": 52, "glide": 53,
    "preglide": 54, "glide_on": 55, "divergence": 57, "drift": 58, "noise_lpf": 59, "noise_hpf": 60,
    "osc_keysync": 61,
    "patch_level": 135, "vca_gain": 136, "fx_dry": 137, "fx_wet": 138,
    "f_overdrive": 139, "f_postdrive": 140, "f_slope": 141, "f_shape": 142, "f_keytrack": 143, "f_res": 144,
    "f_osc3": 149, "f_envsel": 150, "f_ampenv": 151, "f_modenv1": 152, "f_divergence": 153,
    "ae_a": 160, "ae_d": 161, "ae_s": 162, "ae_r": 163, "ae_vel": 164, "ae_trig": 165,
    "me1_a": 169, "me1_d": 170, "me1_s": 171, "me1_r": 172, "me1_vel": 173, "me1_trig": 174,
    "me2_a": 177, "me2_d": 178, "me2_s": 179, "me2_r": 180, "me2_vel": 181, "me2_trig": 182,
    "fx_dist_lvl": 213, "fx_bypass": 215, "fx_routing": 216,
    "dly_lvl": 218, "dly_time": 219, "dly_ratio": 220, "dly_width": 221, "dly_sync": 222, "dly_synctime": 223, "dly_fb": 224,
    "dly_lp": 225, "dly_hp": 226, "dly_slew": 227,
    "rev_lvl": 231, "rev_type": 232, "rev_time": 233, "rev_lp": 234, "rev_hp": 235, "rev_size": 236,
    "rev_mod": 237, "rev_modrate": 238, "rev_lowpass": 239, "rev_highpass": 240, "rev_pre": 241,
    "cho_lvl": 243, "cho_type": 244, "cho_rate": 245, "cho_depth": 246, "cho_fb": 247, "cho_lp": 248,
    "cho_hp": 249,
    "arp_sync": 253, "arp_type": 254, "arp_rhythm": 255, "arp_oct": 256, "arp_gate": 257, "arp_swing": 258,
    "arp_on": 259, "arp_latch": 260, "arp_keysync": 261,
    "anim1_hold": 263, "anim2_hold": 264, "mm_sel": 265,
}
PAIR = {
    "mx_o1": 125, "mx_o2": 127, "mx_o3": 129, "mx_ring": 131, "mx_noise": 133,
    "f_freq": 145, "f_lfo1": 147, "arp_rate": 251,
}
# Firmware 1.2 additions, absent from the manual's MIDI list. Located from byte statistics over the
# 529 patches (plan/18): each holds values in the manual's range for that parameter and is 0 in the
# Init-like patches. Hold 0..127 (= 0..500 ms), Repeats 0 = Off, 1..126, 127 = On.
ENV_EXTRA = {"ae": (166, 167), "me1": (175, 176), "me2": (183, 184)}  # (hold, repeats)
GLFO_BASE = (362, 365)        # LFO 3 and 4: wave, rate (0..127), sync (0 = Off, n = Peak table entry n)
OSC_BASE = (62, 83, 104)      # 21-byte blocks
LFO_BASE = (185, 199)         # 14-byte blocks
MM_BASE, MM_SLOTS = 266, 16   # (src1, src2, depth, dest) x16
FXMM_BASE, FXMM_SLOTS = 330, 4


def pair(p, o):
    return p[o] * 2 + ((p[o + 1] >> 6) & 1)


def decode(p):
    """One 527-byte message -> dict of raw Peak values (all integers, as stored)."""
    assert len(p) == MSG_LEN and p[0] == 0xF0 and p[-1] == 0xF7
    d = {"name": bytes(p[NAME[0]:NAME[1]]).decode("ascii", "replace").rstrip()}
    for k, o in BYTE.items():
        d[k] = p[o]
    for k, o in PAIR.items():
        d[k] = pair(p, o)
    d["osc"] = []
    for b in OSC_BASE:
        d["osc"].append(dict(
            range=p[b], coarse=pair(p, b + 1), fine=pair(p, b + 3), me2_pitch=p[b + 5], lfo2_pitch=pair(p, b + 6),
            wave=p[b + 8], wavemore=p[b + 9], shsrc=p[b + 10], shape=p[b + 11], me1_shape=p[b + 12],
            lfo1_shape=p[b + 13], vsync=p[b + 14], dens=p[b + 15], ddet=p[b + 16], fixed=p[b + 17],
            bend=p[b + 18]))
    d["lfo"] = []
    for b in LFO_BASE:
        d["lfo"].append(dict(
            range=p[b], rate=pair(p, b + 1), sync=p[b + 3], wave=p[b + 4], phase=p[b + 5], slew=p[b + 6],
            fade=p[b + 7], fademode=p[b + 8], fadesync=p[b + 9], montrig=p[b + 10], repeats=p[b + 11],
            common=p[b + 12]))
    for k, (ho, ro) in ENV_EXTRA.items():
        d[k + "_hold"], d[k + "_rep"] = p[ho], p[ro]
    d["glfo"] = [dict(wave=p[b], rate=p[b + 1], sync=p[b + 2]) for b in GLFO_BASE]
    d["mm"] = [dict(s1=p[o], s2=p[o + 1], depth=p[o + 2], dest=p[o + 3])
               for o in (MM_BASE + 4 * i for i in range(MM_SLOTS))]
    d["fxmm"] = [dict(s1=p[o], s2=p[o + 1], depth=p[o + 2], dest=p[o + 3])
                 for o in (FXMM_BASE + 4 * i for i in range(FXMM_SLOTS))]
    return d


def read_bank(path):
    data = open(path, "rb").read()
    assert len(data) % MSG_LEN == 0, "%s: not a Peak bank dump" % path
    return [decode(data[i:i + MSG_LEN]) for i in range(0, len(data), MSG_LEN)]
