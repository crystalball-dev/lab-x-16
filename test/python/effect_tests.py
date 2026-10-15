"""The effect tests (T1..) of waves 2-5, after docs/EFFECTS.md. Each function takes (h, record, want) like the wave 1
tests in test/run_tests.py; spec() gives run_tests.py each effect's guard stages, panel titles, the settings that make
it transparent for C2, its fuzz options and its tests. Where a test reads differently from the spec's sentence, the
docstring says how (docs/DESIGN.md lists every interpretation)."""

import math

import numpy as np

import labanalyze as an
from labtest import sets, fixture, mono_of, instance_wav, peak_freq, envelope_period, cents

SR = 48000


def rms_between(x, sr, a, b):
    return an.rms_db(an.segment(x, sr, a, b))


def transfer_db(out, ref, sr, n=16384):
    """The magnitude response out/ref in dB from Welch spectra (noise inputs)."""
    f, po = an.power_spectrum(out, sr, n)
    _, pr = an.power_spectrum(ref, sr, n)
    return f, 10 * np.log10((po + 1e-30) / (pr + 1e-30))


def local_peak(f, db, centre, width):
    sel = np.where((f >= centre * (1 - width)) & (f <= centre * (1 + width)))[0]
    k = sel[int(np.argmax(db[sel]))]
    if 0 < k < len(db) - 1:
        a, b, c = db[k - 1], db[k], db[k + 1]
        d = a - 2 * b + c
        return f[k] + (0.5 * (a - c) / d if d != 0 else 0.0) * (f[1] - f[0])
    return f[k]


def first_break(out, freq, sr, start, threshold=1e-3):
    """The first sample at or after `start` where `out` stops being a sinusoid at `freq`: a two-zero notch at freq
    (y[n] = x[n] - 2 cos(w) x[n-1] + x[n-2]) nulls any steady or slowly changing tone there exactly, so a gain step, a
    burst or a crush shows from its first sample. -1 when none."""
    w = 2 * math.pi * freq / sr
    x = np.asarray(out, dtype=np.float64)
    y = np.zeros_like(x)
    y[2:] = x[2:] - 2 * math.cos(w) * x[1:-1] + x[:-2]
    idx = np.where(np.abs(y[start:]) > threshold)[0]
    return start + int(idx[0]) if len(idx) else -1


def sine(freq, seconds, level_db, sr=SR):
    t = np.arange(int(seconds * sr)) / sr
    return 10 ** (level_db / 20) * np.sin(2 * math.pi * freq * t)


# ---------------------------------------------------------------------------------------------------------------------
def psy_field_tests(h, record, want):
    quiet = sets("whisper=0", "presence=0", "drift_depth=0")
    if want("T1"):
        tone = fixture("psy_tone", SR, 12.0, [(0.0, 6.0, "sine", 440, -12.0)])
        times = sets("exposure_time=5", "recovery_time=5", "threshold=-36")
        _, a = h.render("t1_onset", ["--in", tone, "--level", "0", "--seconds", "5.2"] + times)
        _, b = h.render("t1_recovery", ["--in", tone, "--level", "0", "--seconds", "12"] + times)
        filled, emptied = float(a.get("custom4", "0")), float(b.get("custom4", "1"))
        record("T1", "exposure: ONSET 5 s of a -12 dBFS tone fills it (>= 0.98); RECOVERY 5 s of silence empties it (< 0.02)",
               filled >= 0.98 and emptied < 0.02, {"after_onset": round(filled, 3), "after_recovery": round(emptied, 3)})
    if want("T2"):
        wav, _ = h.render("t2_silence", ["--signal", "silence", "--seconds", "3"] + sets("exposure_mode=1", "exposure=1"))
        m, _, _ = mono_of(wav)
        record("T2", "silence in, silence out (whisper and presence follow the input's envelope)", an.peak_db(m) < -120,
               {"peak_db": round(an.peak_db(m), 1)})
    if want("T3"):
        base = quiet + sets("exposure_mode=1", "exposure=0", "res_feedback=0.9", "vowel=0", "res_level=1", "mix=1")
        wav, _ = h.render("t3_res220", ["--signal", "noise", "--seconds", "6"] + base + sets("res_pitch=220"))
        ref, _ = h.render("t3_input", ["--signal", "noise", "--seconds", "6", "--null-check"])
        m, _, sr = mono_of(wav)
        r, _, _ = mono_of(ref)
        f, db = transfer_db(an.segment(m, sr, 1, 6), an.segment(r, sr, 1, 6), sr)
        p220 = local_peak(f, db, 220, 0.1)
        _, st = h.render("t3_midi", ["--signal", "noise", "--seconds", "2", "--midi", "57@0.1-2"] + base + sets("res_pitch=110", "pitch_source=1"))
        midi_hz = float(st.get("custom1", "0"))
        record("T3", "resonator peak within 1 % of 220 Hz at feedback 0.9; MIDI note 57 tunes it to 220 Hz",
               abs(p220 / 220 - 1) <= 0.01 and abs(midi_hz - 220) < 0.5, {"peak_hz": round(p220, 2), "midi_hz": round(midi_hz, 2)})
    if want("T4"):
        wav, _ = h.render("t4_drift", ["--signal", "sine:440", "--seconds", "12"]
                          + sets("exposure_mode=1", "exposure=1", "drift_depth=40", "drift_rate=1", "res_level=0", "whisper=0", "presence=0"))
        m, _, sr = mono_of(wav)
        frames = []
        for t in np.arange(1.0, 11.5, 0.01):
            s0 = int(round(t * sr))
            frames.append(cents(peak_freq(m[s0:s0 + 4096], sr, 380, 500, 4096), 440))
        frames = np.array(frames)
        worst, step = float(np.abs(frames).max()), float(np.abs(np.diff(frames)).max())
        record("T4", "drift: a 440 Hz tone wanders within +-DRIFT (40 ct) and moves smoothly (under 5 ct per 10 ms frame)",
               worst <= 46 and step <= 5, {"max_cents": round(worst, 1), "max_step_cents": round(step, 2)})
    if want("T5"):
        _, st = h.render("t5_tracker", ["--signal", "chord:G3", "--seconds", "1"] + sets("pitch_source=2"))
        hz = float(st.get("custom1", "0"))
        record("T5", "tracker: a 196 Hz saw tunes the resonator to 196 +-2 Hz within the first second", abs(hz - 196) <= 2,
               {"resonator_hz": round(hz, 2)})


