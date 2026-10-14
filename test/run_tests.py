"""LAB X-16 test suite: the common tests C1-C9 (docs/EFFECTS.md section 0.3), each effect's own T-tests, and P1, the
preset loudness trims.

Usage:  python -I test/run_tests.py --plugin <slug> [--plugin <slug> ...] | --all
                                    [--only C1,C2,T3,P1] [--skip-pluginval] [--fuzz-seconds 300] [--quick]
Renders go to test/out/<slug> (git-ignored); one JSON report per plugin lands in build/test-report-<slug>.json.
Every render uses fixed instance ids (the harness default), so hash01 decisions and seeded noise repeat exactly.
"""

import argparse
import json
import math
import pathlib
import re
import shutil
import subprocess
import sys
import time

import numpy as np
import soundfile as sf

ROOT = pathlib.Path(__file__).resolve().parent.parent
BUILD = ROOT / "build"
OUT = ROOT / "test" / "out"
sys.path.insert(0, str(ROOT / "test" / "python"))
import labanalyze as an  # noqa: E402

STAT = re.compile(r'(\w+)=("([^"]*)"|\S+)')
PLUGINVAL = shutil.which("pluginval") or r"F:\_DEV\tools\pluginval\pluginval.exe"


def effects():
    """slug -> name, product, code, version, from the lab_add_effect calls in CMakeLists.txt (stubs excluded)."""
    text = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    found = {}
    for m in re.finditer(r'lab_add_effect\(NAME (\w+) SLUG (\w+) PRODUCT "([^"]+)" CODE (\w+) VERSION ([\d.]+)', text):
        name, slug, product, code, version = m.groups()
        if not slug.startswith("stub"):
            found[slug] = {"name": name, "product": product, "code": code, "version": version}
    return found


def sets(*assignments):
    out = []
    for a in assignments:
        out += ["--set", a]
    return out


class Harness:
    def __init__(self, slug, info):
        self.slug = slug
        self.info = info
        self.exe = BUILD / f"Lx16{info['name']}Render_artefacts" / "Release" / f"Lx16{info['name']}Render.exe"
        self.bundle = BUILD / f"Lx16{info['name']}_artefacts" / "Release" / "VST3" / f"LAB X-16 {info['product']}.vst3"
        self.out = OUT / slug
        self.out.mkdir(parents=True, exist_ok=True)

    def run(self, args, timeout=3600):
        proc = subprocess.run([str(self.exe)] + [str(a) for a in args], capture_output=True, text=True, encoding="utf-8",
                              errors="replace", timeout=timeout)
        stats = {}
        for line in proc.stdout.splitlines():
            for key, value, quoted in STAT.findall(line):
                stats[key] = quoted if value.startswith('"') else value
        stats["exit"] = proc.returncode
        if proc.returncode != 0:
            stats["stderr"] = (proc.stderr.strip() or proc.stdout.strip())[-400:]
        return stats

    def render(self, name, args, timeout=3600):
        wav = self.out / f"{name}.wav"
        return wav, self.run(["--out", wav] + list(args), timeout)

    def params(self):
        proc = subprocess.run([str(self.exe), "--list-params"], capture_output=True, text=True, encoding="utf-8")
        presets = re.findall(r'^preset (\d+) "([^"]*)"', proc.stdout, re.M)
        return [name for _, name in presets]


