# LAB X-16 — Design notes and changes from the plan

The plan (architecture, the 23 effect specifications, milestones) is `docs/EFFECTS.md` plus a private handoff. This
file records what the build changed and why, and the test results, as the sibling LAB X- repositories do.

## M0: scaffold, core base, stub

**Zone Bus across DLLs.** The plan kept the bus registry in a `juce::SharedResourcePointer`. That object lives in one
DLL, and every LAB X-16 effect is its own DLL: EMISSION could never reach ARTIFACT, nor any effect another. The registry
is instead a block of plain data in a named file mapping scoped to the host's process id
(`Local\LAB-X16-ZoneBus-<pid>-v1`), mapped by every effect DLL in the process and read and written only through
`std::atomic_ref` (lock-free on the audio thread). The first mapper stamps a magic and a layout version; a client that
finds another layout runs alone. Peers time out after 2 s without a heartbeat (suspended or bridged). Only
`core/host/ZoneBusPlatform.cpp` includes `<windows.h>`.

**No DC blocker in the base.** The plan put one on every effect's output. A 4 Hz high-pass on the whole signal stops a
transparent chain from nulling against its input (check F1, test C1), so DC blocking belongs to the chains that make
DC (rectifiers, biased shapers), as KAYMANOV's spec already says.

**Hot tracks pass.** FL mixes in floating point, so a mixer track may legitimately run above 0 dBFS. The instruments'
safety clip (transparent to 0.95) would have limited every hot track at 100 % mix and even at 0 %. The base leaves the
dry and bypassed signal untouched and rounds off only the processed signal, transparent to +12 dBFS with a +18 dBFS
ceiling; the runaway guard (+42 dBFS or non-finite) still silences and logs a stage that blows up.

**Preset fades crossfade to dry.** A preset change fades the effect, not the music: 30 ms to the latency-aligned dry
signal, `flush()`, 5 ms back. A mixer track never drops out.

**Non-finite host input becomes silence** before the chain, without a fault (the input itself is never faulted).

**Two stubs.** `LAB X-16 STUB` (latency 250 ms, 12000 samples at 48 kHz) and `LAB X-16 STUB ZERO` (none), one source.
Both put an impulse on every bar line into their input path (left, right or both), so two instances on parallel
tracks show plugin delay compensation in one stereo render (F2), and two different DLLs listing each other as PEERS
prove the cross-DLL bus (F7).

Harness checks of the stub: delay-line self-test error 0 (mono and stereo); bypass null 0.0; latency measured equals
reported at 44.1, 48 and 96 kHz (11025, 12000, 24000; 0 for STUB ZERO); state round trip identical; three instances
in one process each list two peers; an injected fault at the stub's stage is silenced and logged by name; editor PNGs
in both themes; pluginval strictness 10 passes for both.

Build times (VS 2022 generator, 2 plugins + 2 harnesses): configure with juceaide 2 min 48 s; cold build 223 s.

### FL Studio checks, first pass (FL 20.6.2 build 1549, 2026-10-13)

| # | Check | Result |
|---|---|---|
| F2 | Plugin delay compensation | **Pass.** STUB (250 ms) and STUB ZERO on parallel tracks, impulses on the bar lines: in the exported song both land on the same sample (96000 and 192000 at 120 BPM). |
| F3 | Slot MIX knob at 50 % with latency | No doubling or slap heard: FL appears to delay the dry part of the slot mix too. |
| F4 | Sidechain | **Pass.** "Sidechain to this track" plus the wrapper's Processing > Connections ("2. Sidechain") moves the stub's meter. |
| F7 | In-process instances | Two STUB instances listed each other; STUB and STUB ZERO did not (see the bus fix below). |
| F5, F6, F1, F8, F9 | MIDI, Smart disable, null, command line, 23 entries | Inconclusive or pending: retest with the fixed stubs. |