# ---------------------------------------------------------------------------------------------------------------------
def anomaly_tests(h, record, want):
    def compare(out, model):
        err = out - model
        return 10 * math.log10(float(np.sum(model ** 2)) / (float(np.sum(err ** 2)) + 1e-30))

    if want("T1"):
        rows = {}
        sr = SR
        # Springboard 1/16 over the whole bar (bar 1, 2.0-4.0 s at 120 BPM): every 1/16 replays the first.
        wav, _ = h.render("t1_springboard", ["--signal", "noise", "--seconds", "4.5", "--text", "0:1:ssssssssssssssss|"])
        ref, _ = h.render("t1_input", ["--signal", "noise", "--seconds", "4.5", "--null-check"])
        m, _, _ = mono_of(wav)
        r, _, _ = mono_of(ref)
        L = int(0.125 * sr)
        a0 = int(2.0 * sr)
        outs, models = [], []
        for k in range(1, 15):
            s0 = a0 + k * L + int(0.004 * sr)
            outs.append(m[s0:a0 + (k + 1) * L - int(0.002 * sr)])
            models.append(r[a0 + int(0.004 * sr):a0 + L - int(0.002 * sr)])
        rows["springboard"] = round(compare(np.concatenate(outs), np.concatenate(models)), 1)
        # Vortex: backwards from the anchor.
        wav, _ = h.render("t1_vortex", ["--signal", "noise", "--seconds", "4.5", "--text", "0:1:vvvvvvvvvvvvvvvv|"])
        m, _, _ = mono_of(wav)
        e = np.arange(int(0.004 * sr), int(1.0 * sr))
        rows["vortex"] = round(compare(m[a0 + e], r[a0 - e]), 1)
        # Jelly: half speed. A 1000 Hz sine plays at 500 Hz from the anchor, phase continuous.
        wav, _ = h.render("t1_jelly", ["--signal", "sine:1000", "--seconds", "4.5", "--text", "0:1:jjjjjjjjjjjjjjjj|"])
        m, _, _ = mono_of(wav)
        amp = 10 ** (-12 / 20)
        e = np.arange(int(0.004 * sr), int(1.5 * sr))
        model = amp * np.sin(2 * math.pi * 1000 * (a0 + 0.5 * e) / sr)
        rows["jelly"] = round(compare(m[a0 + e], model), 1)
        # Comet: the speed falls from 1 to 0 across the region (2 s), p = x - x^2 / 2d.
        wav, _ = h.render("t1_comet", ["--signal", "sine:1000", "--seconds", "4.5", "--text", "0:1:cccccccccccccccc|"])
        m, _, _ = mono_of(wav)
        d = 2.0 * sr
        e = np.arange(int(0.004 * sr), int(1.6 * sr)).astype(float)
        model = amp * np.sin(2 * math.pi * 1000 * (a0 + e - e * e / (2 * d)) / sr)
        rows["comet"] = round(compare(m[a0 + e.astype(int)], model), 1)
        record("T1", "slot models: Springboard, Vortex, Jelly and Comet match their models at 30 dB SNR or better",
               all(v >= 30 for v in rows.values()), {"snr_db": rows})
    if want("T2"):
        st = h.run(["--selftest"])
        record("T2", "history reads: the delay line's self-test (every integer and half-sample delay, Hermite)", st["exit"] == 0,
               {"mono": st.get("selftest_delay_mono"), "stereo": st.get("selftest_delay_stereo")})
    if want("T3"):
        rows, ok = {}, True
        for bpm in (120, 150, 160):
            bar = 4 * 60 / bpm
            wav, _ = h.render(f"t3_switch_{bpm}", ["--signal", "noise", "--seconds", f"{2.5 * bar:.4f}", "--bpm", str(bpm), "--block", "64",
                                                   "--text", "0:1:ssssssssssssssss|;2:................|", "--step", f"pattern=2@{1.5 * bar:.4f}"])
            ref, _ = h.render(f"t3_input_{bpm}", ["--signal", "noise", "--seconds", f"{2.5 * bar:.4f}", "--bpm", str(bpm), "--null-check"])
            m, _, sr = mono_of(wav)
            r, _, _ = mono_of(ref)
            line = int(round(2 * bar * sr))
            after = float(np.abs(m[line + int(0.003 * sr):line + int(0.3 * sr)] - r[line + int(0.003 * sr):line + int(0.3 * sr)]).max())
            before = float(np.abs(m[line - int(0.1 * sr):line - 2] - r[line - int(0.1 * sr):line - 2]).max())
            rows[str(bpm)] = {"bar_line_sample": line, "diff_after": round(after, 8), "diff_before": round(before, 4)}
            ok = ok and after < 1e-5 and before > 1e-3
        record("T3", "a queued pattern switch lands on the next bar line exactly (120/150/160 BPM, 64-sample blocks: on block edges)",
               ok, rows)
    if want("T5"):
        wav, _ = h.render("t5_midi_hold", ["--signal", "noise", "--seconds", "3", "--stopped", "--midi", "62@1.0-2.0"])
        ref, _ = h.render("t5_input", ["--signal", "noise", "--seconds", "3", "--null-check"])
        m, _, sr = mono_of(wav)
        r, _, _ = mono_of(ref)
        held = float(np.abs(an.segment(m, sr, 1.2, 1.95) - an.segment(r, sr, 1.2, 1.95)).max())
        outside = max(float(np.abs(an.segment(m, sr, 0.2, 0.95) - an.segment(r, sr, 0.2, 0.95)).max()),
                      float(np.abs(an.segment(m, sr, 2.05, 2.9) - an.segment(r, sr, 2.05, 2.9)).max()))
        record("T5", "MIDI hold (note 62, Springboard 1/16, transport stopped): repeats while held, live before and after",
               held > 1e-3 and outside < 1e-5, {"diff_while_held": round(held, 4), "diff_outside": outside})


# ---------------------------------------------------------------------------------------------------------------------
def apparition_tests(h, record, want):
    clean = sets("swell_level=0", "whisper_level=0", "phantom=0", "mix=1")
    if want("T2"):
        imp = fixture("app_impulse", SR, 1.5, [(1.0, 1.0, "impulse", 0, 0.0)])
        wav, _ = h.render("t2_preecho", ["--in", imp, "--level", "0", "--seconds", "1.5"]
                          + clean + sets("echo_sync=0", "echo_ms=60", "echo_count=3", "echo_level=1", "echo_feedback=0.6"))
        m, _, sr = mono_of(wav)
        expected = [1.0 - k * 0.06 for k in (1, 2, 3)]
        found = an.local_peaks(m, sr, expected, 0.005)
        errs = [round((p - e) * sr, 2) for p, e in zip(found, expected)]
        burst = fixture("app_burst", SR, 1.5, [(1.0, 1.1, "noise", 0, -6.0)])
        wav2, _ = h.render("t2_swell", ["--in", burst, "--level", "0", "--seconds", "1.5"]
                           + sets("echo_level=0", "whisper_level=0", "phantom=0", "swell_level=1", "swell_len=90", "mix=1"))
        m2, _, _ = mono_of(wav2)
        inside = float(np.sum(an.segment(m2, sr, 0.910, 0.9995) ** 2))
        before = float(np.sum(an.segment(m2, sr, 0.70, 0.905) ** 2))
        record("T2", "an impulse at 1.000 s: pre-echoes 60, 120, 180 ms before it, to the sample; the reverse swell inside [0.910, 1.000)",
               all(abs(e) <= 1 for e in errs) and inside > 1000 * max(before, 1e-30),
               {"echo_error_samples": errs, "swell_energy_in": round(inside, 4), "swell_energy_before": before})
    if want("T3"):
        mix = fixture("app_tone_hit", SR, 3.0, [(0.0, 3.0, "sine", 440, -12.0), (2.0, 2.02, "noise", 0, 0.0)])
        a, _ = h.render("t3_phantom_on", ["--in", mix, "--level", "0", "--seconds", "3"] + sets("echo_level=0", "swell_level=0", "whisper_level=0", "phantom=1", "mix=1"))
        b, _ = h.render("t3_phantom_off", ["--in", mix, "--level", "0", "--seconds", "3"] + sets("echo_level=0", "swell_level=0", "whisper_level=0", "phantom=0", "mix=1"))
        ma, _, sr = mono_of(a)
        mb, _, _ = mono_of(b)
        d = ma - mb
        sustained, after = rms_between(d, sr, 1.0, 1.5), rms_between(d, sr, 2.0, 2.05)
        record("T3", "PHANTOM within 50 ms after a hit at least 20 dB below its level in a sustained tone", sustained - after >= 20,
               {"sustained_db": round(sustained, 1), "after_hit_db": round(after, 1)})
    if want("T4"):
        imp = fixture("app_impulse", SR, 1.5, [(1.0, 1.0, "impulse", 0, 0.0)])
        wav, _ = h.render("t4_budget", ["--in", imp, "--level", "0", "--seconds", "1.5"]
                          + clean + sets("echo_sync=0", "echo_ms=200", "echo_count=6", "echo_level=1"))
        m, _, sr = mono_of(wav)
        early = float(np.abs(an.segment(m, sr, 0.0, 0.755)).max())
        echo = float(np.abs(an.segment(m, sr, 0.79, 0.81)).max())
        record("T4", "budget: COUNT 6 at 200 ms writes nothing before t - 240 ms (only the first echo fits)", early < 1e-6 and echo > 0.1,
               {"peak_before_0.755_s": early, "echo_at_0.8_s": round(echo, 3)})