def fixture(name, sr, seconds, parts):
    """A WAV in test/out/fixtures: parts are (start, end, kind, freq, level_db) with kind 'sine' or 'noise'."""
    path = OUT / "fixtures" / f"{name}.wav"
    path.parent.mkdir(parents=True, exist_ok=True)
    n = int(seconds * sr)
    x = np.zeros(n)
    rng = np.random.default_rng(12345)
    for start, end, kind, freq, level in parts:
        a, b = int(start * sr), int(end * sr)
        amp = 10 ** (level / 20)
        if kind == "sine":
            t = np.arange(b - a) / sr
            seg = np.sin(2 * math.pi * freq * t)
        else:
            seg = rng.uniform(-1.0, 1.0, b - a) * math.sqrt(3.0)   # white noise; level_db is its RMS
        ramp = min(int(0.002 * sr), (b - a) // 2)   # 2 ms edges: the burst itself must not click
        if ramp > 0:
            w = np.ones(b - a)
            w[:ramp] = np.linspace(0, 1, ramp)
            w[-ramp:] = np.linspace(1, 0, ramp)
            seg = seg * w
        x[a:b] += amp * seg
    sf.write(str(path), np.stack([x, x], axis=1), sr, subtype="FLOAT")
    return path


def mono_of(wav):
    data, sr = an.load(wav)
    return an.mono(data), data, sr


# ---------------------------------------------------------------------------------------------------------------------
# Per-effect description: guard stages (in order; OUTPUT is added), C2 settings that make the effect transparent,
# the spec's panel titles for C9, and the T-tests.

def kaymanov_tests(h, record, want):
    if want("T1"):
        dry, _ = h.render("t1_dry", ["--signal", "sine:32.703", "--seconds", "3"] + sets("bypass=1"))
        wet, st = h.render("t1_drive24", ["--signal", "sine:32.703", "--seconds", "3"] + sets("drive=24"))
        md, _, sr = mono_of(dry)
        mw, _, _ = mono_of(wet)
        diff = an.band_db(an.segment(mw, sr, 1, 3), sr, 20, 120) - an.band_db(an.segment(md, sr, 1, 3), sr, 20, 120)
        record("T1", "sub band below SPLIT within 0.05 dB of dry (C1 note, 24 dB drive)", abs(diff) <= 0.05,
               {"sub_diff_db": round(diff, 4)})
    if want("T2"):
        levels = []
        for d in (0, 12, 24, 36):
            wav, _ = h.render(f"t2_drive{d}", ["--signal", "sine:220", "--seconds", "2"] + sets(f"drive={d}"))
            m, _, sr = mono_of(wav)
            levels.append(round(an.band_db(an.segment(m, sr, 0.5, 2), sr, 500, 20000), 2))
        rising = all(b > a for a, b in zip(levels, levels[1:]))
        noise = []
        for d in (10, 20, 30, 40):
            wav, _ = h.render(f"t2_crush{d}", ["--signal", "sine:1000", "--seconds", "2"] + sets("mode=4", f"drive={d}"))
            m, _, sr = mono_of(wav)
            seg = an.segment(m, sr, 0.5, 2)
            noise.append(round(10 * math.log10(max(an.band_energy(seg, sr, 20, 20000) - an.band_energy(seg, sr, 950, 1050), 1e-30)), 2))
        steps = all(b > a for a, b in zip(noise, noise[1:]))
        record("T2", "harmonics above SPLIT rise with drive; crush noise steps up with drive", rising and steps,
               {"harmonics_db_at_0_12_24_36": levels, "crush_noise_db_at_10_20_30_40": noise})
    if want("T3"):
        # At the default 12 dB and at 24 dB. Past ~30 dB (plus the +12 dB pre-emphasis) a folded 5 kHz tone has harmonics
        # beyond 4x's own Nyquist, so no oversampling factor in the plugin can keep them out (docs/DESIGN.md).
        rows, ok = {}, True
        for drive in (12, 24):
            res = {}
            for os_ in (0, 2):
                wav, _ = h.render(f"t3_fold_d{drive}_os{os_}", ["--signal", "sine:5000", "--seconds", "2"]
                                  + sets("mode=2", f"drive={drive}", f"oversample={os_}"))
                m, _, sr = mono_of(wav)
                freqs, p = an.power_spectrum(an.segment(m, sr, 0.5, 2), sr, 16384)
                sel = (freqs >= 10000) & (freqs <= 20000)
                for hk in (10000, 15000, 20000):
                    sel &= np.abs(freqs - hk) > 150
                res[os_] = 10 * math.log10(float(p[sel].sum()) + 1e-30)
            rows[f"drive_{drive}"] = {"off_alias_db": round(res[0], 1), "os4_alias_db": round(res[2], 1), "cleaner_db": round(res[0] - res[2], 1)}
            ok = ok and res[0] - res[2] >= 8.0
        record("T3", "aliasing: folded 5 kHz, 4x at least 8 dB cleaner than Off above 10 kHz (drive 12 and 24 dB)", ok, rows)
    if want("T5"):
        wav, _ = h.render("t5_coolant", ["--signal", "sine:440", "--seconds", "8", "--bpm", "128"]
                          + sets("cool_shape=1", "cool_sync=2", "cool_target=1", "cool_depth=1", "drive=0"))
        m, _, sr = mono_of(wav)
        env = an.envelope(an.segment(m, sr, 1, 8), sr, 0.002)
        e = env - env.mean()
        ac = np.correlate(e, e, mode="full")[len(e) - 1:]
        lo, hi = int(0.3 / 0.002), int(0.7 / 0.002)
        k = lo + int(np.argmax(ac[lo:hi]))
        a, b, c = ac[k - 1], ac[k], ac[k + 1]
        period = (k + 0.5 * (a - c) / (a - 2 * b + c)) * 0.002
        record("T5", "COOLANT period at 128 BPM, 1/4 pump = 468.75 ms", abs(period - 0.46875) <= 0.46875 * 0.01,
               {"period_ms": round(period * 1000, 2)})


def dosimeter_tests(h, record, want):
    quiet = sets("crush_mix=0", "dust=0")
    if want("T1"):
        _, st = h.render("t1_rate", ["--signal", "silence", "--seconds", "20"] + quiet + sets("track=0", "dose=0.5"))
        rate = float(st.get("custom1", "0")) / 20.0
        record("T1", "click rate at TRACK 0 within 20 % of dose^2 x 400 /s over 20 s", abs(rate - 100.0) <= 20.0,
               {"clicks_per_s": round(rate, 1), "expected": 100.0})
    if want("T2"):
        wav, st = h.render("t2_silence_track1", ["--signal", "silence", "--seconds", "5"] + sets("track=1", "dose=0.5", "dust=1"))
        m, _, sr = mono_of(wav)
        silent = an.peak_db(m) < -140
        _, st2 = h.render("t2_silence_track05", ["--signal", "silence", "--seconds", "5"] + sets("track=0.5", "dose=0.5", "dust=1"))
        clicks = float(st2.get("custom1", "0")) / 5.0
        record("T2", "silence in: no dust or hiss; clicks continue only when TRACK < 1", silent and 30 <= clicks <= 70,
               {"track1_peak_db": round(an.peak_db(m), 1), "track05_clicks_per_s": round(clicks, 1)})
    if want("T3"):
        wav, _ = h.render("t3_crush4", ["--signal", "sine:441", "--level", "-6", "--seconds", "1"]
                          + sets("bits=4", "hold=1", "crush_mix=1", "dose=0", "dust=0"))
        data, _ = an.load(wav)
        steps = data * 8.0
        off_grid = float(np.abs(steps - np.round(steps)).max())
        values = sorted(set(np.round(steps.flatten()).astype(int).tolist()))
        record("T3", "crush: a -6 dBFS sine at 4 bits steps in 1/8 of full scale", off_grid < 1e-5 and len(values) >= 7,
               {"max_off_grid": off_grid, "levels_in_eighths": values})
    if want("T4"):
        level = {}
        for f in (125, 1000, 8000):
            wav, _ = h.render(f"t4_phone_{f}", ["--signal", f"sine:{f}", "--seconds", "2"] + quiet + sets("medium=3", "dose=0"))
            m, _, sr = mono_of(wav)
            level[f] = an.rms_db(an.segment(m, sr, 0.5, 2))
        low, high = level[1000] - level[125], level[1000] - level[8000]
        record("T4", "Phone: 125 Hz and 8 kHz (an octave past 250 Hz and 4 kHz) at least 24 dB down", low >= 24 and high >= 24,
               {"below_db": round(low, 1), "above_db": round(high, 1)})


def silo_tests(h, record, want):
    base = sets("damping=0", "duck=0", "modal=0", "predelay=0", "mix=1")
    if want("T1"):
        res = {}
        for decay in (1.2, 3.0):
            wav, _ = h.render(f"t1_rt60_{decay}", ["--signal", "impulse", "--level", "0", "--seconds", str(2.5 * decay + 1)]
                              + base + sets(f"decay={decay}"))
            m, _, sr = mono_of(wav)
            rt, _ = an.schroeder_rt60(m[int(0.1 * sr):], sr, -5, -35)
            res[decay] = rt
        ok = all(abs(rt - d) <= 0.15 * d for d, rt in res.items())
        record("T1", "RT60 within 15 % of DECAY at modal 0 (impulse, Schroeder T30)", ok,
               {f"rt60_at_{d}": round(rt, 3) for d, rt in res.items()})
    if want("T2"):
        burst = fixture("silo_burst", 48000, 3.0, [(0.2, 1.2, "noise", 0, -6.0)])
        wav, _ = h.render("t2_duck", ["--in", burst, "--level", "0", "--seconds", "3"]
                          + sets("decay=6", "duck=1", "duck_release=180", "modal=0", "mix=1", "damping=0.3"))
        m, _, sr = mono_of(wav)
        during = an.rms_db(an.segment(m, sr, 0.7, 1.2))
        after = an.rms_db(an.segment(m, sr, 1.45, 1.55))
        record("T2", "ducking: wet during a -6 dBFS burst at least 12 dB below its level 300 ms after (duck 1)",
               after - during >= 12.0, {"during_db": round(during, 1), "after_db": round(after, 1), "difference_db": round(after - during, 1)})
    if want("T3"):
        rows = []
        for level in range(1, 6):
            _, st = h.render(f"t3_level{level}", ["--signal", "silence", "--seconds", "0.1"] + sets(f"levels={level}"))
            rows.append((float(st.get("custom1", "0")), float(st.get("custom2", "0"))))
        ok = all(b[0] > a[0] and b[1] > a[1] for a, b in zip(rows, rows[1:]))
        record("T3", "LEVEL 1-5: line lengths strictly increase", ok,
               {"shortest_longest_samples": [[round(a, 1), round(b, 1)] for a, b in rows]})
    if want("T4"):
        worst = 0.0
        found = {}
        for length in (1.0, 1.5):
            wav, _ = h.render(f"t4_tunnel_{length}", ["--signal", "impulse", "--level", "0", "--seconds", "0.5"]
                              + sets("tunnel=1", "tunnel_feedback=0", "duck=0", "mix=1", f"tunnel_length={length}"))
            m, _, sr = mono_of(wav)
            expected = [0.1 + ms * 0.001 * length for ms in (23, 41, 67, 89, 113, 151)]
            peaks = an.local_peaks(m, sr, expected, 0.003)
            errs = [abs(p - e) * 1000 for p, e in zip(peaks, expected)]
            worst = max(worst, max(errs))
            found[str(length)] = [round((p - 0.1) * 1000, 3) for p in peaks]
        record("T4", "TUNNEL tap times within 0.1 ms of the table x LENGTH", worst <= 0.1,
               {"tap_ms": found, "worst_error_ms": round(worst, 4)})
    if want("T5"):
        wav, st = h.render("t5_stopped_tail", ["--signal", "impulse", "--level", "0", "--seconds", "14", "--stopped", "--block", "64",
                                               "--program", "5"] + sets("mix=1"))
        m, _, sr = mono_of(wav)
        last = an.rms_db(an.segment(m, sr, 13, 14))
        selftest = h.run(["--selftest"])
        ok = last < -100 and selftest["exit"] == 0 and int(st.get("faults", "1")) == 0
        record("T5", "tail decays after the transport stops (64-sample blocks); delay self-test clean", ok,
               {"last_second_db": round(last, 1), "selftest_mono": selftest.get("selftest_delay_mono"),
                "selftest_stereo": selftest.get("selftest_delay_stereo")})


def rainbow_tests(h, record, want):
    clean = sets("motion=0", "damaged=0", "feedback=0", "mix=1")
    if want("T1"):
        worst = 0.0
        found = {}
        for pattern, quarters in ((1, (0.5, 0.75, 1.25, 2.0, 4.0)), (2, (1.0, 1.618, 2.618, 4.236, 6.854))):
            wav, _ = h.render(f"t1_taps_p{pattern}", ["--signal", "impulse", "--level", "0", "--seconds", "4", "--bpm", "120"]
                              + clean + sets(f"pattern={pattern}"))
            m, _, sr = mono_of(wav)
            expected = [0.1 + q * 0.5 for q in quarters]
            peaks = an.local_peaks(m, sr, expected, 0.005)
            errs = [abs(p - e) * 1000 for p, e in zip(peaks, expected)]
            worst = max(worst, max(errs))
            found[str(pattern)] = [round((p - 0.1) * 1000, 3) for p in peaks]
        record("T1", "tap times at 120 BPM within 0.1 ms of the pattern (Antenna, Golden)", worst <= 0.1,
               {"tap_ms": found, "worst_error_ms": round(worst, 4)})
    if want("T2"):
        burst = fixture("rainbow_burst", 48000, 3.5, [(0.1, 0.15, "sine", 1000, -12.0)])
        wav, _ = h.render("t2_feedback", ["--in", burst, "--level", "0", "--seconds", "3.5", "--bpm", "120"]
                          + sets("tap1_level=0", "tap2_level=0", "tap3_level=0", "tap4_level=0", "tap5_level=1", "tap5_sync=2",
                                 "feedback=0.5", "damaged=0", "motion=0", "fb_source=0", "mix=1"))
        m, _, sr = mono_of(wav)
        levels = [an.rms_db(an.segment(m, sr, 0.1 + 0.5 * k, 0.15 + 0.5 * k)) for k in range(1, 6)]
        diffs = [round(b - a, 2) for a, b in zip(levels, levels[1:])]
        record("T2", "feedback 0.5, tap 5 at one beat: each repeat -6 dB +-0.3", all(abs(d + 6.0) <= 0.3 for d in diffs),
               {"repeat_db": [round(v, 2) for v in levels], "step_db": diffs})
    if want("T3"):
        level = {}
        for f in (150, 1000, 6000):
            wav, _ = h.render(f"t3_damaged_{f}", ["--signal", f"sine:{f}", "--seconds", "6", "--bpm", "120"]
                              + sets("tap1_level=0", "tap2_level=0", "tap3_level=0", "tap4_level=0", "tap5_level=1",
                                     "damaged=1", "feedback=0", "motion=0", "mix=1"))
            m, _, sr = mono_of(wav)
            level[f] = an.rms_db(an.segment(m, sr, 2.5, 6))
        low, high = level[1000] - level[150], level[1000] - level[6000]
        record("T3", "DAMAGED tap: an octave outside 300-3000 Hz at least 18 dB below inside", low >= 18 and high >= 18,
               {"below_db": round(low, 1), "above_db": round(high, 1)})
    if want("T4"):
        rows = {}
        ok = True
        for preset in (1, 4):
            _, probe = h.render(f"t4_probe_{preset}", ["--signal", "silence", "--seconds", "0.1", "--program", str(preset)] + sets("mix=1"))
            tail = float(probe.get("tail_s", "0"))
            wav, _ = h.render(f"t4_tail_{preset}", ["--signal", "impulse", "--level", "0", "--seconds", str(tail + 3),
                                                    "--program", str(preset)] + sets("mix=1"))
            m, _, sr = mono_of(wav)
            env = an.envelope(m, sr, 0.01)
            above = np.where(20 * np.log10(env / (env.max() + 1e-30) + 1e-30) > -60)[0]
            measured = (above[-1] + 1) * 0.01 - 0.1 if len(above) else 0.0
            rows[str(preset)] = {"reported_s": round(tail, 2), "measured_s": round(measured, 2)}
            ok = ok and tail >= measured
        record("T4", "tail reported >= measured decay to -60 dB", ok, rows)


def wish_granter_tests(h, record, want):
    burst = fixture("wish_burst", 48000, 31.0, [(0.1, 0.4, "sine", 440, -12.0)])
    if want("T1"):
        rows = {}
        ok = True
        for iv, name in enumerate(("octave", "fifth", "octave_fifth", "major", "minor", "sus2", "lydian")):
            wav, _ = h.render(f"t1_shimmer_{name}", ["--in", burst, "--level", "0", "--seconds", "31"]
                              + sets("shimmer=1", "decay=1", "mix=1", f"shimmer_interval={iv}"))
            m, _, sr = mono_of(wav)
            secs = an.windows_db(m, sr, 1, 31, 1.0)
            rise = max(b - a for a, b in zip(secs, secs[1:]))
            rows[name] = {"max_rise_db": round(rise, 2), "last_db": round(secs[-1], 1)}
            ok = ok and rise <= 0.5 and secs[-1] <= -95
        record("T1", "every interval at shimmer 1, decay 1: no 1 s window louder than the last (0.5 dB); -95 dB by 30 s", ok, rows)
    if want("T2"):
        noise = fixture("wish_noise", 48000, 35.0, [(0.2, 2.2, "noise", 0, -12.0)])
        wav, _ = h.render("t2_freeze", ["--in", noise, "--level", "0", "--seconds", "35", "--step", "freeze=1@2.5"] + sets("mix=1"))
        m, _, sr = mono_of(wav)
        secs = an.windows_db(m, sr, 4, 34, 1.0)
        spread = max(secs) - min(secs)
        record("T2", "FREEZE: level within +-1 dB over 30 s with the input removed", spread <= 2.0,
               {"min_db": round(min(secs), 2), "max_db": round(max(secs), 2), "spread_db": round(spread, 2)})
    if want("T3"):
        burst4 = fixture("wish_burst_short", 48000, 5.0, [(0.1, 0.4, "sine", 440, -12.0)])
        spectra = {}
        for label, extra in (("none", sets("shimmer=0")), ("octave", sets("shimmer=0.8", "shimmer_interval=0")),
                             ("fifth", sets("shimmer=0.8", "shimmer_interval=1"))):
            wav, _ = h.render(f"t3_{label}", ["--in", burst4, "--level", "0", "--seconds", "5"] + sets("mix=1") + extra)
            m, _, sr = mono_of(wav)
            seg = an.segment(m, sr, 1.5, 4.0)
            spectra[label] = (an.tone_db(seg, sr, 880, 0.02), an.tone_db(seg, sr, 660, 0.02))
        octave = spectra["octave"][0] - spectra["none"][0]
        fifth = spectra["fifth"][1] - spectra["none"][1]
        record("T3", "shimmer spectrum: 880 Hz (Octave) and 660 Hz (Fifth) at least 12 dB over the no-shimmer tail",
               octave >= 12 and fifth >= 12, {"octave_gain_db": round(octave, 1), "fifth_gain_db": round(fifth, 1)})
    if want("T4"):
        onsets = {}
        for pd in (0, 100):
            wav, _ = h.render(f"t4_predelay_{pd}", ["--signal", "impulse", "--level", "0", "--seconds", "1"]
                              + sets("mix=1", "shimmer=0", f"predelay={pd}"))
            m, _, sr = mono_of(wav)
            onsets[pd] = an.onset(m, sr, -60.0)
        diff = (onsets[100] - onsets[0]) * 48000
        record("T4", "pre-delay exact (impulse onsets 0 vs 100 ms)", abs(diff - 4800) <= 1.0,
               {"onset_difference_samples": round(diff, 2), "expected": 4800})


SPEC = {
    "kaymanov": {"stages": ["shaper"], "panels": ["EMITTER", "COOLANT", "SPLIT", "HEAT"],
                 "transparent": [(f"os{o}_mode{m}", sets("drive=0", f"oversample={o}", f"mode={m}")) for o in range(3) for m in range(5)],
                 "tests": kaymanov_tests},
    "dosimeter": {"stages": ["crush"], "panels": [], "transparent": [("clean", sets("dose=0", "dust=0", "crush_mix=0"))],
                  "fuzz": ["--no-growth-check"], "tests": dosimeter_tests},
    "silo": {"stages": ["fdn"], "panels": [], "transparent": [("fdn", sets("mix=0")), ("tunnel", sets("mix=0", "tunnel=1"))],
             "tests": silo_tests},
    "rainbow": {"stages": ["taps"], "panels": [], "transparent": [("dry", sets("mix=0"))], "fuzz": ["--growth", "peak"],
                "tests": rainbow_tests},
    "wish_granter": {"stages": ["fdn"], "panels": [], "transparent": [("dry", sets("mix=0")), ("frozen", sets("mix=0", "freeze=1"))],
                     "tests": wish_granter_tests},
}


def common_tests(h, spec, presets, record, want, args):
    if want("C1"):
        diffs = {}
        for label, extra in (("default", []), ("last_preset", ["--program", str(len(presets) - 1)])):
            _, st = h.render(f"c1_null_{label}", ["--null-check", "--seconds", "3", "--signal", "drums:120"] + extra)
            diffs[label] = float(st.get("null_max_diff", "1"))
        record("C1", "null at BYPASS On (default and last preset)", all(d <= 1e-6 for d in diffs.values()), diffs)

    if want("C2"):
        bad, reported = [], set()
        for sr in (44100, 48000, 96000):
            for label, extra in spec["transparent"]:
                _, st = h.render("c2_latency", ["--latency-check", "--seconds", "1", "--sr", str(sr)] + extra)
                rep, meas = int(st.get("latency_reported", "-1")), int(st.get("latency_measured", "-2"))
                reported.add((sr, rep))
                if rep != meas:
                    bad.append({"sr": sr, "setting": label, "reported": rep, "measured": meas})
        fz = h.run(["--fuzz", "--seconds", "60", "--seed", "3"])
        changes = int(fz.get("latency_changes", "-1"))
        record("C2", "latency: reported = measured (44.1/48/96 kHz, every mode); constant through a 60 s fuzz",
               not bad and changes == 0, {"mismatches": bad, "reported": sorted(reported), "fuzz_latency_changes": changes})

    if want("C3"):
        st = h.run(["--roundtrip"])
        record("C3", "state round trip", st.get("roundtrip") == "PASS",
               {k: st.get(k) for k in ("param_mismatches", "state_identical", "callsign_ok", "state_bytes")})

    if want("C4"):
        seq = list(range(1, len(presets))) + [0]
        times = [0.6 + 0.6 * i for i in range(len(seq))]
        wav, st = h.render("c4_preset_switch", ["--signal", "drums:120", "--seconds", str(times[-1] + 1.0), "--program-at",
                                                ",".join(f"{p}@{t:.2f}" for p, t in zip(seq, times))])
        data, sr = an.load(wav)
        d = np.abs(np.diff(data, axis=0)).max(axis=1)
        mask = np.zeros(len(d), bool)
        for t in times:
            mask[max(0, int((t - 0.01) * sr)):int((t + 0.06) * sr)] = True
        at_switch, steady = float(d[mask].max()), float(d[~mask].max())
        ok = int(st.get("faults", "1")) == 0 and (at_switch < 0.5 or at_switch <= steady * 1.05)
        record("C4", "preset switch during signal: jump below 0.5 (or no larger than the presets' own), no fault", ok,
               {"jump_at_switch": round(at_switch, 4), "jump_steady": round(steady, 4), "faults": st.get("faults")})

    if want("C5"):
        rows, ok = [], True
        seconds = args.fuzz_seconds
        for sr in (44100, 48000):
            for block in ("1", "7", "64", "3000", "vary"):
                extra = ["--vary-blocks"] if block == "vary" else ["--block", block]
                started = time.time()
                st = h.run(["--fuzz", "--seconds", str(seconds), "--seed", str(7 + len(rows)), "--sr", str(sr)] + extra
                           + spec.get("fuzz", []))
                good = (st["exit"] == 0 and int(st.get("non_finite", "1")) == 0 and int(st.get("faults", "1")) == 0
                        and int(st.get("growth_silence", "1")) == 0)
                ok = ok and good
                rows.append({"sr": sr, "block": block, "ok": good, "faults": st.get("faults"), "non_finite": st.get("non_finite"),
                             "growth": st.get("growth_silence"), "peak_db": st.get("peak_db"), "wall_s": round(time.time() - started, 1)})
        record("C5", f"fuzz {seconds:g} s x (1, 7, 64, 3000, variable blocks) x (44.1, 48 kHz): no fault, non-finite or growth", ok,
               {"runs": rows})

    if want("C6"):
        log = h.out / "c6_faults.log"
        if log.exists():
            log.unlink()
        names = spec["stages"] + ["output"]
        rows, ok = [], True
        for i, name in enumerate(names):
            _, st = h.render(f"c6_inject_{name}", ["--signal", "drums:120", "--seconds", "2", "--inject", f"{i}@1.0", "--fault-log", log])
            good = int(st.get("faults", "0")) >= 1 and int(st.get("non_finite", "1")) == 0 and float(st.get("peak_db", "99")) < 6.0
            ok = ok and good
            rows.append({"stage": name, "faults": st.get("faults"), "peak_db": st.get("peak_db")})
        text = log.read_text(encoding="utf-8") if log.exists() else ""
        logged = sorted(set(re.findall(r"FAULT\s+(\S+)", text)))
        ok = ok and all(n.upper() in logged for n in names)
        record("C6", "guard: an injection at every stage is caught, silenced and logged by name", ok, {"runs": rows, "logged": logged})

    if want("C7") and not args.skip_pluginval:
        rows, ok = {}, True
        for level in (5, 10):
            proc = subprocess.run([PLUGINVAL, "--strictness-level", str(level), "--validate", str(h.bundle)],
                                  capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=3600)
            (h.out / f"pluginval-{level}.log").write_text(proc.stdout + proc.stderr, encoding="utf-8")
            rows[str(level)] = proc.returncode
            ok = ok and proc.returncode == 0
        record("C7", "pluginval strictness 5 and 10", ok, {"exit_codes": rows, "bundle": str(h.bundle.name)})

    if want("C8"):
        rows = {}
        for p, name in enumerate(presets):
            st = h.run(["--program", str(p), "--seconds", "30", "--block", "64", "--sr", "48000", "--signal", "drums:120"])
            rows[name] = round(float(st.get("realtime_x", "0")), 1)
        worst = min(rows.values())
        record("C8", "CPU: at least 20x real time at 48 kHz, 64-sample blocks, worst preset", worst >= 20.0,
               {"worst_x": worst, "per_preset_x": rows})

    if want("C9"):
        png = h.out / "editor.png"
        st = h.run(["--png", png])
        titles = st.get("panels", "")
        missing = [t for t in spec["panels"] if t not in titles]
        ok = st.get("ok") == "1" and st.get("size") == "960x560" and not missing and len(titles) > 0
        record("C9", "editor PNG renders; every panel title in the spec appears", ok,
               {"size": st.get("size"), "panels": titles, "missing": missing})


def loudness_test(h, presets, record, want):
    if not want("P1"):
        return
    _, ref = h.render("p1_input", ["--signal", "drums:120", "--seconds", "8"] + sets("bypass=1"))
    ref_db = float(ref.get("rms_db", "-100"))
    rows, ok = {}, True
    for p, name in enumerate(presets):
        if p == 0:
            continue   # Init is the defaults, untrimmed
        _, st = h.render(f"p1_preset{p}", ["--signal", "drums:120", "--seconds", "8", "--program", str(p)])
        diff = float(st.get("rms_db", "-100")) - ref_db
        rows[name] = round(diff, 2)
        ok = ok and abs(diff) <= 1.0
    record("P1", "presets loudness-trimmed: RMS within 1 dB of the input (drums, preset's own mix)", ok,
           {"input_rms_db": round(ref_db, 2), "difference_db": rows})


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--plugin", action="append", default=[])
    parser.add_argument("--all", action="store_true")
    parser.add_argument("--only", help="comma-separated test ids, e.g. C1,C3,T2")
    parser.add_argument("--skip-pluginval", action="store_true")
    parser.add_argument("--fuzz-seconds", type=float, default=300.0)
    parser.add_argument("--quick", action="store_true", help="30 s fuzz runs")
    args = parser.parse_args()
    if args.quick:
        args.fuzz_seconds = min(args.fuzz_seconds, 30.0)

    known = effects()
    slugs = sorted(s for s in known if s in SPEC) if args.all else args.plugin
    if not slugs:
        parser.error("name a --plugin or use --all")
    only = {x.strip().upper() for x in args.only.split(",")} if args.only else None

    def want(test_id):
        return only is None or test_id in only

    failures = 0
    for slug in slugs:
        if slug not in known or slug not in SPEC:
            print(f"unknown plugin {slug}")
            return 2
        h = Harness(slug, known[slug])
        if not h.exe.exists():
            print(f"harness not built: {h.exe}")
            return 2
        presets = h.params()
        report = {"plugin": slug, "version": known[slug]["version"], "tests": []}
        print(f"\n== {known[slug]['product']} ({slug} {known[slug]['version']})")

        def record(test_id, name, ok, detail):
            report["tests"].append({"id": test_id, "name": name, "pass": bool(ok), **detail})
            print(f"{'PASS' if ok else 'FAIL'}  {test_id:3s} {name}  {json.dumps(detail, ensure_ascii=False)[:420]}")

        common_tests(h, SPEC[slug], presets, record, want, args)
        SPEC[slug]["tests"](h, record, want)
        loudness_test(h, presets, record, want)
        passed = sum(t["pass"] for t in report["tests"])
        failures += len(report["tests"]) - passed
        print(f"{passed}/{len(report['tests'])} passed")
        BUILD.mkdir(exist_ok=True)
        (BUILD / f"test-report-{slug}.json").write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding="utf-8")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
