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

## Waves 2-5: the other eighteen effects

PSY-FIELD, ANOMALY, APPARITION, CARRIER, OASIS, GAUSS and PSY-DOG (wave 2), EMISSION and CONTAGION (wave 3, the bus
clients), 100 RADS, C-CONSCIOUSNESS, ARTIFACT and SNORK (wave 4, learned and evolved state), ZEALOTS, NUMBERS STATION,
SARCOPHAGUS, POLTERGEIST and CLEAR SKY (wave 5) were built together and tested once at the end, all at 0.1.0.

**Core additions.** `lab::dsp` gains `Hilbert` (a polyphase IIR phase splitter, twelve first-order all-pass sections
in two branches, for the true frequency shifts of CARRIER and NUMBERS STATION), `Onset` (LAB X-0's transient-shaper
envelopes as an onset detector), `Allpass`, `Resonator` and `CombFilter` (LAB X-10's resonator), `FormantBank` (LAB
X-19's alto table), `Ensemble` (X-19), `Dynamics` (transient shaper, compressor, parallel grit), the generators
`Programmer`, `Whisper`, `Presence` and `Siren`, and `Anomaly`, LAB X-0's TIME and VOLUME lane engine on its own.
`ui/Views.h` adds bar, matrix, dot-field and big-number views.

**Shared code that changed under wave 1.** Wave 1 passes all its tests again with these changes.

- `MicroShift` runs one formula through every ratio: the two heads keep moving through ratio 1 instead of switching
  to a fixed path there. RAINBOW's damaged tap, whose detune walk crosses zero, jumped at each crossing; WISH
  GRANTER's ratios (all above 1) sound the same.
- `Fdn` takes an optional 8 x 8 mixing matrix (C-CONSCIOUSNESS's learned one, in place of the Hadamard), an extra mono
  input (EMISSION's bloom, C-CONSCIOUSNESS's harmony) and exposes its line outputs. Without them it is the same network.
- The Zone Bus keeps a load per bar parity (`publishParity`): CONTAGION's peers cross a bar line in any order, and
  each reads every peer's load for the bar just ended from the other parity's slot, so the outbreak does not depend
  on the order the host runs the tracks in. The layout version is unchanged (the field was reserved and zero).
- `Chain::presetLoaded` lets state that is not a parameter follow a factory preset (NUMBERS STATION drops a typed key,
  so a preset's key is its KEY number alone), and `Chain::loadText` gives the harness a text form of a chain's own
  state (`--text`: ANOMALY's patterns, NUMBERS STATION's key). The harness also takes an instance prefix (`k:`) on `--step`, `--ramp`, `--text` and `--set`,
  prints every instance's meters, and can post an EMISSION schedule on the bus (`--emission`).
- Parameter units `min`, `km`, `m/s2` and `h` (shown as hh:mm).

**Changes from the plan, effect by effect.**

- ANOMALY: RESOLUTION and LENGTH are global rather than stored per pattern (one grid, one clock); the presets select
  the eight factory patterns. MIDI holds work with the transport stopped (on an internal clock at the last tempo) and
  take effect at once rather than at the next step.
- APPARITION: events read the input history when they play, not when the onset is found, so only causality limits
  them: a pre-echo k echo times early needs k x ECHO + 2 ms <= 240 ms, and the reverse swell (audio from after the
  hit, played backwards) needs twice its length inside the window, so SWELL LENGTH is 20-120 ms instead of 20-200. The
  plan's single clamp on the sum of all lengths is replaced by these per-event limits.
- EMISSION: ARM is Off, Countdown (COUNTDOWN bars after the bar in which it was switched on, as planned) or At Bar
  (fires at bar COUNTDOWN of the song, so a render or a loop replays it wherever playback starts). Only the Siren
  instance sounds the siren. The P-wave's noise burst follows the track's own envelope, so a silent track stays silent.
- CONTAGION: DRIFT is a wandering delay whose amplitude follows the symptoms (exactly the input at none). Every
  instance starts healthy when the transport starts, so a render repeats the outbreak.
- 100 RADS: the economy is turned around so the plan's own test can hold. The plan had winners pay GREED x price and
  losers save TAX x (1 - W) on their own; then the winner changes even at TAX 0. Here the winners pay TAX x price, the
  tax goes to the losers, and GREED enriches the winners: TAX 0 is a market where the rich stay rich.
- C-CONSCIOUSNESS: Oja's rule as planned, but every step is projected back to an orthogonal matrix (the norm clamp,
  then two Newton-Schulz polar steps) instead of normalising rows. With row normalisation the matrix drifted toward
  rank one and the norm clamp then scaled the whole feedback down: the more the mind learned, the shorter the reverb
  (the learned tail came out 5 dB under the untrained one). The harmony combs filter what enters the network rather
  than its own output: fed back from the output they formed a second loop around the network that could oscillate
  (the fuzz's growth check caught it). ABSORB also freezes forgetting.
- ARTIFACT: reseeds from EMISSION's schedule (when the song crosses a fire point) instead of the bus's reseed counter,
  so the reseed lands on the same bar in every render. Gray-Scott's textbook constants (Du 0.16) die out on a ring of
  24 cells; a search over the ring gave Du 0.4, Dv 0.05 and the four regimes Spots (F 0.04, k 0.062), Stripes (0.03,
  0.055), Mitosis (0.025, 0.05) and Coral (0.05, 0.055), with three-cell seeds, the input's energy modulating the
  substrate feed (feeding v directly saturated every cell), and re-nucleation when the pattern dies out.
- GAUSS: the boost is POWER x charge in dB; a shot leaves charge x RESIDUAL; the rail flash scales with the hit.
- SNORK: what plays is a voice whose genome glides to the alpha's (no jump at a generation boundary).
- ZEALOTS: an AGC keeps the flock at the input's level (a converged flock is coherent and much louder), and each
  agent's restlessness is its own slow random walk.
- CLEAR SKY: every CLEAR SKY in a project sees the same sky (the Markov rolls hash the front's index alone).

**Bugs the tests found.** Each is fixed and its test passes.

- ANOMALY's heads: a head that jumped kept the old head's speed, so a turn from forward to backward (Vortex after live
  play, and from the very first sample) was a jump on every sample: Vortex played a stale head two samples off at
  119/120 of the level. A head now takes its speed from its first step after a jump.
- The onset detector fired a second time on a lone click's own decay: when the 50 ms gap ended, the fast envelope
  still led the slow one. It now re-arms only after the attack condition has once been false. (POLTERGEIST's drop test
  saw two objects; SARCOPHAGUS made a small second deposit.)
- OASIS: the noise floor dropped instantly to whatever the envelope read, including while the envelope charged from
  silence, then climbed back at 1 dB/s, so it sat far below the hiss and DEHISS never engaged; any song starting in
  silence would do the same. The floor now falls at most 40 dB/s, rises 1 dB/s, and learns nothing from digital
  silence. The expander also opened slowly and closed fast, which smeared attacks; it now opens in 2 ms and closes
  over 80 ms.
- CLEAR SKY: each flash overwrote the one pending thunder, so with flashes every few seconds most rumbles never came
  (the first arrived 18.5 s after the first flash instead of 5 s). Flashes now queue their thunder, and up to four
  rumbles overlap.
- 100 RADS: the auction ran every 16 samples on raw band levels, so the ripple of six equal tones flipped the winner
  thousands of times (and would have chattered the gains). A reigning winner now keeps its seat until outbid by
  10 %. The budget balanced the dB targets weighted by dB levels, which does not conserve power (-1.7 LUFS on pink
  noise); balancing the targets in power was not enough either (-4.6 LUFS), because on noise the winner still changes
  several times a second and a new winner rises slower than the old one falls. The shift is now computed every block
  on the gains as they stand, holding the bands' K-weighted power (BS.1770's curve at each band's centre), in a few
  passes because bands at +BOOST or -CUT stop taking it; the gains also move sample by sample across the block.
- ZEALOTS: at ZEALOTRY 0 the cohesion spring and an alignment that did not fade with ZEALOTRY cancelled most of the
  restlessness (about 30 % of MAX DETUNE). Cohesion is now 0.05 + 5.45 x ZEALOTRY and alignment 0.5 x ZEALOTRY.
- SNORK: a specimen's pitch shifter was fed only while its pitch gene shifted, so when the gene (or the voice's glide)
  came back, up to 50 ms of stale audio replayed; in silence that read as growth. The shifter always runs and fades in
  and out over 20 ms around +-0.25 semitone.
- APPARITION placed echoes at the truncated sample count: 0.06 x 48000 is 2879.99..., so echo k came k samples late.
  Sample counts are rounded.
- SARCOPHAGUS: particles of one hit start within its first 20 ms at one pitch, so a dense burst added up nearly in
  phase, up to 27 dB over the hit (a 300 s fuzz run peaked at the +18 dBFS ceiling). The particle bus is now scaled
  by the number of grains sounding (1 / sqrt (n / 2), smoothed over 2 ms) and soft-limited at 0 dBFS; T3 bounds the
  peak of two minutes of the densest decay.

**Test interpretations.** As for wave 1, these are recorded so the numbers in the results can be read.

- C2 for the bus clients and modulators: EMISSION as a listener with no schedule, CONTAGION healthy, 100 RADS at DEPTH
  0, NUMBERS STATION with the filter, carrier, numbers and buzzer off, SARCOPHAGUS at DOSE 0, CLEAR SKY calm and clear.
- C5 judges growth against the loudest of the first 4 s of silence (`--growth peak`) for ANOMALY (a repeat can replay
  a loud slice in silence) and C-CONSCIOUSNESS (TRANSFER hums in silence by design). GAUSS (the coil whine charges in
  silence), EMISSION, ARTIFACT, NUMBERS STATION (the buzzer and pips), SARCOPHAGUS (a late generation's particle can
  pop seconds after the hit, louder than the quiet moments before it), POLTERGEIST and CLEAR SKY (the weather beds)
  make sound in silence by design: their fuzz checks faults and non-finite output only, and their own tests bound
  their levels.
- ANOMALY T1 compares Springboard, Vortex, Jelly and Comet with their exact models (a 30 dB SNR floor; Hermite reads
  of a 1 kHz tone at half speed are the limit).
- APPARITION T2 checks the echoes to the sample and the swell's energy inside its window; T4 checks that COUNT 6 at
  200 ms writes nothing before t - 240 ms.
- EMISSION T1 detects the P-wave with a two-zero notch at the test tone: it nulls any steady tone, so the warning's
  high-pass does not register, and the first changed sample does.
- CONTAGION T4 measures at full fever (incubation 0, linear course): 6 bits and the drift's speed within 88 cents.
- 100 RADS T2 counts winner changes after the first seating (2-16 s, 28 beats at 120 BPM).
- C-CONSCIOUSNESS T3 compares an untrained mind (ABSORB from the start) with one that learned a G for 5 s and was
  then erased and set to ABSORB, the same impulse at 10 s in both, DECAY 0.3 so the learned tone has died by then.
- SNORK T1's generation 0 is the neutral genome the voice starts from (the best of the random first population can
  already be past the goal's target, the input's centroid plus 1.25 octaves at STRENGTH 0.5).
- ZEALOTS T2 averages the spread over eight moments; the crowd wanders.
- NUMBERS STATION T3 times the buzzer by its rising edges: an autocorrelation locks where the ~25 Hz buzz's pulses also
  line up (31 pulses, 1.28 s).
- PSY-DOG T3 reads the pack's duck envelope (its meter) after a 60 Hz thump, at the end of renders 20 ms apart. The
  pack's audio level is no measure of BLOOM: the phantoms' pitch-shift heads make it wander by several dB, so on a
  noise hit and full band it seemed back after 0.23 s, and even band-limited around the tone after 0.24 s.
- CLEAR SKY T4: every weather state at its default element levels on the drums and on white noise, and a storm with
  every element at maximum on the drums, stays within 6 dB of the input's peak. (White noise through the flash's +6 dB
  shelf above 6 kHz gains 5 dB on its own.) Silence in gives only the beds, under -6 dBFS.
- SARCOPHAGUS T1 compares particle counts with the Bateman solution integrated with the effect's own control step,
  from eight clicks a quarter second apart: one click gives about 50 particles per generation, whose Poisson spread
  (14 %) alone fails a 20 % tolerance one time in six. Tolerance 20 %, or three standard deviations where few are
  expected.
- P1: NUMBERS STATION's Complement A and Complement B keep OUTPUT at 0 dB. They are a pair whose outputs sum to the
  input across two tracks (T1); a loudness trim on each would break the sum. The spec's `untrimmed` list exempts them
  from both `trim_presets.py` and P1.

### Waves 2-5 results

(2026-10-15, `python -I test/run_tests.py`, six runners in parallel, 300 s fuzz runs)

| Effect | Checks | Latency | Worst-preset CPU (48 kHz, 64-sample blocks) | pluginval 5 / 10 | Effect tests |
|---|---|---|---|---|---|
| PSY-FIELD 0.1.0 | 15/15 | 0 | 240x | pass / pass | exposure full after 5 s ONSET, empty after 5 s RECOVERY; silence stays exactly silent; resonator peak 221.2 Hz for 220, MIDI 57 tunes 220.0 Hz; drift within 25.9 ct at DRIFT 40, at most 0.55 ct per 10 ms; tracker 196.0 Hz on G3 |
| ANOMALY 0.1.0 | 14/14 | 0 | 384x | pass / pass | Springboard and Vortex exact (SNR 329 and 327 dB), Jelly 106 dB, Comet 68 dB; a pattern switch lands exactly on the bar line at 120, 150 and 160 BPM; a MIDI hold repeats while held and is exact outside |
| APPARITION 0.1.0 | 13/13 | 250 ms (11025, 12000, 24000) | 192x | pass / pass | pre-echoes at 60, 120 and 180 ms to the sample; the swell inside its window; PHANTOM 30.6 dB down within 50 ms of a hit; nothing written before t - 240 ms |
| CARRIER 0.1.0 | 14/14 | 0 | 160x | pass / pass | formants U 326.9 / 720.2 Hz (325 / 700), I 348.8 / 1727.5 Hz (350 / 1700); combs within 0.09 st of the chord and of held MIDI notes; shifted image 70.4 dB down; woodpecker 0.1000 s free, 0.1250 s at 1/16 |
| OASIS 0.1.0 | 13/13 | 64 samples | 47x | pass / pass | clipped sine's THD from -17.5 to -48.1 dB; hiss in the pauses 26.5 dB down, the tone -0.09 dB; above 8 kHz from -103 to +33 dB with REGENERATE |
| GAUSS 0.1.0 | 13/13 | 64 samples | 480x | pass / pass | +11.93 dB after silence, +0.6 dB 50 ms later; +3.0 dB at RESIDUAL 0.25; whine -48, -23 (charged), -63 dB (after the shot) |
| PSY-DOG 0.1.0 | 13/13 | 0 | 120x | pass / pass | the pack silent within 50 ms of a hit; left-right correlation -0.006 at SPREAD 1; back within 1 dB 0.48 s after the hit (0.46 expected) |
| EMISSION 0.1.0 | 15/15 | 0 | 160x | pass / pass | every instance's P-wave at 16.000 s to the sample (At Bar and Countdown), repeats at 32 and 48 s to the sample; SHELTER 1 within 2e-7 of the input; ANABIOTICS silent, then back; a lone siren the same |
| CONTAGION 0.1.0 | 15/15 | 0 | 320x | pass / pass | R0 0: no infection in 64 bars; R0 8: all six susceptible peers infected, the immune one never; reruns bit-identical; full fever 6.0 bits, drift 19 ct; alone, susceptible again after WANE |
| 100 RADS 0.1.0 | 14/14 | 0 | 148x | pass / pass | DEPTH 0 exact; 19 seat changes in 28 beats at TAX 0.3, none at TAX 0; +0.18 LUFS on pink noise at BUDGET 0; gains within -9.0 and +6.0 dB |
| C-CONSCIOUSNESS 0.1.0 | 13/13 | 0 | 74x | pass / pass | 10 min at MEMORY 1, FORGET 0: spectral norm 1.0000, moved 27.8 from the Hadamard, no fault; learned tail +7.1 dB at C, E and G; erased mind equals an untrained one to 6e-11 |
| ARTIFACT 0.1.0 | 14/14 | 0 | 160x | pass / pass | resonators at 220.0, 311.1 and 381.1 Hz (1, sqrt 2, sqrt 3); strongest peak 1144 Hz on a lattice ratio; field within [0, 0.90]; gain variance 0.061 at FORMATION 6, field still to 0.11 % in 10 s at 30; one reseed on an emission, none without |
| SNORK 0.1.0 | 13/13 | 0 | 27x | pass / pass | BRIGHTER: the alpha's centroid from 932 to 2202 Hz (2.4x) in 16 generations; EVOLVE Off keeps the genome; one strain renders identically, another differs |
| ZEALOTS 0.1.0 | 14/14 | 0 | 113x | pass / pass | spread 0.07 ct at ZEALOTRY 1, 19.3 ct on average at 0 (MAX DETUNE 25); 0.16, 19.2 and 0.22 ct before, just after and 2 s after a hit; 10 min of motion without a non-finite sample |
| NUMBERS STATION 0.1.0 | 14/14 | 0 | 384x | pass / pass | the complementary pair sums to the input within 1.5e-8; keys repeat and differ; buzzer period 1.2501 s, pips 80 ms on, 40 off; shifted image 70.4 dB down |
| SARCOPHAGUS 0.1.0 | 14/14 | 0 | 320x | pass / pass | particle counts within 10 % of the Bateman solution (eight clicks; 396, 431, 378, 390 at 30 s for 400, 400, 400, 379); none after ten of the longest half-lives; two minutes of the densest decay peak at the input's level; generation 2 at -1.2 ct of +6 st |
| POLTERGEIST 0.1.0 | 13/13 | 0 | 480x | pass / pass | first impact 0.4517 s (free fall from 1 m: 0.4518), bounce interval 0.5417 s (0.5421); Doppler 197 approaching up, 25 receding down, 0 wrong; RAGE with 12 objects 3.6 dB over the input's peak |
| CLEAR SKY 0.1.0 | 14/14 | 0 | 175x | pass / pass | thunder 5.000 s after its flash at 1.715 km; the day at 16:00 after 130 s of a 1-minute day from 12:00; rain 3.5x the drops for 4x RAIN; weather states at most 5.6 dB over the input's peak, the beds alone under -6 dBFS |

Wave 1 runs again with the changed core: KAYMANOV 14/14 (80x), DOSIMETER 14/14 (384x), SILO 15/15 (175x), RAINBOW
14/14 (101x), WISH GRANTER 14/14 (120x), each with the same effect-test numbers as before and pluginval 5 and 10.

Every effect, all 23: bypass null 0.0, state round trip identical, preset switches below the presets' own jumps with no
fault, an injected fault at every stage (and OUTPUT) silenced and logged by name, no fault or non-finite sample (and,
where judged, no growth) in ten 300 s fuzz runs, latency constant through the fuzz, an editor PNG of 960 x 560 with
every panel, every trimmed preset within 1 dB of the input RMS, and pluginval strictness 5 and 10: 319 checks, all
pass. The whole suite takes about 6 minutes in six parallel runners. After a change to a shared header, rebuilding the
23 plugins (VST3 and standalone) and 25 harnesses with `msbuild build\LABX16.sln /m:12` takes about 14 minutes; JUCE's
modules are not recompiled. (`cmake --build` given several `--target` names under the Visual Studio generator builds
them one MSBuild at a time, with one link running; a whole-solution build runs a dozen compiles and links at once.)