# ---------------------------------------------------------------------------------------------------------------------
def carrier_tests(h, record, want):
    plain = sets("comb_level=0", "am_depth=0", "woodpecker=0", "width=0", "mix=1")
    if want("T1"):
        ref, _ = h.render("t1_input", ["--signal", "pink", "--seconds", "8", "--null-check"])
        r, _, sr = mono_of(ref)
        rows, ok = {}, True
        for vowel, name, formants in ((0, "U", (325, 700)), (1, "I", (350, 1700))):
            wav, _ = h.render(f"t1_vowel_{name}", ["--signal", "pink", "--seconds", "8"] + plain + sets("formant_mix=1", f"vowel={vowel}"))
            m, _, _ = mono_of(wav)
            f, db = transfer_db(an.segment(m, sr, 1, 8), an.segment(r, sr, 1, 8), sr)
            found = [round(local_peak(f, db, fc, 0.15), 1) for fc in formants]
            rows[name] = found
            ok = ok and all(abs(p / fc - 1) <= 0.03 for p, fc in zip(found, formants))
        record("T1", "formant peaks within 3 % of the table (U: 325, 700 Hz; I: 350, 1700 Hz)", ok, {"peaks_hz": rows})
    if want("T2"):
        ref, _ = h.render("t2_input", ["--signal", "noise", "--seconds", "8", "--null-check"])
        r, _, sr = mono_of(ref)
        combs = sets("formant_mix=0", "am_depth=0", "woodpecker=0", "width=0", "comb_level=1", "comb_feedback=0.95", "mix=1")
        wav, _ = h.render("t2_chord", ["--signal", "noise", "--seconds", "8"] + combs + sets("chord=13", "root=48"))
        m, _, _ = mono_of(wav)
        f, db = transfer_db(an.segment(m, sr, 1, 8), an.segment(r, sr, 1, 8), sr, 65536)
        notes = (48, 55, 62)
        err = [round(12 * math.log2(local_peak(f, db, 440 * 2 ** ((n - 69) / 12), 0.02) / (440 * 2 ** ((n - 69) / 12))), 3) for n in notes]
        wav2, _ = h.render("t2_midi", ["--signal", "noise", "--seconds", "8", "--midi", "45@0.05-8,52@0.05-8"] + combs + sets("pitch_source=1"))
        m2, _, _ = mono_of(wav2)
        f2, db2 = transfer_db(an.segment(m2, sr, 1, 8), an.segment(r, sr, 1, 8), sr, 65536)
        err2 = [round(12 * math.log2(local_peak(f2, db2, 440 * 2 ** ((n - 69) / 12), 0.02) / (440 * 2 ** ((n - 69) / 12))), 3) for n in (45, 52)]
        record("T2", "combs on the chord's notes (C3 G3 D4) and on held MIDI notes (A2 E3) within 0.1 semitone",
               all(abs(e) <= 0.1 for e in err + err2), {"chord_error_st": err, "midi_error_st": err2})
    if want("T3"):
        wav, _ = h.render("t3_shift", ["--signal", "sine:1000", "--seconds", "3"]
                          + sets("formant_mix=0", "comb_level=0", "width=0", "woodpecker=0", "shift_hz=100", "sideband=1", "am_depth=1", "mix=1"))
        m, _, sr = mono_of(wav)
        seg = an.segment(m, sr, 0.5, 3)
        rej = an.tone_db(seg, sr, 1100, 0.01) - an.tone_db(seg, sr, 900, 0.01)
        record("T3", "frequency shifter: 1 kHz +100 Hz upper lands at 1100 Hz with the 900 Hz image at least 40 dB down", rej >= 40,
               {"image_rejection_db": round(rej, 1)})
    if want("T4"):
        rows = {}
        for sync, expected in ((0, 0.1), (1, 0.125)):
            wav, _ = h.render(f"t4_woodpecker_{sync}", ["--signal", "sine:440", "--seconds", "4", "--bpm", "120"]
                              + sets("formant_mix=0", "comb_level=0", "width=0", "woodpecker=1", f"wp_sync={sync}", "mix=1"))
            m, _, sr = mono_of(wav)
            rows[str(expected)] = round(envelope_period(an.segment(m, sr, 0.5, 4), sr, 0.002, 0.05, 0.2), 5)
        ok = abs(rows["0.1"] - 0.1) <= 0.001 and abs(rows["0.125"] - 0.125) <= 0.00125
        record("T4", "woodpecker 10.0 Hz +-1 %; synced 1/16 at 120 BPM = 8 Hz", ok, {"period_s": rows})


# ---------------------------------------------------------------------------------------------------------------------
def thd_db(x, sr, f0, harmonics=9):
    fund = an.tone_db(x, sr, f0, 0.01)
    power = sum(10 ** (an.tone_db(x, sr, f0 * k, 0.005) / 10) for k in range(2, harmonics + 1) if f0 * k < sr / 2 - 1000)
    return 10 * math.log10(power + 1e-30) - fund


def oasis_tests(h, record, want):
    if want("T1"):
        clipped = fixture("oasis_clipped", SR, 3.0, [(0.0, 3.0, "clipsine", 441, -3.0)])
        wav, _ = h.render("t1_declip", ["--in", clipped, "--level", "0", "--seconds", "3"] + sets("declip=1", "dehiss=0", "regen=0", "sub_regen=0", "mix=1"))
        m, _, sr = mono_of(wav)
        before = thd_db(an.segment(an.mono(an.load(clipped)[0]), sr, 1, 3), sr, 441)
        after = thd_db(an.segment(m, sr, 1, 3), sr, 441)
        record("T1", "a sine clipped at -3 dB: THD after DECLIP at least 15 dB lower", before - after >= 15,
               {"thd_before_db": round(before, 1), "thd_after_db": round(after, 1)})
    if want("T2"):
        parts = [(k, k + 0.5, "sine", 1000, -20.0) for k in range(8)] + [(0.0, 8.0, "noise", 0, -50.0)]
        hissy = fixture("oasis_hissy", SR, 8.0, parts)
        wav, _ = h.render("t2_dehiss", ["--in", hissy, "--level", "0", "--seconds", "8"] + sets("declip=0", "dehiss=1", "regen=0", "sub_regen=0", "mix=1"))
        m, _, sr = mono_of(wav)
        inp = an.mono(an.load(hissy)[0])
        pause = rms_between(inp, sr, 6.6, 6.95) - rms_between(m, sr, 6.6, 6.95)
        tone = rms_between(m, sr, 6.1, 6.45) - rms_between(inp, sr, 6.1, 6.45)
        record("T2", "hiss in the pauses down 20 dB; the tone within 1 dB (DEHISS 1)", pause >= 20 and abs(tone) <= 1,
               {"pause_reduction_db": round(pause, 1), "tone_change_db": round(tone, 2)})
    if want("T3"):
        capped = fixture("oasis_capped", SR, 4.0, [(0.0, 4.0, "lowpass_noise", 4000, -18.0)])
        rows = {}
        for regen in (0, 1):
            wav, _ = h.render(f"t3_regen{regen}", ["--in", capped, "--level", "0", "--seconds", "4"]
                              + sets("declip=0", "dehiss=0", f"regen={regen}", "sub_regen=0", "mix=1"))
            m, _, sr = mono_of(wav)
            rows[regen] = an.band_db(an.segment(m, sr, 1, 4), sr, 8000, 16000)
        record("T3", "a signal capped at 4 kHz gains energy above 8 kHz with REGENERATE (20 dB or more over none)", rows[1] - rows[0] >= 20,
               {"above_8k_regen0_db": round(rows[0], 1), "above_8k_regen1_db": round(rows[1], 1)})


# ---------------------------------------------------------------------------------------------------------------------
def gauss_tests(h, record, want):
    off = sets("whine=0", "flash=0", "mix=1")
    if want("T1"):
        hits = fixture("gauss_hits", SR, 3.0, [(2.5, 2.505, "sine", 1000, -12.0), (2.55, 2.555, "sine", 1000, -12.0)])
        a, _ = h.render("t1_power12", ["--in", hits, "--level", "0", "--seconds", "3"] + off + sets("power=12", "recharge=2", "residual=0.05"))
        b, _ = h.render("t1_power0", ["--in", hits, "--level", "0", "--seconds", "3"] + off + sets("power=0"))
        ma, _, sr = mono_of(a)
        mb, _, _ = mono_of(b)
        first = an.peak_db(an.segment(ma, sr, 2.499, 2.507)) - an.peak_db(an.segment(mb, sr, 2.499, 2.507))
        second = an.peak_db(an.segment(ma, sr, 2.549, 2.557)) - an.peak_db(an.segment(mb, sr, 2.549, 2.557))
        record("T1", "a hit after 2.5 s of silence (RECHARGE 2 s) boosted by POWER +-0.5 dB; the next, 50 ms later, by under 1 dB",
               abs(first - 12) <= 0.5 and second < 1.0, {"first_db": round(first, 2), "second_db": round(second, 2)})
    if want("T3"):
        hit = fixture("gauss_hit4", SR, 5.0, [(4.0, 4.005, "sine", 1000, -12.0)])
        wav, _ = h.render("t3_whine", ["--in", hit, "--level", "0", "--seconds", "5"] + sets("whine=1", "flash=0", "power=12", "recharge=2", "mix=1"))
        m, _, sr = mono_of(wav)
        early, charged, after = rms_between(m, sr, 0.3, 0.6), rms_between(m, sr, 2.5, 3.5), rms_between(m, sr, 4.05, 4.3)
        record("T3", "coil whine rises with the charge and falls at the discharge", charged > early + 6 and after < charged - 6,
               {"early_db": round(early, 1), "charged_db": round(charged, 1), "after_shot_db": round(after, 1)})
    if want("T4"):
        hits = fixture("gauss_pair", SR, 3.0, [(2.5, 2.505, "sine", 1000, -12.0), (2.6, 2.605, "sine", 1000, -12.0)])
        a, _ = h.render("t4_residual", ["--in", hits, "--level", "0", "--seconds", "3"] + off + sets("power=12", "recharge=2", "residual=0.25"))
        b, _ = h.render("t4_power0", ["--in", hits, "--level", "0", "--seconds", "3"] + off + sets("power=0"))
        ma, _, sr = mono_of(a)
        mb, _, _ = mono_of(b)
        second = an.peak_db(an.segment(ma, sr, 2.599, 2.607)) - an.peak_db(an.segment(mb, sr, 2.599, 2.607))
        record("T4", "RESIDUAL 0.25: the hit right after a shot is boosted by POWER x RESIDUAL (3 dB) +-1 dB", abs(second - 3) <= 1,
               {"second_db": round(second, 2)})


    if want("T5"):
        # FL keeps processing a mixer track with the transport stopped: nothing self-generated may sound then.
        wav, _ = h.render("t5_stopped", ["--signal", "silence", "--seconds", "4", "--stopped"] + sets("whine=1"))
        m, _, _ = mono_of(wav)
        record("T5", "transport stopped: no coil whine", an.peak_db(m) < -120, {"peak_db": round(an.peak_db(m), 1)})