**Crash on project load.** A project that routed a sidechain into STUB (and had STUB ZERO bridged) crashed FL on every
load: an access violation reading address 0 inside `Fruity Wrapper_x64.dll`, from its GUI drawing, with no LAB X-16 code
on the stack. With the stubs moved out of the VST3 folder the project loads. The stub declared its sidechain bus
inactive by default, so on load FL restores a routing into a port that does not exist yet; the base now declares the
sidechain bus **active by default** (unrouted, the host feeds it silence). Effects with an optional sidechain therefore
choose their source with a parameter instead of guessing from the bus state. Retest pending.

**Zone Bus fix: keep the mapping handle open.** `Lx16BusHost` (test/bus_host.cpp) loads VST3 effects into its own
process as FL does and reads the bus block: every module had created a block of its own. Windows removes a section's
name when its last handle closes, even while views still map it, and `mapSharedBlock` closed its handle after mapping,
so each DLL that came later created a new block. Each DLL now keeps its handle for its lifetime. With the fix, STUB +
STUB ZERO register two fresh peers on one block, STUB + STUB + STUB ZERO three.

### FL Studio checks, second pass (2026-10-13, fixed stubs)

| # | Check | Result |
|---|---|---|
| F7 | In-process instances | **Pass.** STUB and STUB ZERO list each other; with STUB ZERO bridged, both show ALONE (expected: a bridged plugin runs in another process). |
| F4 + reload | Sidechain, then save and reopen | **Pass.** No crash with the sidechain bus active by default. |
| F5 | MIDI to a mixer effect | **Pass.** A MIDI Out channel's *Port* and the effect wrapper's *Settings > MIDI > Input port* set to the same number deliver notes. |
| F6 | Smart disable vs tails | **Pass.** With *Allow smart disable* on and nothing routed in, TAIL's 4 s decay rings out fully. |
| F8 | Command-line render | **Pass.** `FL64.exe /R /Ewav "<project>.flp"` writes `<project>.wav` next to the project (48 kHz float, the song's length) and exits, in about 9 s. |
| F1 | Null through the effect | Covered: the harness shows the stub passes audio exactly (null 0.0) and F2 shows FL compensates its latency to the sample. |
| F9 | 23 entries | Pending the effects. |

## M1 and wave 1: core DSP, five effects, the test suite

**Core DSP.** `core/dsp` now holds the suite's primitives, ported from the siblings and unified: `Filters` (TPT
state-variable filter, one-poles, DC blocker, RBJ biquads, tilt, fourth-order Butterworth), `Crossovers` (LR4, LR8),
`Crush`, `Modulation` (the RATE and TIME lists, LFO, Cooldown), `Follower`, `Generators` (Geiger, Dust), `MicroShift`
(the shimmer heads, standalone), `Fdn` (one network for every reverb) and `SplitBandDrive` (LAB X-10's EMITTER).

**Base additions.** `BlockContext::mixOffset` lets a macro raise MIX (WISH GRANTER's GRANT adds 0.15). Settings that
change through the base's switch fade (KAYMANOV's oversampling, DOSIMETER's MEDIUM, SILO's LEVEL and TUNNEL) are taken
directly on the first block after `prepareToPlay`, so a project that loads with them set does not start with a fade.
The harness can inject a fault at the OUTPUT stage (a non-finite sample after the chain), so test C6 covers every
stage the specs name.

**KAYMANOV.** Each oversampling path's latency is measured at `prepare` by pushing an impulse through it: the
polyphase IIR half-bands have a fractional latency, and padding by its rounded value left the 2x path one sample off
the others. Crush mode's sample-hold now counts its length at the oversampled rate, so no rate reduction means
holding one oversampled sample (transparent) at every oversampling setting; before, it held one base-rate sample and
added half a sample of delay at 4x. Reported and measured latency now agree (6 samples) at 44.1, 48 and 96 kHz, in
every mode and oversampling setting.

**DOSIMETER.** The click rate is `dose^2 x 400 /s x ((1 - track) + track x env)` rather than `x env^track`: the
envelope is 0 in silence, so `env^track` would stop the clicks for any TRACK above 0, against T2 (clicks continue in
silence while TRACK is below 1). MEDIUM caps are fourth-order Butterworth (Tape a second-order 6.5 kHz low-pass). The
click and dust generators are seeded from the instance id, so a render repeats click for click.

**SILO.** LEVEL picks the NOOSPHERE line set x 0.3 (the small set) x 0.35, 0.5, 0.65, 0.82 or 1.0; SIZE scales within
the level by 0.8 to 1.2. A LEVEL or TUNNEL change goes through the base's switch: the effect fades to dry, the chamber
empties, and the new one fades in, which makes LEVEL a staircase rather than a slider. DAMPING maps to a one-pole from
1.5 kHz to 24 kHz, and the network's damping fades out to exactly none toward 0.45 of the sample rate, so DAMPING 0 is
no damping. Delay modulation is 0.15 ms x (1 - modal): a ringing room barely moves. DUCK's envelope follows the input's
level on a -60..0 dB scale (10 ms attack, DUCK RELEASE): a linear envelope recovers so slowly in dB that the room
stayed ducked long after a hit (T2 failed). TUNNEL sums the input to mono into one line through a 300-3000 Hz band
(12 dB per octave each side); six taps pan narrowly left and right; the longest feeds back, soft-limited.

**RAINBOW.** FEEDBACK takes tap 5 before its LEVEL (LEVEL shapes what is heard, FEEDBACK what returns); Sum is the
taps after their levels, divided by the sum of the levels when that exceeds 1, so it never returns more than one tap
would. Tap times glide like a tape head (50 ms). The dead antenna's detune walk (+-12 ct) and erratic level are
`hash01` decisions per sixteenth note (the host's when it plays, the sample clock's when it is stopped).

**WISH GRANTER.** Every interval runs all three shimmer heads with gliding gains, so a head that joins never plays a
stale buffer. FREEZE ramps over 50 ms (line gains to 1, damping, modulation and input out), then locks the reads to
integer delays; the frozen level holds within 0.2 dB over 30 s. A frozen WISH GRANTER reports an infinite tail (VST3
`kInfiniteTail`). BRIGHTNESS is a tilt around 1.5 kHz.

**The reverb network** (`Fdn`, shared by SILO and WISH GRANTER) reads with Hermite interpolation, whose loss per pass
stays small even in SILO's 3-20 ms lines, and modulates lines 2, 4, 5 and 7 instead of NOOSPHERE's 0, 2, 5 and 7: the
two shortest lines carry the first arrivals, and keeping them still makes the pre-delay exact (T4 was 3 samples off).

**Harness bug: a dangling play head.** `makeInstance` gave the processor a pointer to the play head inside an
`Instance` that was then moved (returned, pushed into a vector), so every harness render read a stale play head:
`--bpm` and the transport position never reached the effects. The play head now lives on the heap. The M0 stub checks
(impulse latency, bypass null, round trip) did not depend on the transport. Every harness instance also gets a fixed
id (overridable with `--id`), so `hash01` decisions and seeded noise repeat from render to render.

**Test suite.** `test/run_tests.py` (with `test/python/labanalyze.py`, merged from X-19's and X-0's `analyze.py`)
runs C1-C9 from `docs/EFFECTS.md`, each effect's T-tests and P1, the preset loudness check. `test/trim_presets.py`
measures each preset at OUTPUT 0 dB and writes the trim into the preset table. Interpretations:

- C2 for the zero-latency reverbs and delays is measured at MIX 0 (their wet paths have no impulse-aligned peak by
  design); KAYMANOV at DRIVE 0 in every mode and oversampling setting; DOSIMETER with the crush, clicks and dust off.
  Latency never changes during a 60 s fuzz (the harness counts changes).
- C4: the largest jump at a preset switch must stay below 0.5, or no larger than the presets' own jumps in steady
  playing (distortion presets jump further than 0.5 in normal operation).
- C5: RAINBOW judges growth in silence against the loudest of the first 4 s of silence (`--growth peak`): its echoes
  arrive up to 4 s apart. DOSIMETER clicks in silence by design (Poisson, so any 100 ms window can beat the early ones
  by chance) and has no recursive path that could grow, so its fuzz checks faults and non-finite output only; T2 covers
  its silence. The others use the siblings' rule (6 dB over the quietest window so far).
- KAYMANOV T2: harmonics above 500 Hz of a 220 Hz sine at 0, 12, 24 and 36 dB drive; crush noise at 10, 20, 30 and
  40 dB drive. T3 at the default 12 dB and at 24 dB drive. At 30 dB (plus the +12 dB pre-emphasis) a folded 5 kHz tone
  has harmonics beyond 96 kHz, past any 4x path; 4x is then only 6 dB cleaner than Off.
- DOSIMETER T4 and RAINBOW T3 use tones an octave outside the band edges (125 Hz and 8 kHz; 150 Hz and 6 kHz).
- SILO T2: DECAY 6 s, a 1 s white-noise burst at -6 dBFS RMS, wet over the burst's last 0.5 s against 250-350 ms after.
- WISH GRANTER T1 allows a 0.5 dB rise between 1 s windows (the crossfading heads wobble; X-19 test 9) and wants
  -95 dBFS by 30 s from a -12 dBFS burst. T4 compares onsets (-60 dB) with PRE-DELAY 0 and 100 ms.
- P1: drums at 120 BPM, -12 dBFS (input RMS -26.7 dBFS), each preset at its own MIX; Init (the defaults) is not
  trimmed. Agroprom Tunnels runs at 80 % MIX: the phone band throws away the kick, and fully wet it needed +13 dB,
  past OUTPUT's range.

### Wave 1 results (2026-10-13, `python -I test/run_tests.py --all`, 300 s fuzz runs)

| Effect | Checks | Latency | Worst-preset CPU (48 kHz, 64-sample blocks) | pluginval 5 / 10 | Effect tests |
|---|---|---|---|---|---|
| KAYMANOV 0.1.0 | 14/14 | 6 samples, every mode and oversampling, 44.1/48/96 kHz | 84x | pass / pass | sub band 0.0025 dB from dry at 24 dB drive; folded 5 kHz 80 dB (12 dB drive) and 60 dB (24 dB) cleaner at 4x; COOLANT period 467.7 ms (468.75) |
| DOSIMETER 0.1.0 | 14/14 | 0 | 384x | pass / pass | 101.5 clicks/s for 100 expected; silence with TRACK 1 exactly silent, TRACK 0.5 keeps 52.8/s; 4-bit steps exactly 1/8; Phone 30 dB down at 125 Hz, 32 dB at 8 kHz |
| SILO 0.1.0 | 15/15 | 0 | 192x | pass / pass | RT60 1.13 s for 1.2 and 2.87 s for 3.0; duck 23.8 dB; line lengths rise with every LEVEL; tunnel taps 0.06 ms late (the band-pass); tail gone with the transport stopped |
| RAINBOW 0.1.0 | 14/14 | 0 | 107x | pass / pass | tap times exact to the sample (Antenna, Golden); repeats -6.26 to -6.30 dB at feedback 0.5; damaged tap 23 / 28 dB down an octave outside its band; reported tails 17.4 and 36.6 s for 5.1 and 8.5 s measured |
| WISH GRANTER 0.1.0 | 14/14 | 0 | 148x | pass / pass | every interval decays to -124 dBFS or below by 30 s, largest 1 s rise 0.36 dB; freeze holds within 0.15 dB over 30 s; octave +57 dB, fifth +39 dB over the plain tail; pre-delay 4800 samples exactly |

Every effect: bypass null 0.0, state round trip identical, preset switches below the presets' own jumps with no fault,
an injected fault at every stage (and OUTPUT) silenced and logged by name, no fault, non-finite sample or (where
judged) growth in ten 300 s fuzz runs, latency constant through the fuzz, editor PNG 960 x 560 with every panel, and
every preset within 1 dB of the input RMS. A full rebuild of the seven plugins and seven harnesses takes about 5 min;
the whole suite about 4 min.