# ---------------------------------------------------------------------------------------------------------------------
def psy_dog_tests(h, record, want):
    scene = fixture("dog_tone_hit", SR, 4.0, [(0.0, 4.0, "sine", 440, -12.0), (2.0, 2.02, "noise", 0, 0.0)])

    def phantoms(name, extra):
        a, _ = h.render(f"{name}_on", ["--in", scene, "--level", "0", "--seconds", "4"] + sets("phantom=1", "mix=1") + extra)
        b, _ = h.render(f"{name}_off", ["--in", scene, "--level", "0", "--seconds", "4"] + sets("phantom=0", "mix=1") + extra)
        _, da, sr = mono_of(a)
        _, db, _ = mono_of(b)
        return da - db, sr

    if want("T1"):
        d, sr = phantoms("t1", sets("vanish=5"))
        m = d.mean(axis=1)
        sustained, gone = rms_between(m, sr, 1.0, 1.5), rms_between(m, sr, 2.01, 2.05)
        record("T1", "the pack within 50 ms of a hit (VANISH 5 ms) at least 20 dB under its level in a sustained tone", sustained - gone >= 20,
               {"sustained_db": round(sustained, 1), "after_hit_db": round(gone, 1)})
    if want("T2"):
        a, _ = h.render("t2_wide_on", ["--signal", "noise", "--seconds", "4"] + sets("phantom=1", "pack=6", "spread=1", "mix=1"))
        b, _ = h.render("t2_wide_off", ["--signal", "noise", "--seconds", "4"] + sets("phantom=0", "pack=6", "spread=1", "mix=1"))
        _, da, sr = mono_of(a)
        _, db, _ = mono_of(b)
        d = (da - db)[int(0.5 * sr):]
        corr = an.correlation(d[:, 0], d[:, 1])
        record("T2", "the pack's left and right correlate under 0.3 (SPREAD 1, PACK 6)", corr < 0.3, {"correlation": round(corr, 3)})
    if want("T3"):
        # The pack's duck envelope (meter 0) after a 60 Hz thump at 2.0 s, read at the end of renders 20 ms apart. (Its
        # audio level is no measure: the phantoms' pitch-shift heads make the pack's own level wander by several dB.)
        thump = fixture("dog_tone_thump", SR, 4.0, [(0.0, 4.0, "sine", 440, -12.0), (2.0, 2.03, "sine", 60, 0.0)])
        t_back, lowest = float("inf"), 1.0
        for k in range(36):
            end_s = 2.1 + 0.02 * k
            _, st = h.render("t3_bloom", ["--in", thump, "--level", "0", "--seconds", f"{end_s:.2f}"]
                             + sets("phantom=1", "mix=1", "vanish=5", "bloom=400"))
            duck = float(st.get("custom0", "0"))
            lowest = min(lowest, duck)
            if duck >= 10 ** (-1 / 20):
                t_back = end_s - 2.0
                break
        expected = 0.025 + 0.4   # the 25 ms hold, then BLOOM (the thump's own attack adds a little)
        record("T3", "BLOOM: the pack back within 1 dB of its sustained level 25 ms + 400 ms after the hit (+-20 %)",
               abs(t_back - expected) <= 0.2 * expected, {"returned_after_s": round(t_back, 3), "expected_s": expected,
                                                          "lowest_duck_before_return": round(lowest, 4)})


# ---------------------------------------------------------------------------------------------------------------------
def emission_tests(h, record, want):
    tone = ["--signal", "sine:440.3", "--level", "-24", "--bpm", "120"]
    fire = int(16.0 * SR)

    def steps(wav, k):
        m, _, sr = mono_of(instance_wav(wav, k))
        return first_break(m, 440.3, sr, fire - 400)

    if want("T1"):
        rows, ok = {}, True
        for mode, extra in (("at_bar", sets("0:role=1", "0:arm=2", "0:countdown=9", "0:siren_level=0")),
                            ("countdown", sets("0:role=1", "0:countdown=4", "0:siren_level=0") + ["--step", "0:arm=1@8.0"])):
            wav, st = h.render(f"t1_{mode}", tone + ["--seconds", "17", "--instances", "4"] + extra)
            found = [steps(wav, k) for k in range(4)]
            rows[mode] = [f - fire for f in found]
            ok = ok and all(abs(f - fire) <= 1 for f in found)
        record("T1", "the siren schedules bar 8 (At Bar 9, and Countdown 4 armed at bar 4): every instance's P-wave starts at 16.000 s to the sample",
               ok, {"offset_samples": rows})
    if want("T2"):
        wav, _ = h.render("t2_shelter", tone + ["--seconds", "24", "--instances", "3"] + sets("0:role=1", "0:arm=2", "0:countdown=9", "shelter=1"))
        worst = 0.0
        for k in range(3):
            m, _, sr = mono_of(instance_wav(wav, k))
            ref = sine(440.3, len(m) / sr, -24)
            worst = max(worst, float(np.abs(m - ref).max()) / 10 ** (-24 / 20))
        record("T2", "SHELTER 1: the output equals the input within 0.01 dB through a whole emission", worst <= 0.00115,
               {"worst_relative_error": worst})
    if want("T3"):
        wav, _ = h.render("t3_repeat", tone + ["--seconds", "49"] + sets("role=1", "arm=2", "countdown=9", "repeat=1", "siren_level=0"))
        m, _, sr = mono_of(wav)
        found = [first_break(m, 440.3, sr, int(t * sr) - 400) - int(t * sr) for t in (16.0, 32.0, 48.0)]
        record("T3", "REPEAT 8: fire points at bars 8, 16 and 24 (16, 32, 48 s)", all(abs(f) <= 1 for f in found), {"offset_samples": found})
    if want("T4"):
        wav, _ = h.render("t4_anabiotics", tone + ["--seconds", "25"] + sets("role=1", "arm=2", "countdown=9", "siren_level=0", "anabiotics=1"))
        m, _, sr = mono_of(wav)
        asleep, awake = rms_between(m, sr, 15.95, 22.45), rms_between(m, sr, 23.0, 24.5)
        record("T4", "ANABIOTICS: silent from a quarter beat before the fire point to the end of the aftermath, then back",
               asleep < -100 and awake > -30, {"asleep_db": round(asleep, 1), "awake_db": round(awake, 1)})
    if want("T5"):
        wav, _ = h.render("t5_solo", tone + ["--seconds", "17"] + sets("role=1", "arm=2", "countdown=9", "siren_level=0"))
        m, _, sr = mono_of(wav)
        found = first_break(m, 440.3, sr, fire - 400)
        record("T5", "a siren alone runs its own schedule (P-wave at 16.000 s)", abs(found - fire) <= 1, {"offset_samples": found - fire})


    if want("T6"):
        # One EMISSION alone with its defaults (a listener that has heard no siren): its own schedule fires at bar 3.
        wav, _ = h.render("t6_alone", tone + ["--seconds", "5"] + sets("siren_level=0"))
        m, _, sr = mono_of(wav)
        found = first_break(m, 440.3, sr, int(4.0 * sr) - 400)
        record("T6", "alone with its defaults, EMISSION fires at bar 3 (4.000 s at 120 BPM) to the sample",
               abs(found - int(4.0 * sr)) <= 1, {"offset_samples": found - int(4.0 * sr)})


# ---------------------------------------------------------------------------------------------------------------------
def contagion_tests(h, record, want):
    common = ["--signal", "drums:120", "--bpm", "120", "--instances", "8"] + sets("field_source=0")
    if want("T1"):
        _, st = h.render("t1_r0_zero", common + ["--seconds", "128"] + sets("0:role=1", "r0=0"))
        infected = [int(float(st.get(f"instance_{k}_custom3", "-1"))) for k in range(1, 8)]
        record("T1", "R0 0: no peer of a Patient Zero is ever infected over 64 bars", all(i == 0 for i in infected), {"infections": infected})
    if want("T2"):
        wav, st = h.render("t2_r0_eight", common + ["--seconds", "32"] + sets("0:role=1", "r0=8", "7:immunity=1"))
        infected = [int(float(st.get(f"instance_{k}_custom3", "-1"))) for k in range(1, 8)]
        record("T2", "R0 8 with one Patient Zero: every susceptible peer infected within 16 bars; the immune one (IMMUNITY 1) never",
               all(i >= 1 for i in infected[:6]) and infected[6] == 0, {"infections": infected})
    if want("T3"):
        wav_a, _ = h.render("t3_run_a", common + ["--seconds", "24"] + sets("0:role=1", "r0=4"))
        wav_b, _ = h.render("t3_run_b", common + ["--seconds", "24"] + sets("0:role=1", "r0=4"))
        same = all(np.array_equal(an.load(instance_wav(wav_a, k))[0], an.load(instance_wav(wav_b, k))[0]) for k in range(8))
        record("T3", "determinism: two identical runs give bit-identical outputs for every instance", same, {"identical": same})
    if want("T4"):
        _, st = h.render("t4_symptoms", ["--signal", "noise", "--bpm", "120", "--seconds", "42"]
                         + sets("role=1", "incubation=0", "recovery=32", "stage_curve=0", "symptoms=1"))
        bits, worst = float(st.get("custom5", "0")), float(st.get("custom6", "999"))
        record("T4", "SYMPTOMS 1 at full fever: crushed to 6 bits, the drift within +-80 cents", abs(bits - 6) <= 0.1 and worst <= 88,
               {"bits": round(bits, 2), "max_cents": round(worst, 1)})
    if want("T5"):
        _, st = h.render("t5_solo", ["--signal", "drums:120", "--bpm", "120", "--seconds", "24"]
                         + sets("role=1", "incubation=1", "recovery=4", "wane=4", "field_source=0"))
        state, infections = int(float(st.get("custom0", "-1"))), int(float(st.get("custom3", "-1")))
        record("T5", "alone, a Patient Zero runs the whole course: infected, recovered after 5 bars, susceptible again after WANE",
               state == 0 and infections == 1, {"state_at_bar_12": state, "infections": infections})


    if want("T6"):
        # Alone with its defaults (the track itself as the field): a loud track catches it after the first bar and the
        # fever is under way within eight bars.
        _, st = h.render("t6_alone", ["--signal", "drums:120", "--bpm", "120", "--seconds", "16"])
        state, s_now = int(float(st.get("custom0", "-1"))), float(st.get("custom1", "0"))
        record("T6", "alone with its defaults, a loud track infects itself: infected and feverish (symptoms over 0.5) by bar 8",
               state == 1 and s_now > 0.5, {"state": state, "symptoms": round(s_now, 3)})


# ---------------------------------------------------------------------------------------------------------------------
def hundred_rads_tests(h, record, want):
    if want("T1"):
        a, _ = h.render("t1_depth0", ["--signal", "noise", "--seconds", "3"] + sets("depth=0", "mix=1"))
        b, _ = h.render("t1_input", ["--signal", "noise", "--seconds", "3", "--null-check"])
        diff = float(np.abs(an.load(a)[0] - an.load(b)[0]).max())
        record("T1", "DEPTH 0 hands back the input exactly", diff <= 1e-6, {"max_diff": diff})
    if want("T2"):
        tones = fixture("rads_six", SR, 16.0, [(0.0, 16.0, "sine", f, -20.0) for f in (70, 190, 490, 1240, 3160, 8000)])
        rows = {}
        for label, extra in (("tax_0.3", sets("tax=0.3")), ("tax_0", sets("tax=0", "greed=0.5"))):
            # Changes after the first seating: the count at 16 s minus the count at 2 s (28 beats at 120 BPM).
            counts = []
            for seconds in ("2", "16"):
                _, st = h.render(f"t2_{label}_{seconds}", ["--in", tones, "--level", "0", "--seconds", seconds, "--bpm", "120"] + extra)
                counts.append(int(float(st.get("custom1", "-1"))))
            rows[label] = counts[1] - counts[0]
        record("T2", "six steady tones: the winner changes at least 4 times in 28 beats with TAX 0.3, never with TAX 0",
               rows["tax_0.3"] >= 4 and rows["tax_0"] == 0, {"winner_changes": rows})
    if want("T3"):
        a, _ = h.render("t3_market", ["--signal", "pink", "--seconds", "10"] + sets("depth=1", "budget=0", "mix=1"))
        b, _ = h.render("t3_input", ["--signal", "pink", "--seconds", "10", "--null-check"])
        da, sr = an.load(a)
        db, _ = an.load(b)
        change = an.lufs(da[sr:], sr) - an.lufs(db[sr:], sr)
        record("T3", "loudness kept: integrated LUFS within 1 dB at BUDGET 0 (pink noise, DEPTH 1)", abs(change) <= 1.0,
               {"lufs_change_db": round(change, 2)})
    if want("T4"):
        _, st = h.render("t4_bounds", ["--signal", "drums:120", "--seconds", "30"] + sets("depth=1", "boost=6", "cut=9"))
        lo, hi = float(st.get("custom2", "0")), float(st.get("custom3", "0"))
        wlo, whi = float(st.get("custom4", "-1")), float(st.get("custom5", "2"))
        record("T4", "wealth stays in [0, 1]; gains in [-CUT, +BOOST]", lo >= -9.01 and hi <= 6.01 and wlo >= 0 and whi <= 1,
               {"gain_db": [round(lo, 2), round(hi, 2)], "wealth": [round(wlo, 3), round(whi, 3)]})


# ---------------------------------------------------------------------------------------------------------------------
def c_consciousness_tests(h, record, want):
    if want("T1"):
        _, st = h.render("t1_stability", ["--signal", "noise", "--seconds", "600"] + sets("memory=1", "forget=0", "decay=1", "harmony=1", "mix=1"))
        norm = float(st.get("custom0", "9"))
        ok = int(st.get("faults", "1")) == 0 and int(st.get("non_finite", "1")) == 0 and norm <= 1.0001
        record("T1", "10 min of noise at MEMORY 1, FORGET 0, DECAY 1: no fault, spectral norm <= 1", ok,
               {"spectral_norm": round(norm, 5), "faults": st.get("faults"), "moved_from_hadamard": st.get("custom5")})
    if want("T2"):
        notes = [130.81, 164.81, 196.0]
        scene = fixture("cc_chord_noise", SR, 20.0, [(0.0, 10.0, "sine", f, -18.0) for f in notes] + [(10.0, 20.0, "noise", 0, -18.0)])
        rows = {}
        for memory in (1, 0):
            wav, _ = h.render(f"t2_memory{memory}", ["--in", scene, "--level", "0", "--seconds", "20"]
                              + sets(f"memory={memory}", "harmony=1", "voices=3", "comb_octave=3", "forget=0", "mix=1"))
            m, _, sr = mono_of(wav)
            seg = an.segment(m, sr, 12, 20)
            rows[memory] = 10 * math.log10(sum(10 ** (an.tone_db(seg, sr, f, 0.01) / 10) for f in notes))
        gain = rows[1] - rows[0]
        record("T2", "a C major chord for 10 s, then noise: the tail at C, E and G at least 6 dB over a mind with MEMORY 0", gain >= 6,
               {"learned_over_control_db": round(gain, 1)})
    if want("T3"):
        # An untrained mind (ABSORB from the start) and one that learned a G for 5 s, then ERASE and ABSORB: the same
        # impulse at 10 s (the network's modulation in the same phase), DECAY 0.3 so the learned tone has died by then.
        imp = fixture("cc_impulse_10", SR, 13.0, [(10.0, 10.0, "impulse", 0, 0.0)])
        scene = fixture("cc_learn_then_impulse", SR, 13.0, [(0.0, 5.0, "sine", 196.0, -12.0), (10.0, 10.0, "impulse", 0, 0.0)])
        a, _ = h.render("t3_init", ["--in", imp, "--level", "0", "--seconds", "13"] + sets("absorb=1", "decay=0.3", "mix=1"))
        b, _ = h.render("t3_erased", ["--in", scene, "--level", "0", "--seconds", "13", "--step", "erase=1@5.0", "--step", "absorb=1@5.0"]
                        + sets("memory=1", "decay=0.3", "mix=1"))
        ma, _, sr = mono_of(a)
        mb, _, _ = mono_of(b)
        ea, eb = rms_between(ma, sr, 10.0, 13.0), rms_between(mb, sr, 10.0, 13.0)
        diff = float(np.abs(an.segment(ma, sr, 10.0, 13.0) - an.segment(mb, sr, 10.0, 13.0)).max())
        record("T3", "ERASE: the response after erasing is the untrained mind's (impulse energy within 0.1 dB)", abs(ea - eb) <= 0.1,
               {"init_db": round(ea, 3), "erased_db": round(eb, 3), "max_sample_difference": diff})


    if want("T4"):
        # FL keeps processing a mixer track with the transport stopped: nothing self-generated may sound then.
        wav, _ = h.render("t4_stopped", ["--signal", "silence", "--seconds", "4", "--stopped"] + sets("transfer=1", "mix=1"))
        m, _, _ = mono_of(wav)
        record("T4", "transport stopped: TRANSFER does not hum", an.peak_db(m) < -120, {"peak_db": round(an.peak_db(m), 1)})


# ---------------------------------------------------------------------------------------------------------------------
def artifact_tests(h, record, want):
    if want("T1"):
        _, st = h.render("t1_lattice", ["--signal", "silence", "--seconds", "0.1"] + sets("lattice=0", "f0=220"))
        found = [float(st.get(f"custom{k}", "0")) for k in (4, 5, 6)]
        expected = [220.0, 220.0 * math.sqrt(2), 220.0 * math.sqrt(3)]
        ok = all(abs(f / e - 1) <= 0.001 for f, e in zip(found, expected))
        ref, _ = h.render("t1_input", ["--signal", "noise", "--seconds", "10", "--null-check"])
        wav, _ = h.render("t1_resonance", ["--signal", "noise", "--seconds", "10"] + sets("lattice=0", "f0=220", "q=120", "depth=1", "wet_level=1", "mix=1"))
        m, _, sr = mono_of(wav)
        r, _, _ = mono_of(ref)
        f, db = transfer_db(an.segment(m, sr, 2, 10), an.segment(r, sr, 2, 10), sr, 65536)
        sel = (f > 150) & (f < 1300)
        peak = float(f[sel][int(np.argmax(db[sel]))])
        ratios = [math.sqrt(s) for s in (1, 2, 3, 4, 5, 6, 8, 9, 10, 11, 12, 13, 14, 16, 17, 18, 19, 20, 21, 22, 24, 25, 26, 27)]
        nearest = min(abs(peak / (220 * q) - 1) for q in ratios)
        record("T1", "resonators at BASE x the cubic lattice's ratios (1, sqrt2, sqrt3 ...); the strongest resonance on one of them (1 %)",
               ok and nearest <= 0.01, {"first_three_hz": [round(x, 2) for x in found], "strongest_peak_hz": round(peak, 2)})
    if want("T2"):
        _, st = h.render("t2_bounds", ["--signal", "noise", "--seconds", "60"] + sets("formation=0.1", "feed=1"))
        lo, hi = float(st.get("custom1", "-1")), float(st.get("custom2", "2"))
        record("T2", "the field stays in [0, 1] (60 s of noise, FORMATION 0.1 s, FEED 1)", lo >= 0 and hi <= 1, {"min": lo, "max": hi})
    if want("T3"):
        _, st = h.render("t3_moving", ["--signal", "drums:120", "--seconds", "30"] + sets("formation=6", "feed=0.5"))
        variance = float(st.get("custom3", "0"))
        _, a = h.render("t3_static20", ["--signal", "noise", "--seconds", "20"] + sets("formation=30", "feed=0"))
        _, b = h.render("t3_static30", ["--signal", "noise", "--seconds", "30"] + sets("formation=30", "feed=0"))
        ca, cb = float(a.get("custom7", "0")), float(b.get("custom7", "1"))
        drift = abs(cb - ca) / max(abs(ca), 1e-6)
        record("T3", "FORMATION 6: the gains form a pattern (variance over 0.002); FORMATION 30, FEED 0: nearly still (2 % in 10 s)",
               variance > 0.002 and drift < 0.02, {"gain_variance": round(variance, 5), "field_change": round(drift, 4)})
    if want("T4"):
        _, a = h.render("t4_emission", ["--signal", "noise", "--seconds", "10", "--bpm", "120", "--emission", "4,0,2"])
        _, b = h.render("t4_no_emission", ["--signal", "noise", "--seconds", "10", "--bpm", "120"])
        ra, rb = int(float(a.get("custom0", "-1"))), int(float(b.get("custom0", "-1")))
        record("T4", "an EMISSION fire point on the Zone Bus (bar 4) reseeds the lattice once; none without it", ra == 1 and rb == 0,
               {"reseeds_with_emission": ra, "reseeds_without": rb})


# ---------------------------------------------------------------------------------------------------------------------
def snork_tests(h, record, want):
    if want("T1"):
        _, st = h.render("t1_brighter", ["--signal", "pink", "--seconds", "36", "--bpm", "120"] + sets("goal=0", "drive_goal=0.5", "generation=1"))
        first, now = float(st.get("custom3", "0")), float(st.get("custom2", "0"))
        record("T1", "BRIGHTER on pink noise: the alpha's centroid after 16 generations at least 30 % above generation 0's",
               first > 0 and now >= 1.3 * first, {"first_alpha_hz": round(first, 1), "alpha_hz": round(now, 1), "generations": st.get("custom0")})
    if want("T2"):
        _, a = h.render("t2_frozen10", ["--signal", "pink", "--seconds", "10"] + sets("evolve=0", "generation=1"))
        _, b = h.render("t2_frozen20", ["--signal", "pink", "--seconds", "20"] + sets("evolve=0", "generation=1"))
        same = a.get("custom5") == b.get("custom5")
        record("T2", "EVOLVE Off: the alpha's genome is the same at generation 4 and 8", same, {"checksums": [a.get("custom5"), b.get("custom5")]})
    if want("T3"):
        a, _ = h.render("t3_run_a", ["--signal", "pink", "--seconds", "12"] + sets("generation=1", "seed=7"))
        b, _ = h.render("t3_run_b", ["--signal", "pink", "--seconds", "12"] + sets("generation=1", "seed=7"))
        c, _ = h.render("t3_run_c", ["--signal", "pink", "--seconds", "12"] + sets("generation=1", "seed=8"))
        same = np.array_equal(an.load(a)[0], an.load(b)[0])
        different = not np.array_equal(an.load(a)[0], an.load(c)[0])
        record("T3", "determinism: two renders with one STRAIN are identical; another STRAIN differs", same and different,
               {"same_strain_identical": same, "other_strain_differs": different})


# ---------------------------------------------------------------------------------------------------------------------
def zealots_tests(h, record, want):
    if want("T1"):
        _, st = h.render("t1_zealous", ["--signal", "sine:440", "--seconds", "6"] + sets("zealotry=1", "predator_source=0"))
        spread = float(st.get("custom0", "99"))
        record("T1", "ZEALOTRY 1: the flock's detune spread under 2 cents after 4 s", spread < 2, {"spread_cents": round(spread, 2)})
    if want("T2"):
        # The crowd wanders, so the spread (highest minus lowest detune) is averaged over eight moments.
        spreads = []
        for k in range(8):
            _, st = h.render(f"t2_scattered_{k}", ["--signal", "sine:440", "--seconds", f"{3.0 + 0.5 * k:.1f}"]
                             + sets("zealotry=0", "max_detune=25", "predator_source=0"))
            spreads.append(float(st.get("custom0", "0")))
        mean = float(np.mean(spreads))
        record("T2", "ZEALOTRY 0: the spread stays near MAX DETUNE (on average at least half of 25 ct, 3-6.5 s)", mean >= 12.5,
               {"mean_spread_cents": round(mean, 2), "spreads": [round(x, 1) for x in spreads]})
    if want("T3"):
        scene = fixture("zealots_fire", SR, 6.0, [(0.0, 6.0, "sine", 440, -24.0), (3.0, 3.02, "noise", 0, 0.0)])
        base = sets("zealotry=0.8", "fear=1", "predator_source=1")
        spread = {}
        for t in ("2.9", "3.15", "5.2"):
            _, st = h.render(f"t3_spread_{t}", ["--in", scene, "--level", "0", "--seconds", t] + base)
            spread[t] = float(st.get("custom0", "0"))
        ok = spread["3.15"] >= 3 * max(spread["2.9"], 0.5) and spread["5.2"] <= 0.5 * spread["3.15"]
        record("T3", "the predator: the spread jumps on a hit and halves within 2 s", ok, {"spread_cents": {k: round(v, 2) for k, v in spread.items()}})
    if want("T4"):
        st = h.run(["--selftest"])
        _, r = h.render("t4_long", ["--signal", "drums:120", "--seconds", "600"] + sets("zealotry=0.2", "max_detune=100"))
        record("T4", "reads never leave the buffer: delay self-test, and 10 min with the flock running", st["exit"] == 0 and int(r.get("non_finite", "1")) == 0,
               {"selftest": st.get("selftest_delay_mono"), "non_finite": r.get("non_finite")})


# ---------------------------------------------------------------------------------------------------------------------
def numbers_station_tests(h, record, want):
    if want("T1"):
        wav, _ = h.render("t1_pair", ["--signal", "noise", "--seconds", "6", "--instances", "2", "--set", "1:complement=1"] + sets("key_seed=1986"))
        ref, _ = h.render("t1_input", ["--signal", "noise", "--seconds", "6", "--null-check"])
        a, _ = an.load(instance_wav(wav, 0))
        b, _ = an.load(instance_wav(wav, 1))
        x, _ = an.load(ref)
        err = float(np.abs(a + b - x).max())
        record("T1", "two instances with one key, COMPLEMENT Off and On: their outputs sum to the input within 1e-5", err <= 1e-5, {"max_error": err})
    if want("T2"):
        a, _ = h.render("t2_key_a", ["--signal", "noise", "--seconds", "4"] + sets("key_seed=7"))
        b, _ = h.render("t2_key_b", ["--signal", "noise", "--seconds", "4"] + sets("key_seed=7"))
        c, _ = h.render("t2_key_c", ["--signal", "noise", "--seconds", "4"] + sets("key_seed=8"))
        d, _ = h.render("t2_text", ["--signal", "noise", "--seconds", "4", "--text", "STRELOK"] + sets("key_seed=7"))
        same = np.array_equal(an.load(a)[0], an.load(b)[0])
        differs = not np.array_equal(an.load(a)[0], an.load(c)[0]) and not np.array_equal(an.load(a)[0], an.load(d)[0])
        record("T2", "the hop sequence repeats for one key and changes with another KEY number or key text", same and differs,
               {"same_key_identical": same, "other_keys_differ": differs})
    if want("T3"):
        wav, _ = h.render("t3_buzzer", ["--signal", "silence", "--seconds", "10"] + sets("filter_mode=2", "buzzer_level=1"))
        m, _, sr = mono_of(wav)
        # The on/off gate's period from its rising edges (an 80 ms average hides the ~25 Hz buzz itself; an
        # autocorrelation would lock where the buzz's pulses also line up, 31 pulses away).
        env = np.convolve(an.envelope(m, sr, 0.001), np.ones(80) / 80, mode="same")
        on = env > 0.5 * env.max()
        rises = np.flatnonzero(np.diff(on.astype(int)) == 1) * 0.001
        period = float(np.mean(np.diff(rises))) if len(rises) >= 3 else 0.0
        wav2, _ = h.render("t3_numbers", ["--signal", "silence", "--seconds", "2.1", "--bpm", "120"] + sets("filter_mode=2", "numbers=1", "numbers_level=1"))
        m2, _, _ = mono_of(wav2)
        env = an.envelope(an.segment(m2, sr, 0.0, 0.7), sr, 0.001)
        on = env > env.max() * 0.5
        edges = np.flatnonzero(np.diff(on.astype(int)))
        lengths = np.diff(edges)[:8] * 0.001 if len(edges) > 8 else np.array([])
        on_ms = [round(float(v) * 1000) for v in lengths[0::2]]
        off_ms = [round(float(v) * 1000) for v in lengths[1::2]]
        ok = abs(period - 1.25) <= 0.0125 and all(abs(v - 80) <= 3 for v in on_ms) and all(abs(v - 40) <= 3 for v in off_ms) and len(on_ms) >= 3
        record("T3", "the buzzer's period 1.25 s +-1 %; number pips 80 ms on, 40 ms off", ok,
               {"buzzer_period_s": round(period, 4), "pips_on_ms": on_ms, "pips_off_ms": off_ms})
    if want("T4"):
        wav, _ = h.render("t4_ssb", ["--signal", "sine:1000", "--seconds", "3"] + sets("filter_mode=2", "carrier_hz=100", "sideband=1", "carrier_depth=1"))
        m, _, sr = mono_of(wav)
        seg = an.segment(m, sr, 0.5, 3)
        rej = an.tone_db(seg, sr, 1100, 0.01) - an.tone_db(seg, sr, 900, 0.01)
        record("T4", "single sideband: the 900 Hz image at least 40 dB under 1100 Hz", rej >= 40, {"image_rejection_db": round(rej, 1)})


    if want("T5"):
        # FL keeps processing a mixer track with the transport stopped: nothing self-generated may sound then.
        wav, _ = h.render("t5_stopped", ["--signal", "silence", "--seconds", "4", "--stopped"] + sets("filter_mode=2", "numbers=1", "numbers_level=1", "buzzer_level=1"))
        m, _, _ = mono_of(wav)
        record("T5", "transport stopped: no pips and no buzzer", an.peak_db(m) < -120, {"peak_db": round(an.peak_db(m), 1)})


# ---------------------------------------------------------------------------------------------------------------------
def bateman_counts(t_end, t_hit, half_life, n0, activity, daughters, dt=16 / SR):
    lam = [math.log(2) / (b * half_life) for b in (0.05, 0.3, 1.5, 6.0)]
    n = [n0, 0.0, 0.0, 0.0]
    decayed = [0.0] * 4
    t = t_hit
    while t < t_end:
        d = [lam[k] * n[k] * dt for k in range(daughters)]
        for k in range(daughters):
            n[k] -= d[k]
            if k + 1 < daughters:
                n[k + 1] += d[k]
            decayed[k] += d[k]
        t += dt
    return [activity * 50 * x for x in decayed[:daughters]]


def sarcophagus_tests(h, record, want):
    click = fixture("sarc_click", SR, 1.0, [(0.5, 0.5, "impulse", 0, 0.0)])
    base = sets("dose=1", "activity=1", "half_life=1", "daughters=4", "sensitivity=0.5")
    if want("T1"):
        # Eight clicks a quarter second apart: about 400 particles per generation, so chance (a Poisson spread of about
        # 5 %) stays well inside the tolerance: 20 %, or three standard deviations where few are expected.
        hits = [0.5 + 0.25 * k for k in range(8)]
        clicks = fixture("sarc_clicks", SR, 3.0, [(t, t, "impulse", 0, 0.0) for t in hits])
        rows, ok = {}, True
        for t in (3.0, 10.0, 30.0):
            _, st = h.render(f"t1_counts_{int(t)}", ["--in", clicks, "--level", "0", "--seconds", str(t)] + base)
            got = [int(float(st.get(f"custom{k}", "0"))) for k in range(4)]
            exp = [sum(x) for x in zip(*(bateman_counts(t, th, 1.0, 1.0, 1.0, 4) for th in hits))]
            rows[str(t)] = {"counted": got, "expected": [round(e, 1) for e in exp]}
            ok = ok and all(abs(g - e) <= max(0.2 * e, 3 * math.sqrt(e), 4) for g, e in zip(got, exp))
        record("T1", "eight clicks: particles per generation follow the Bateman solution (20 %, or 3 sigma when few)", ok, rows)
    if want("T2"):
        _, a = h.render("t2_at61", ["--in", click, "--level", "0", "--seconds", "61"] + base)
        _, b = h.render("t2_at71", ["--in", click, "--level", "0", "--seconds", "71"] + base)
        ca = [a.get(f"custom{k}") for k in range(4)]
        cb = [b.get(f"custom{k}") for k in range(4)]
        record("T2", "ten of the longest half-lives after the hit: no more particles", ca == cb and b.get("custom4") == "0",
               {"at_61_s": ca, "at_71_s": cb, "deposits_left": b.get("custom4")})
    if want("T3"):
        st = h.run(["--selftest"])
        dense = sets("dose=1", "activity=1", "grain=30", "half_life=0.1", "pitch_step=12", "attenuation=1")
        _, r = h.render("t3_dense", ["--signal", "drums:120", "--seconds", "120"] + dense)
        _, ref = h.render("t3_input", ["--signal", "drums:120", "--seconds", "120"] + sets("bypass=1"))
        over = float(r.get("peak_db", "99")) - float(ref.get("peak_db", "0"))
        record("T3", "dense decay for 2 min: particles stay inside their capture (no non-finite sample) and the peak stays "
               "within 12 dB of the input's", int(r.get("non_finite", "1")) == 0 and st["exit"] == 0 and over <= 12,
               {"non_finite": r.get("non_finite"), "faults": r.get("faults"), "peak_over_input_db": round(over, 1)})
    if want("T4"):
        burst = fixture("sarc_tone", SR, 7.0, [(0.5, 0.6, "sine", 440, -6.0)])
        wav, _ = h.render("t4_pitch", ["--in", burst, "--level", "0", "--seconds", "7"]
                          + sets("dose=1", "activity=1", "daughters=2", "pitch_step=3", "attenuation=1", "darken=0", "half_life=1"))
        m, _, sr = mono_of(wav)
        seg = an.segment(m, sr, 0.8, 7.0)
        f2 = peak_freq(seg, sr, 600, 650)
        error = cents(f2, 440 * 2 ** (6 / 12))
        record("T4", "generation-2 particles at 2 x PITCH STEP (+6 st: 622.3 Hz) within 10 cents", abs(error) <= 10,
               {"found_hz": round(f2, 2), "error_cents": round(error, 2)})


# ---------------------------------------------------------------------------------------------------------------------
def poltergeist_tests(h, record, want):
    if want("T1"):
        click = fixture("polt_click", SR, 3.0, [(0.5, 0.5, "impulse", 0, 0.0)])
        _, st = h.render("t1_drop", ["--in", click, "--level", "0", "--seconds", "3"] + sets("throw=0", "gravity=9.8", "bounce=0.6", "life=8"))
        first, second = float(st.get("custom6", "-1")), float(st.get("custom7", "-1"))
        fall = math.sqrt(2 * 1.0 / 9.8)
        interval = 2 * 0.6 * math.sqrt(2 * 9.8 * 1.0) / 9.8
        ok = abs(first - fall) <= 0.1 * fall and abs((second - first) - interval) <= 0.1 * interval
        record("T1", "a dropped object (THROW 0): first impact after free fall from 1 m, the next after a bounce at 0.6 (10 %)", ok,
               {"first_s": round(first, 4), "expected_first_s": round(fall, 4), "interval_s": round(second - first, 4), "expected_interval_s": round(interval, 4)})
    if want("T2"):
        _, st = h.render("t2_doppler", ["--signal", "drums:120", "--seconds", "20"] + sets("throw=1", "doppler=1", "objects=12"))
        up, down, wrong = (int(float(st.get(f"custom{k}", "-1"))) for k in (2, 3, 4))
        record("T2", "Doppler: approaching impacts play above their pitch, receding ones below, never the other way",
               wrong == 0 and up + down > 0, {"approaching_up": up, "receding_down": down, "wrong": wrong})
    if want("T3"):
        wav, st = h.render("t3_rage", ["--signal", "drums:120", "--seconds", "20"] + sets("rage=1", "objects=12", "throw=1"))
        ref, _ = h.render("t3_input", ["--signal", "drums:120", "--seconds", "20", "--null-check"])
        peak, ref_peak = float(st.get("peak_db", "99")), an.peak_db(an.mono(an.load(ref)[0]))
        record("T3", "RAGE with twelve objects stays bounded (output peak within 12 dB of the input's)", peak <= ref_peak + 12,
               {"peak_db": round(peak, 1), "input_peak_db": round(ref_peak, 1)})


# ---------------------------------------------------------------------------------------------------------------------
def clear_sky_tests(h, record, want):
    if want("T1"):
        _, st = h.render("t1_storm", ["--signal", "noise", "--seconds", "60"] + sets("weather=4", "lightning=1", "distance=1.715"))
        flash, rumble = float(st.get("custom4", "-1")), float(st.get("custom5", "-1"))
        delay = (rumble - flash) / SR if flash >= 0 and rumble >= 0 else -1
        record("T1", "thunder: the rumble arrives distance / 343 m/s after its flash (1.715 km: 5 s) within 5 %", abs(delay - 5.0) <= 0.25,
               {"delay_s": round(delay, 3)})
    if want("T2"):
        _, st = h.render("t2_day", ["--signal", "silence", "--seconds", "130"] + sets("day_length=1", "time_of_day=12"))
        hours = float(st.get("custom1", "-1"))
        expected = (12 + 130 / 60 * 24) % 24
        record("T2", "the day: TIME OF DAY moves 24 h per DAY LENGTH minutes while the song plays", abs(hours - expected) <= 0.05,
               {"hours": round(hours, 3), "expected": round(expected, 3)})
    if want("T3"):
        counts = {}
        for rain in (0.25, 1.0):
            _, st = h.render(f"t3_rain_{rain}", ["--signal", "silence", "--seconds", "10"] + sets("weather=3", f"rain={rain}"))
            counts[rain] = int(float(st.get("custom3", "0")))
        ratio = counts[1.0] / max(1, counts[0.25])
        record("T3", "rain density scales with RAIN (drops in 10 s, 1.0 against 0.25: 4x +-30 %)", 2.8 <= ratio <= 5.2,
               {"drops": counts, "ratio": round(ratio, 2)})
    if want("T4"):
        # Every weather state at its default element levels on two kinds of program (the reference drums and a saw
        # chord), and a storm with every element at maximum: the peak at most 6 dB over the input's. (White noise is no
        # program: the flash's +6 dB shelf over 5 kHz alone lifts its peak 5 dB.)
        rows, ok = {}, True
        storm_max = sets("weather=4", "rain=1", "wind=1", "lightning=1", "rumble=1")
        for sig in ("drums:120", "chord:C3,E3,G3"):
            tag = sig.split(":")[0]
            _, ref = h.render(f"t4_input_{tag}", ["--signal", sig, "--seconds", "30"] + sets("bypass=1"))
            ref_peak = float(ref.get("peak_db", "0"))
            runs = [(f"weather{w}", sets(f"weather={w}")) for w in range(1, 6)] + [("storm_max", storm_max)]
            for label, extra in runs:
                _, st = h.render(f"t4_{label}_{tag}", ["--signal", sig, "--seconds", "30"] + extra)
                over = float(st.get("peak_db", "99")) - ref_peak
                rows[f"{tag}_{label}"] = round(over, 1)
                ok = ok and over <= 6.0
        _, quiet = h.render("t4_beds", ["--signal", "silence", "--seconds", "30"] + storm_max)
        beds = float(quiet.get("peak_db", "99"))
        record("T4", "levels bounded: no weather state more than 6 dB over the input's peak; silence in, only the beds (under -6 dBFS)",
               ok and beds < -6, {"over_input_peak_db": rows, "silence_peak_db": round(beds, 1)})


    if want("T5"):
        # FL keeps processing a mixer track with the transport stopped: nothing self-generated may sound then.
        wav, _ = h.render("t5_stopped", ["--signal", "silence", "--seconds", "4", "--stopped"] + sets("weather=4", "rain=1", "wind=1", "lightning=1", "rumble=1"))
        m, _, _ = mono_of(wav)
        record("T5", "transport stopped: no rain, wind, thunder or hum", an.peak_db(m) < -120, {"peak_db": round(an.peak_db(m), 1)})


# ---------------------------------------------------------------------------------------------------------------------
def spec():
    dry = [("dry", sets("mix=0"))]
    return {
        "psy_field": {"stages": ["drift", "resonator"], "panels": ["FIELD", "RESONATOR", "VOICES", "DRIFT"], "transparent": dry,
                      "tests": psy_field_tests},
        "anomaly": {"stages": ["anomaly"], "panels": ["LANES", "PATTERN", "ANOMALY"],
                    "transparent": dry + [("live", sets("time_mix=0", "volume_mix=0"))], "fuzz": ["--growth", "peak"], "tests": anomaly_tests},
        "apparition": {"stages": ["apparition"], "panels": ["FORECAST", "PRE-ECHO", "SWELL", "PHANTOM"],
                       "transparent": dry + [("clean", sets("echo_level=0", "swell_level=0", "whisper_level=0", "phantom=0"))],
                       "tests": apparition_tests},
        "carrier": {"stages": ["banks", "shift"], "panels": ["FORMANT", "COMB", "SHIFT", "WOODPECKER"],
                    "transparent": dry + [("plain", sets("formant_mix=0", "comb_level=0", "am_depth=0", "woodpecker=0", "width=0"))],
                    "tests": carrier_tests},
        "oasis": {"stages": ["declip", "dehiss"], "panels": ["HEAL", "DECLIP", "DEHISS", "REGENERATE"],
                  "transparent": [("off", sets("declip=0", "dehiss=0", "regen=0", "sub_regen=0", "heal=0"))], "tests": oasis_tests},
        "gauss": {"stages": ["rail"], "panels": ["CAPACITOR", "RAIL", "COIL"], "transparent": [("off", sets("power=0", "whine=0", "flash=0"))],
                  "fuzz": ["--no-growth-check"], "tests": gauss_tests},
        "psy_dog": {"stages": ["pack"], "panels": ["PACK", "ILLUSION", "SHAPE"], "transparent": dry + [("none", sets("phantom=0"))],
                    "tests": psy_dog_tests},
        "emission": {"stages": ["program"], "panels": ["COUNTDOWN", "SIREN", "WAVE", "SHELTER", "PEERS"], "transparent": [("listener", [])],
                     "fuzz": ["--no-growth-check"], "tests": emission_tests,
                     "untrimmed": ["Standard Blowout", "Shelter in Bunker", "Open Field", "Periodic Sixteen", "Anabiotics",
                                   "Severe Front"]},
        "contagion": {"stages": ["drift", "choke", "grit"], "panels": ["OUTBREAK", "SPREAD", "COURSE", "SYMPTOMS", "PEERS"],
                      "transparent": [("healthy", [])], "tests": contagion_tests,
                      "untrimmed": ["Patient Zero", "Yantar Outbreak", "Slow Zombification", "Psi-helmet Issued", "Fever Dream",
                                    "Pandemic R8"]},
        "hundred_rads": {"stages": ["auction"], "panels": ["MARKET", "AUCTION", "ECONOMY"], "transparent": [("idle", sets("depth=0"))],
                         "tests": hundred_rads_tests},
        "c_consciousness": {"stages": ["fdn"], "panels": ["MIND", "LEARNING", "HARMONY", "SPACE"], "transparent": dry,
                            "fuzz": ["--growth", "peak"], "tests": c_consciousness_tests},
        "artifact": {"stages": ["lattice"], "panels": ["CRYSTAL", "PROPERTY", "LATTICE", "GROWTH", "PEERS"],
                     "transparent": dry + [("clean", sets("wet_level=0", "property=0"))], "fuzz": ["--no-growth-check"], "tests": artifact_tests},
        "snork": {"stages": ["alpha"], "panels": ["BREEDING", "GOAL", "GENETICS", "STRAIN"], "transparent": dry, "tests": snork_tests},
        "zealots": {"stages": ["flock"], "panels": ["FLOCK", "DOCTRINE", "PREDATOR"], "transparent": dry, "tests": zealots_tests},
        "numbers_station": {"stages": ["hop", "carrier"], "panels": ["TRANSMISSION", "HOP", "CARRIER", "NUMBERS"],
                            "transparent": [("off", sets("filter_mode=2", "carrier_depth=0", "numbers=0", "buzzer_level=0"))],
                            "fuzz": ["--no-growth-check"], "untrimmed": ["Complement A", "Complement B"],
                            "tests": numbers_station_tests},
        "sarcophagus": {"stages": ["chain"], "panels": ["REACTOR", "DEPOSIT", "DECAY", "PARTICLES"], "transparent": [("empty", sets("dose=0"))],
                        "fuzz": ["--no-growth-check"], "tests": sarcophagus_tests},
        "poltergeist": {"stages": ["objects"], "panels": ["ROOM", "TELEKINESIS", "PHYSICS", "ACOUSTICS"], "transparent": dry,
                        "fuzz": ["--no-growth-check"], "tests": poltergeist_tests},
        "clear_sky": {"stages": ["sky"], "panels": ["FORECAST", "FRONT", "ELEMENTS", "STORM", "DAY"],
                      "transparent": [("calm", sets("weather=1", "fog=0", "rain=0", "wind=0", "lightning=0", "time_of_day=12"))],
                      "fuzz": ["--no-growth-check"], "tests": clear_sky_tests},
    }
