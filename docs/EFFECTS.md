# LAB X-16 — Effect specifications

Twenty-three standalone FL Studio mixer effects in two factions. Written 2026-10-12 with the plan (a private handoff
that holds the architecture, the core API, the Zone Bus, the theme engine, the FL checks and the milestones; what the
build changed is in `docs/DESIGN.md`). This file is the per-effect contract: parameters, DSP, host I/O, presets and tests. Commit it
as `docs/EFFECTS.md` once the first wave builds; keep it current when a spec changes during execution, and record
the reason in `docs/DESIGN.md`.

Concept leads. Each effect imports one mechanism from a field unrelated to audio and bends it to the Zone. The
numbers below (times, ranges, constants) are starting points for the executing model to tune by ear and by test;
the mechanisms, parameter ids and host contracts are the fixed part.

---

## 0. Roster, conventions, common tests

### 0.1 Roster

| # | Effect | Slug | Code | Faction | VST3 categories | Latency | Sidechain | MIDI | Zone Bus | Wave |
|---|---|---|---|---|---|---|---|---|---|---|
| 1 | KAYMANOV | `kaymanov` | `L6Ka` | A | Fx, Distortion | fixed: 4x oversampler path | no | no | no | 1 |
| 2 | PSY-FIELD | `psy_field` | `L6Ps` | A | Fx, Modulation | zero | optional (field source) | optional (pitch) | no | 2 |
| 3 | DOSIMETER | `dosimeter` | `L6Do` | A | Fx, Distortion | zero | no | no | no | 1 |
| 4 | SILO | `silo` | `L6Si` | A | Fx, Reverb | zero | no | no | no | 1 |
| 5 | ANOMALY | `anomaly` | `L6An` | A | Fx, Delay | zero | no | optional (slot triggers) | no | 2 |
| 6 | CONTAGION | `contagion` | `L6Co` | A | Fx | zero | optional (field source) | no | client | 3 |
| 7 | EMISSION | `emission` | `L6Em` | A | Fx | zero | no | no | client | 3 |
| 8 | APPARITION | `apparition` | `L6Ap` | A | Fx, Delay | fixed: 250 ms | no | no | no | 2 |
| 9 | ZEALOTS | `zealots` | `L6Ze` | A | Fx, Modulation | zero | optional (predator) | no | no | 5 |
| 10 | 100 RADS | `hundred_rads` | `L6Rd` | A | Fx, Dynamics | zero | no | no | no | 4 |
| 11 | SARCOPHAGUS | `sarcophagus` | `L6Sa` | A | Fx, Delay | zero | no | no | no | 5 |
| 12 | POLTERGEIST | `poltergeist` | `L6Po` | A | Fx, Spatial | zero | no | no | no | 5 |
| 13 | SNORK | `snork` | `L6Sn` | A | Fx | zero | optional (target) | no | no | 4 |
| 14 | GAUSS | `gauss` | `L6Ga` | A | Fx, Dynamics | fixed: 64 samples | no | no | no | 2 |
| 15 | PSY-DOG | `psy_dog` | `L6Pd` | A | Fx, Spatial | zero | no | no | no | 2 |
| 16 | RAINBOW | `rainbow` | `L6Rb` | B | Fx, Delay | zero | no | no | no | 1 |
| 17 | WISH GRANTER | `wish_granter` | `L6Wg` | B | Fx, Reverb | zero | no | no | no | 1 |
| 18 | CARRIER | `carrier` | `L6Cr` | B | Fx, Filter | zero | no | optional (pitch) | no | 2 |
| 19 | C-CONSCIOUSNESS | `c_consciousness` | `L6Cc` | B | Fx, Reverb | zero | no | no | no | 4 |
| 20 | ARTIFACT | `artifact` | `L6Ar` | B | Fx, Filter | zero | no | no | listener (reseed) | 4 |
| 21 | NUMBERS STATION | `numbers_station` | `L6Ns` | B | Fx, Filter | zero | no | no | no | 5 |
| 22 | OASIS | `oasis` | `L6Oa` | B | Fx, Restoration | fixed: 64 samples | no | no | no | 2 |
| 23 | CLEAR SKY | `clear_sky` | `L6Cs` | B | Fx, Modulation | zero | no | no | no | 5 |

Faction A: 15 effects. Faction B: 8. Bundle id `com.operationfairway.labx16.<slug>`. Product name
`LAB X-16 <NAME>`. Optional, un-numbered utility if wanted later: PSI-HELMET, the Regulator alone with a
NOT ADJUSTED mode (looser look-ahead), code `L6Ph`.

### 0.2 Conventions

- **Common parameters first, always:** `mix` (0-100 %, default 100), `output` (-24..+12 dB, 0), `bypass`
  (Off|On, latency-matched). Bus clients add `bus` (Off|On, default On) fourth. The effect's parameters follow in
  the order of its table. Ids are `snake_case`; names Title Case; units and formatting from X-19 `Params.h`.
- **Mix law:** linear crossfade, except reverbs and delays, which use the Noosphere equal-power law.
- **Sync lists:** `RATE` = the X-19 `lfo_sync` list (Free|1/1|1/2|1/4|1/8|1/16|1/32|1/4 T|1/8 T|1/16 T|1/4 D|1/8 D|
  2 bars|4 bars); `TIME` = the X-19 `delay_sync` list (Free|1/4|1/8|1/8 D|1/8 T|1/16|1/4 D|1/2) plus 3/16, 1/32 and
  1/1 appended; `BARS` = 1|2|4|8|16|32|64|128. Free means the companion ms or Hz parameter applies.
- **Randomness:** `hash01(bar, step, salt, callsign)`; never a free-running RNG for decisions. Noise generators
  may use the seeded xorshift `Rng` from `Common.h`.
- **Macros:** one per effect, the faction's accent colour, with the mod wheel added when MIDI is present.
- **Presets:** six to eight each, named from the lore, loudness-trimmed (`output` set so the RMS on the reference signal stays within 1 dB of the input at the preset's mix). Preset 0 is "Init".
- **Guard stages:** each spec names its stages in chain order; the base calls `guard(stage)` after each.
- **Smoothing:** frequencies and times log-domain; levels linear; choices none.

### 0.3 Common tests (every effect, `Tests/run_tests.py --plugin <slug>`)

| # | Test | Pass |
|---|---|---|
| C1 | Null at `bypass` On | output equals the input delayed by the reported latency within 1e-6 |
| C2 | Latency | reported equals measured by impulse at 44.1, 48 and 96 kHz and in every mode or oversampling setting; never changes during a 60 s automation fuzz |
| C3 | State round trip | every parameter and the blob restore exactly (X-19 test 15 pattern) |
| C4 | Preset switch during signal | largest sample jump below 0.5 and no fault (X-10 test 18 pattern) |
| C5 | Fuzz | 300 s random parameters, presets, transport jumps at 1, 7, 64, 3000 and variable blocks, 44.1 and 48 kHz: no fault, no non-finite, no growth in silence |
| C6 | Guard | `--inject stage@t` at every stage is caught, silenced, logged with the stage name |
| C7 | pluginval | strictness 5 and 10 |
| C8 | CPU | at least 20x real time at 48 kHz, 64-sample blocks, worst-case preset (thread CPU time) |
| C9 | Editor | PNG snapshot renders; every panel title in the spec appears |

Per-effect tests are numbered T1 and up below.

---

## FACTION A — THE DEEP ZONE

### 1. KAYMANOV // ИЗЛУЧАТЕЛЬ

Code `L6Ka` · Wave 1 · Fx, Distortion · latency fixed to the 4x oversampler path · stereo in/out · no bus.

**Imported from:** the Kaymanov emitter under the Yantar factory and its failing cooling pumps. Mechanism: X-10's
EMITTER, a split-band oversampled shaper that keeps the sub clean, plus COOLDOWN as a duty cycle that lets the drive
overheat on the grid.

**Signal flow**
```
in -> [oversample 1x|2x|4x] -> LR4 split at SPLIT -> low: sum to mono (SUB MONO) or keep stereo, untouched
                                                  -> high: pre-emphasis above 400 Hz (drive-scaled, up to +12 dB)
                                                           -> shaper (tube|clip|fold|rectify|crush) with BIAS
                                                           -> keep only the change: delta, DC block, HP at SPLIT
                                                           -> tilt EQ (TONE) -> + high
     -> low + high -> [downsample] -> latency pad to the 4x path -> HEAT meter -> mix
COOLANT: Cooldown gain curve (pump|gate|saw|sine, synced) modulates DRIVE in dB (and optionally the high band level)
```
Stages for the guard: `shaper`, `output`.

**Core modules:** `SplitBandDrive` (Emitter port with `LR4` from X-0 replacing the JUCE crossover), `Crush`,
`Cooldown`, `Transport`, `Oversampler` (juce_dsp), `Needle`.

**New DSP:** COOLANT maps the Cooldown curve c(t) in [1-depth, 1] to drive: `drive_eff = drive_db + cool_amount *
24 dB * (1 - c)`, so the drive peaks where the duty cycle dips (the pumps are off, the emitter heats). HEAT is a
one-pole follower of `drive_eff` shown on the needle; above 48 dB the needle enters the red arc. `sub_mono` Off
keeps the low band stereo (X-10 always summed).

**Parameters** (after the common three)

| id | name | range | default | unit | notes |
|---|---|---|---|---|---|
| `mode` | Mode | Tube\|Clip\|Fold\|Rectify\|Crush | Tube | | shaper |
| `drive` | Drive | 0..60 | 12 | dB | linear smooth |
| `bias` | Bias | -1..1 | 0 | % | asymmetry; in Crush trades bits for rate |
| `tone` | Tone | -1..1 | 0 | % | tilt around 900 Hz |
| `split` | Split | 20..1000 | 120 | Hz | log smooth; LR4 |
| `sub_mono` | Sub Mono | Off\|On | On | | |
| `oversample` | Oversample | Off\|2x\|4x | 2x | | latency padded to 4x; change via the preset fade |
| `cool_shape` | Coolant | Off\|Pump\|Gate\|Saw\|Sine | Off | | Cooldown shape |
| `cool_sync` | Coolant Rate | RATE without Free | 1/2 | | |
| `cool_depth` | Coolant Depth | 0..1 | 0.5 | % | |
| `cool_hold` | Coolant Hold | 0..1 | 0.4 | % | pump time constant or gate duty |
| `cool_amount` | Coolant Heat | 0..1 | 0.6 | % | how much the dip drives the shaper |
| `cool_target` | Coolant Target | Drive\|Level\|Both | Drive | | Level also dips the high band |
| `heat` | HEAT | 0..1 | 0 | % | macro: +24 dB drive, +0.3 bias, -0.3 tone, +0.3 cool_depth |

**UI:** Oxide theme. Panels EMITTER, COOLANT, SPLIT, HEAT (needle, red arc). The needle jitters more as drive rises.

**Presets:** Init, Cooling Pumps Off, Lefty's Repair (light tube, pumps steady), Zombified Bass (fold, 36 dB, split
90 Hz), Factory Floor (clip, gate 1/4), X-18 Components (rectify, bias), Full Power (crush, 4x, heat 1).

**Tests**
- T1 sub band: C1 note through the chain at 24 dB drive, band below SPLIT within 0.05 dB of dry (X-10 test 9).
- T2 energy above the split rises with drive; crush produces the expected level steps.
- T3 aliasing: folded 5 kHz sine, 4x at least 8 dB cleaner than Off above 10 kHz.
- T4 latency identical at Off, 2x and 4x (C2) and across `mode`.
- T5 COOLANT period at 128 BPM, 1/4 pump: 468.75 ms (X-10 test 12).

---

### 2. PSY-FIELD // ПСИ-ПОЛЕ

Code `L6Ps` · Wave 2 · Fx, Modulation · zero latency · stereo in/out · sidechain optional · MIDI optional.

**Imported from:** `zone_field_psychic`, the psy-emission that drains mental strength and fills the head with
voices, and the SoC mental-strength bar that empties with exposure. Mechanism: an exposure integrator that deepens
the effect the longer signal is present, over a vowel comb, whisper formants, the presence whine and anomalous
pitch drift.

**Signal flow**
```
in -> DRIFT (two-head micro shifter, cents from a Programmer walk x EXPOSURE) -> RESONATOR (vowel comb, pitch source)
   -> + WHISPER (noise through 3 BP at 1:2:3 of F0, level = envelope x EXPOSURE)
   -> + PRESENCE (3100 Hz sine + octave, 5.3 Hz vibrato, level = envelope x EXPOSURE)
   -> mix
EXPOSURE e: rises while the field source's envelope is above THRESHOLD, falls otherwise (see below)
```
Stages: `drift`, `resonator`, `output`.

**Core modules:** `MicroShift`, `Programmer`, `Resonator` (pitch source: fixed, MIDI, tracker), `Whisper`, `Presence`,
`Follower`, `SVF`.

**New DSP:**
- Exposure: `e += (env > thr ? dt / exposure_time : -dt / recovery_time)`, clamped to [0, 1]; `env` is a follower
  (5 ms attack, 120 ms release) of the field source: the input, or the sidechain when `field_source` is Sidechain
  and the bus is connected, else the input. `exposure_mode` Manual bypasses the integrator and uses the knob.
- Drift: cents = `drift_depth` × walk(t) × (0.3 + 0.7e); walk is the Programmer at `drift_rate`. Window 50 ms.
  The shifter's base delay is part of the sound; it is not reported as latency and the dry is not delayed.
- Resonator feedback = `res_feedback` + 0.3e, clamped 0.97; vowel = `vowel` + 0.2e (toward I).
- Pitch tracker (when `pitch_source` is Auto): autocorrelation on a 2 kHz low-passed mono copy every control
  block over a 40 ms window, 50-1000 Hz, with a 100 ms hold on confidence drops; falls back to `res_pitch`.
- UI tint follows e: Oxide phosphor shifts from hazard yellow toward a blue cast at e = 1 (SoC yellow, CoP blue).

**Parameters**

| id | name | range | default | unit | notes |
|---|---|---|---|---|---|
| `exposure` | Exposure | 0..1 | 0 | % | the macro; in Auto mode the knob shows e and adds to it |
| `exposure_mode` | Exposure Mode | Auto\|Manual | Auto | | |
| `exposure_time` | Onset Time | 1..60 | 12 | s | time in the field to full exposure |
| `recovery_time` | Recovery | 1..120 | 20 | s | time in silence back to zero |
| `threshold` | Field Threshold | -60..0 | -36 | dB | |
| `field_source` | Field Source | Input\|Sidechain | Input | | |
| `pitch_source` | Pitch Source | Fixed\|MIDI\|Auto | Fixed | | |
| `res_pitch` | Resonator Pitch | 30..2000 | 110 | Hz | log |
| `res_feedback` | Resonator Feedback | 0..1 | 0.4 | % | |
| `vowel` | Vowel | 0..1 | 0.3 | % | U-O-A-E-I |
| `res_level` | Resonator Level | 0..1 | 0.5 | % | |
| `whisper` | Whisper | 0..1 | 0.3 | % | |
| `whisper_f0` | Whisper Formant | 60..400 | 127 | Hz | log |
| `presence` | Presence | 0..1 | 0.15 | % | |
| `drift_depth` | Drift | 0..200 | 40 | ct | |
| `drift_rate` | Drift Rate | 0.02..2 | 0.15 | Hz | log |

**Presets:** Init, Yantar Approach (slow onset), Mental Strength Zero (manual 1), Controller's Den (vowel I, deep
drift), Red Forest Phantoms (whisper high), Kindergarten Jammer (presence, fast drift), Psy-block (recovery 2 s).

**Tests**
- T1 exposure timing: a -12 dBFS tone for `exposure_time` seconds brings e to 0.98-1.0; silence for
  `recovery_time` brings it under 0.02.
- T2 silence in, silence out (whisper and presence keyed by envelope).
- T3 resonator peak within 1 % of `res_pitch` at feedback 0.9; MIDI note 57 moves it to 220 Hz (if F5 passed).
- T4 drift: measured pitch of a 440 Hz tone wanders within ±`drift_depth` cents and never steps more than 2 cents
  per control block.
- T5 tracker: a 196 Hz saw sets the comb to 196 ±2 Hz within 200 ms.

---

### 3. DOSIMETER // ДОЗИМЕТР

Code `L6Do` · Wave 1 · Fx, Distortion · zero latency · stereo in/out.

**Imported from:** radiation and the detectors that tick with it. Mechanism: Poisson Geiger clicks whose rate
follows the input level, dust and hiss that follow the envelope, and one crush primitive, all in one lo-fi stage.

**Signal flow**
```
in -> CRUSH (bits, hold) -> MEDIUM tone cap -> + GEIGER (Poisson bursts through a BP at TONE, rate from DOSE x envelope^TRACK)
   -> + DUST (crackle + hiss x envelope) -> mix ; needle shows the click rate
```
Stages: `crush`, `output`.

**Core modules:** `Crush`, `Geiger`, `Dust`, `Follower`, `SVF`, `Needle`.

**New DSP:** click rate `r = dose² × 400 /s × env^track`, env normalised to 0-1 over -60..0 dBFS; each click is
noise with a 0.35 ms decay (X-3) through the band-pass; `stereo` Stereo uses an independent RNG per channel,
Mono shares one. `medium`: Clean, Tape (6.5 kHz low-pass), Radio (3 kHz), Phone (300-3400 Hz band). The needle
follows a 100 ms count of clicks; its jitter rises with the count.

**Parameters**

| id | name | range | default | unit | notes |
|---|---|---|---|---|---|
| `dose` | Dose | 0..1 | 0.3 | % | the macro |
| `track` | Track | 0..1 | 0.7 | % | 0 constant rate, 1 follows the input |
| `tone` | Click Tone | 500..8000 | 3000 | Hz | log |
| `click_level` | Click Level | 0..1 | 0.5 | % | |
| `dust` | Dust | 0..1 | 0.2 | % | crackle and hiss |
| `bits` | Bits | 1..16 | 16 | | linear |
| `hold` | Rate Divide | 1..64 | 1 | x | sample-hold |
| `crush_mix` | Crush Mix | 0..1 | 1 | % | |
| `medium` | Medium | Clean\|Tape\|Radio\|Phone | Clean | | |
| `stereo` | Clicks | Mono\|Stereo | Stereo | | |

**Presets:** Init, Background Count, Garbage Heap, Agroprom Tunnels (phone), Pripyat Hotspot (dose 1), Burnt Fuzz
(bits 5), Clicks Only (crush off).

**Tests**
- T1 click rate at track 0: within 20 % of `dose² × 400` per second over 20 s.
- T2 silence in: no dust, no hiss; clicks continue only when track < 1.
- T3 crush: a -6 dBFS sine at 4 bits shows quantisation steps of 1/8 full scale.
- T4 medium: Phone kills energy below 250 Hz and above 4 kHz by at least 24 dB.

---

### 4. SILO // ШАХТА

Code `L6Si` · Wave 1 · Fx, Reverb · zero latency · stereo in/out · tail reported.

**Imported from:** X-16's circular, silo-shaped emitter chamber with a switch on every level, and the drainage
tunnels under the factory. Mechanism: the Noosphere FDN at small size with a modal blend that strengthens room
modes, discrete LEVELS stepping the geometry, a wet ducked by the input, and a TUNNEL mode of narrow multi-taps.

**Signal flow**
```
in -> pre-delay (0-30 ms) -> 80 Hz HP -> FDN (8 lines, size 1-6 m, matrix = (1-modal) Hadamard + modal Identity, damping)
   -> wet x duck(env) -> equal-power mix
TUNNEL: in -> 6 taps (23, 41, 67, 89, 113, 151 ms x LENGTH) with BP 300-3000 Hz and feedback 0.3-0.8 -> wet
```
Stages: `fdn`, `output`.

**Core modules:** `Fdn` (modal blend), `DelayLine`, `Follower`, `SVF`.

**New DSP:** `modal` blends the mixing matrix between Hadamard (diffuse) and identity (each line its own
resonance; the room "rings"); the matrix is renormalised so the spectral norm stays 1. `levels` 1-5 picks the
base line lengths from five tables scaled from the Noosphere set (level 1 = 0.35x, level 5 = 1.0x of the small
set), so stepping it is audible as a staircase, not a slider. `duck`: wet gain = 1 - duck × env (10 ms attack,
`duck_release` ms release), the claustrophobic pumping of a room that only speaks between hits.

**Parameters**

| id | name | range | default | unit | notes |
|---|---|---|---|---|---|
| `levels` | Level | 1..5 | 3 | | integer; steps the chamber |
| `size` | Size | 0..1 | 0.3 | % | within the level |
| `decay` | Decay | 0.1..6 | 1.2 | s | RT60, log |
| `damping` | Damping | 0..1 | 0.5 | % | |
| `modal` | Modal | 0..1 | 0.3 | % | |
| `predelay` | Pre-delay | 0..30 | 4 | ms | |
| `duck` | Duck | 0..1 | 0.4 | % | |
| `duck_release` | Duck Release | 20..1000 | 180 | ms | log |
| `tunnel` | Tunnel | Off\|On | Off | | multi-tap mode |
| `tunnel_length` | Tunnel Length | 0.5..2 | 1 | x | scales the taps |
| `tunnel_feedback` | Tunnel Feedback | 0..0.8 | 0.5 | % | |
| `claustrophobia` | Claustrophobia | 0..1 | 0 | % | macro: -size, +modal, +duck, +damping |

**Presets:** Init, Emitter Chamber, Level Three Switch, Controller's Storage (tiny, modal), Drainage Tunnel
(tunnel), Elevator Shaft (level 5, long), Airlock (duck 1).

**Tests**
- T1 RT60 within 15 % of `decay` at modal 0 (impulse, Schroeder integration).
- T2 ducking: wet level during a -6 dBFS burst at least 12 dB below its level 300 ms after at duck 1.
- T3 levels: line lengths strictly increase from 1 to 5 (harness readout).
- T4 tunnel tap times within 0.1 ms of the table × length.
- T5 tail decays after the transport stops (64-sample blocks), delay self-test clean.

---

### 5. ANOMALY // АНОМАЛИЯ

Code `L6An` · Wave 2 · Fx, Delay · zero latency · stereo in/out · MIDI optional.

**Imported from:** the anomalies that bend space and time, and the emission as the Zone's clock. Mechanism:
X-0's TIME and VOLUME lanes as a standalone Gross Beat-style slicer.

**Signal flow**
```
in -> HistoryBuffer (16 s) -> two read heads with 2.5 ms crossfades driven by TIME slots:
     Springboard 1/4..1/64 repeat, Whirligig 1/8 and 1/16 scratch (SCRATCH depth), Vortex reverse, Comet tape stop,
     Jelly half speed, Burner accelerating roll, Bubble random stutter
   -> VOLUME slots: Electro gates 1/8..1/32, fade in, fade out, duck, tremolo 1/16, mute
   -> mix
Regions come from an 8-pattern, 2-lane step grid (state blob), or momentary MIDI slot holds.
```
Stages: `anomaly`, `output`.

**Core modules:** `Anomaly` curves behind `RegionSource`, `DelayLine` (stereo Hermite with sentinel), `Transport`,
`Hash`; UI `LaneGrid`.

**New DSP:** `RegionSource` implemented by `LaneGrid` state: 8 patterns × 2 lanes × 64 steps at resolution
1/8, 1/16 or 1/32 and length 16, 32 or 64; regions are runs of identical slots (X-0 rule); boundary snapping and
the block-edge lesson from X-0 0.1.1 carried over. MIDI: notes 60-71 hold TIME slots 1-12, 72-83 hold VOLUME slots
1-12 while the key is down (if F5 passed; else the parameters remain). `run` Host follows FL's bar position; Free
uses an internal clock at the last tempo.

**Parameters**

| id | name | range | default | unit | notes |
|---|---|---|---|---|---|
| `pattern` | Pattern | 1..8 | 1 | | integer; switches at the next pattern boundary |
| `resolution` | Resolution | 1/8\|1/16\|1/32 | 1/16 | | per pattern in state; this selects the edit pattern's |
| `length` | Length | 16\|32\|64 | 16 | | |
| `scratch_depth` | Scratch | 0..1 | 0.6 | % | |
| `run` | Run | Host\|Free | Host | | |
| `time_mix` | Time Mix | 0..1 | 1 | % | TIME lane wet |
| `volume_mix` | Volume Mix | 0..1 | 1 | % | VOLUME lane depth |
| `field` | Field | 0..1 | 0 | % | macro: +scratch depth, +time_mix, randomised slot picks per bar from the hash |

**UI:** LaneGrid with the TIME palette (anomaly names) and VOLUME palette, pattern buttons 1-8, tools copy,
paste, clear, shuffle (hashed by bar so FL renders repeat).

**Presets:** Init, Springboard Field, Whirligig Alley, Vortex Pull, Comet Trail, Jelly Pond, Burner Row, Bubble
Cluster (pattern content lives in the blob; presets carry patterns).

**Tests**
- T1 slot models: Springboard, Vortex, Jelly, Comet match their analytic models at 30 dB SNR (X-0 test 19).
- T2 history reads: 2 M random reads inside the buffer, worst error below 1e-6 (X-0 test 20).
- T3 pattern switch on block-edge boundaries at 120, 150 and 160 BPM with 64-sample blocks (X-0 test 30).
- T4 state blob round trip with 8 patterns.
- T5 MIDI hold selects a slot for the note's duration (if F5 passed).

---

### 6. CONTAGION // ЗАРАЖЕНИЕ

Code `L6Co` · Wave 3 · Fx · zero latency · stereo in/out · sidechain optional · Zone Bus client.

**Imported from:** epidemiology, the SIR compartment model. Lore: zombified stalkers spreading out of Yantar;
"everyone who is affected by it loses his mind and turns into a zombie" (Sakharov, Clear Sky); the psi-helmet
as immunity.

**Mechanism.** Every instance is a stalker with a state: Susceptible, Infected, Recovered. Infection enters by
`role` Patient Zero, by a field source crossing `threshold`, or from peers through the bus. An infected instance
degrades its audio along a staged symptom curve and publishes viral load; others catch it at bar boundaries with
a probability set by R0 and their immunity; they recover, stay immune for `wane` bars, then become susceptible.

**State machine (evaluated at every bar boundary, from loads published before the bar)**
- Susceptible → Infected with `p = (1 - immunity) × (1 - exp(-(r0 / recovery) × L))`, `L` = sum of the loads
  of infected peers (excluding self), roll `hash01(bar, 0, "infect", callsign) < p`. Patient Zero infects itself
  when `role` switches to Patient Zero (edge) and the field source infects when its envelope exceeds `threshold`
  for a whole bar.
- Infected, time since infection `t` in bars: incubation `[0, inc)`: load ramps 0 → 0.5, symptoms 0; fever
  `[inc, inc + 0.6 × recovery)`: load 1, symptoms ramp 0 → 1 (curve per `stage_curve`); zombified
  `[.., inc + recovery)`: symptoms 1, load 0.7. Then Recovered: symptoms decay to 0 over one bar.
- Recovered → Susceptible after `wane` bars (0 = never). `role` Carrier: always load 1, never symptomatic.
- Alone (no peers): the same timeline runs locally; Patient Zero and the field source still infect self.

**Symptoms chain (s in 0..1)**
```
in -> DRIFT (MicroShift, ±80 ct x s random walk) -> CHOKE (SVF LP 20 kHz -> 1.2 kHz x sqrt(s)) -> GRIT tube 0 -> 24 dB x s, corner 300 Hz
   -> CRUSH bits 16 -> 16 - 10 s, hold 1 -> 1 + 7 s -> + WHISPER (s x envelope) -> level -3 dB x s -> mix x symptoms macro
```
Stages: `drift`, `choke`, `grit`, `output`.

**Core modules:** `ZoneBus Client`, `MicroShift`, `SVF`, `ParallelGrit`, `Crush`, `Whisper`, `Follower`, `Hash`.

**Parameters**

| id | name | range | default | unit | notes |
|---|---|---|---|---|---|
| `role` | Role | Stalker\|Patient Zero\|Carrier | Stalker | | Patient Zero edge triggers self-infection |
| `r0` | R0 | 0..8 | 2 | | |
| `immunity` | Immunity | 0..1 | 0 | % | the psi-helmet |
| `incubation` | Incubation | 0..8 | 1 | bars | |
| `recovery` | Recovery | 1..32 | 8 | bars | |
| `wane` | Wane | 0..64 | 16 | bars | 0 = permanent immunity |
| `symptoms` | Symptoms | 0..1 | 1 | % | the macro: scales the whole chain |
| `stage_curve` | Course | Linear\|Fever\|Collapse | Fever | | symptom ramp shape |
| `field_source` | Field Source | None\|Input\|Sidechain | None | | |
| `threshold` | Field Threshold | -60..0 | -24 | dB | |
| `cure` | Cure | Off\|On | Off | | edge: instance returns to Susceptible now |

**UI:** PEERS infection map (dots per callsign, coloured S/I/R), stage readout, load bar; Oxide theme.

**Presets:** Init, Patient Zero, Yantar Outbreak (R0 4), Slow Zombification (incubation 4), Psi-helmet Issued
(immunity 0.8), Carrier Only, Pandemic R8.

**Tests** (harness `--instances 8`)
- T1 R0 = 0: no peer ever infected over 64 bars.
- T2 R0 = 8, one Patient Zero: all 7 susceptible peers infected within 16 bars; immunity 1 peers never.
- T3 determinism: two identical runs produce bit-identical outputs for every instance.
- T4 symptoms 1: bits measured at 6, cutoff near 1.2 kHz, drift within ±80 cents.
- T5 solo: one instance, Patient Zero, runs the full timeline and returns to Susceptible after wane.

---

### 7. EMISSION // ВЫБРОС

Code `L6Em` · Wave 3 (build first) · Fx · zero latency · stereo in/out · Zone Bus client.

**Imported from:** seismology and civil-defence timing: sirens give a lead, the P-wave arrives, then the S-wave,
then aftermath. Lore: emissions, sirens and radio warnings, shelters, Anabiotics that let a stalker sleep through
one.

**Mechanism.** One instance with `role` Siren schedules an emission on the host grid and posts it on the bus;
every instance (the siren included) renders the same phases from its own bar position, scaled by `severity` and
by `1 - shelter`. Alone, an instance with `role` Siren still runs its own schedule.

**Phases** (F = fire point, a bar start; all depths × severity × (1 - shelter))
- Warning `[F - lead bars, F)`: SIREN layer (350, 705, 1055 Hz triad with 0.7 Hz tremolo, plus noise through a
  band-pass sweeping 800 → 3000 Hz) rises from silence to `siren_level`; the program gets a slow 20 → 120 Hz
  high-pass rise.
- P-wave `[F, F + 120 ms]`: program gain +12 dB with a 60 ms decay, white burst at -12 dBFS, crush bits 6,
  Transient attack +0.8.
- S-wave `[F, F + pressure_time]`: low-pass from 20 kHz down to 400 Hz over the first 30 %, back up over the rest
  (exponential segments); Regulator at amount 1; width 1 → 0 (mono collapse) following the low-pass.
- Aftermath `[F + pressure_time, + aftermath_time]`: level -18 dB recovering exponentially; a small FDN bloom at
  wet 0.5 decaying to 0.
- Anabiotics On: fade to silence in 20 ms at `F - 0.25 beat`, return with a 200 ms fade at the end of aftermath.
- Repeat: when `repeat` is not Off, the next fire point is `F + repeat bars`, published by the siren at each fire.

**Scheduling.** `arm` Off → On edge on the siren: at the next bar boundary it posts
`EmissionSchedule { fireBar = bar + countdown, lead, severity, pressure, aftermath, repeat, seq }`. Listeners use
the newest `seq`. Because phases are functions of bar position, host loops replay the emission when they cross F.
Stages: `program`, `output`.

**Core modules:** `ZoneBus Client`, `Siren` generator, `Crush`, `Transient`, `SVF`, `Regulator`, `Fdn` (small), `Transport`.

**Parameters**

| id | name | range | default | unit | notes |
|---|---|---|---|---|---|
| `role` | Role | Listener\|Siren | Listener | | |
| `arm` | Arm | Off\|Armed | Off | | siren only; edge |
| `countdown` | Countdown | 1..64 | 8 | bars | |
| `lead` | Siren Lead | 1..8 | 2 | bars | |
| `repeat` | Repeat | Off\|8\|16\|32\|64\|128 | Off | bars | |
| `severity` | Severity | 0..1 | 0.7 | % | siren's value is published |
| `pressure_time` | Pressure | 0.5..8 | 2.5 | s | |
| `aftermath_time` | Aftermath | 0.5..16 | 4 | s | |
| `shelter` | Shelter | 0..1 | 0 | % | per instance; the macro, inverted sense: 1 = safe |
| `siren_level` | Siren Level | 0..1 | 0.5 | % | |
| `anabiotics` | Anabiotics | Off\|On | Off | | |

**UI:** COUNTDOWN view (bars to F, phase name), PEERS, needle for pressure; the whole panel flashes the accent in
the P-wave.

**Presets:** Init, Standard Blowout, Shelter in Bunker (shelter 0.9), Open Field (shelter 0, severity 1),
Periodic Sixteen, Anabiotics, Severe Front (pressure 8 s).

**Tests** (`--instances 4`, instance 0 Siren)
- T1 arm at bar 4 with countdown 4: every instance shows the P-wave burst at bar 8 within one sample.
- T2 shelter 1: output equals input within 0.01 dB throughout.
- T3 repeat 8: fire points at bars 8, 16, 24.
- T4 anabiotics: silence from F - 0.25 beat to the end of aftermath, then signal returns.
- T5 solo siren: one instance, same timeline.

---

### 8. APPARITION // ПРИЗРАК

Code `L6Ap` · Wave 2 · Fx, Delay · latency fixed 250 ms (round(0.25 × sr)) · stereo in/out.

**Imported from:** forecasting, and a deliberate use of plugin delay compensation: with a reported latency the
output can contain events that precede the input in song time. Lore: the apparitions around the Scorcher that
vanish when attacked; Ghost's body in X-16.

**Mechanism.** The input is written to a look-ahead buffer; the output head reads 250 ms behind. Onsets detected
at the write head are events in the output's future: pre-echoes, a reverse swell and a rising whisper are added
into the output buffer before the hit. PHANTOM adds decorrelated doubles that vanish on attacks.

**Signal flow**
```
in -> look-ahead ring (write head) ; onset detector at the write head (dual envelope, SENSITIVITY, 50 ms minimum gap,
      confirmed SWELL_LEN later)
   on onset at t: add to the output accumulation ring
      PRE-ECHO: grain of [t, t + GRAIN] at t - k x ECHO_TIME, k = 1..COUNT, level ECHO_LEVEL x FEEDBACK^(k-1)
      SWELL: time-reversed [t, t + SWELL_LEN] placed at [t - SWELL_LEN, t), fade-in, SWELL_TONE low-pass
      WHISPER: rising formant noise from t - WHISPER_LEAD to t
   output head (t - 250 ms): dry + accumulation ; PHANTOM: 2 Schroeder allpass chains (5-23 ms) L/R, gain x (1 - attack)
```
Constraint: `swell_len + whisper_lead + count × echo_time ≤ 240 ms` is enforced by clamping; the UI shows the
budget. Stages: `apparition`, `output`.

**Core modules:** `DelayLine`, `Onset`, `Follower`, `Whisper`, allpass from `Fdn` internals, `LatencyAligner`.

**Parameters**

| id | name | range | default | unit | notes |
|---|---|---|---|---|---|
| `sensitivity` | Sensitivity | 0..1 | 0.5 | % | onset threshold |
| `echo_level` | Pre-echo | 0..1 | 0.5 | % | |
| `echo_sync` | Pre-echo Time | TIME | 1/16 | | Free uses ms |
| `echo_ms` | Pre-echo ms | 10..200 | 60 | ms | |
| `echo_count` | Pre-echo Count | 1..6 | 3 | | integer |
| `echo_feedback` | Pre-echo Decay | 0..0.9 | 0.6 | % | |
| `grain` | Grain | 10..120 | 40 | ms | log |
| `swell_level` | Swell | 0..1 | 0.4 | % | |
| `swell_len` | Swell Length | 20..200 | 90 | ms | |
| `swell_tone` | Swell Tone | 500..16000 | 5000 | Hz | log |
| `whisper_level` | Whisper | 0..1 | 0.2 | % | |
| `whisper_lead` | Whisper Lead | 50..200 | 120 | ms | |
| `phantom` | Phantom | 0..1 | 0.3 | % | |
| `haunt` | Haunt | 0..1 | 0 | % | macro: all levels up, sensitivity up |

**Presets:** Init, Red Forest Walk, Ghost's Trail (echoes), Pre-echo Drums, Reverse Choir (swell), Phantom Pack,
Vanishing.

**Tests**
- T1 C2 at 44.1/48/96 kHz: reported = measured = round(0.25 × sr).
- T2 impulse at 1.000 s: after compensation, pre-echoes at 1.000 - k × 60 ms within one sample, swell energy
  entirely inside [0.910, 1.000).
- T3 phantom gain within 50 ms after a transient at least 20 dB below its gain in a sustained tone.
- T4 budget clamp: with count 6 at 200 ms, nothing is written before `t - 240 ms`.

---

### 9. ZEALOTS // ФАНАТИКИ

Code `L6Ze` · Wave 5 · Fx, Modulation · zero latency · stereo in/out · sidechain optional (predator).

**Imported from:** flocking simulation (Reynolds' boids: cohesion, separation, alignment). Lore: Monolith
fighters chanting as one before the stone, scattering under fire.

**Mechanism.** Up to 16 agents each read the mono-summed input from one shared delay line at their own delay
`d_i` (position) with their own pan `y_i`. A moving read head is a pitch change, so an agent's velocity in delay
space is its detune: `dd/dt = -(2^(v/1200) - 1)` with `v` in cents. The boids rules steer `v` and the pan
velocity at control rate. Nothing else is needed: 16 steered chorus voices.

**Rules (every control block, dt = n / sr)**
- cohesion: `v_i += k_c × (d_i - leader_delay) × dt × 1000` (toward the leader's delay);
  pan: `w_i += k_c × (0 - y_i) × dt`
- alignment: `v_i += k_a × (mean v - v_i) × dt`
- separation: for each j with `|d_i - d_j| < sep`: `v_i += k_s × sign(d_i - d_j) × (1 - |d_i - d_j| / sep) × dt × 1000`
- damping `v_i *= 1 - 0.5 dt`; clamp `|v_i| ≤ max_detune`; reflect at `d_min` 2 ms and `d_max` 60 ms
- zealotry z: `k_c = 0.5 + 5z`, `k_s = 0.2 + 2(1 - z)`, `k_a = 1`; `sep = 1.5 ms`
- predator: on a transient of the input or the sidechain (dual envelope), `v_i += fear × hash-signed max_detune`,
  `w_i += fear × hash-signed 1`
Output: `Σ agents / sqrt(N) × wet`, panned by `y_i × spread`, plus dry. Stages: `flock`, `output`.

**Core modules:** `DelayLine` (Hermite), `Follower`/`Onset`, `panGains`, `Hash`; UI `FlockView` (agents as dots
in delay × pan space).

**Parameters**

| id | name | range | default | unit | notes |
|---|---|---|---|---|---|
| `agents` | Agents | 2..16 | 12 | | integer |
| `zealotry` | Zealotry | 0..1 | 0.5 | % | the macro |
| `max_detune` | Max Detune | 0..100 | 25 | ct | |
| `leader_delay` | Leader | 5..40 | 12 | ms | |
| `spread` | Spread | 0..1 | 0.8 | % | pan range |
| `fear` | Fear | 0..1 | 0.5 | % | predator scatter |
| `predator_source` | Predator | Off\|Input\|Sidechain | Input | | |
| `chant` | Chant | 0..1 | 0 | % | vowel O formant bank on the flock sum |

**Presets:** Init, Chant, Scattered Crowd (zealotry 0), Procession, Under Fire (fear 1), Monolith Wall (16 agents,
zealotry 1), Whisper Congregation.

**Tests**
- T1 zealotry 1: detune spread across agents below 2 cents after 4 s (pitch tracks of a 440 Hz tone).
- T2 zealotry 0: spread near `max_detune`.
- T3 predator: spread jumps on a transient and halves within 2 s.
- T4 reads never leave the buffer (sentinel self-test with the flock running 10 min).

---

### 10. 100 RADS // 100 РЕНТГЕН

Code `L6Rd` · Wave 4 · Fx, Dynamics · zero latency · stereo in/out.

**Imported from:** auction theory, a second-price auction with a budget and wealth. Lore: the 100 Rads bar,
Barkeep's cut, Sidorovich's prices, bandits taking their share.

**Mechanism.** Six bands bid every control block for gain out of a fixed loudness budget. Winners get boosted
and pay the second-highest bid from their wealth; losers get cut and save. Loudness moves around the spectrum
instead of being held still.

**Algorithm (per control block)**
- bands from five LR4 crossovers: 120, 300, 800, 2000, 5000 Hz (the outer two set by `low_split`, `high_split`,
  the inner three interpolated geometrically); `e_b` = band RMS in dB over a 30 ms follower, mapped -60..0 → 0..1.
- bid `b_b = e_b × (1 + hunger × W_b)`; wealth `W_b` in [0, 1], initial 0.5.
- winners = top `slots` bids; price = the next bid down. Per beat: winners `W -= greed × price`, losers
  `W += tax × (1 - W)`, clamped.
- target gains: winners `+boost dB`, losers `-cut × (1 - 0.5 W_b) dB`; then subtract the energy-weighted mean
  so `Σ e_b g_b = 0` (loudness kept), allowing up to `budget` dB of net rise.
- gains smoothed with `speed` (attack 1-50 ms, release 20-500 ms), scaled by `depth`, applied per band, bands
  summed. At depth 0 the stage hands back its input (X-0 multiband rule).
Stages: `auction`, `output`.

**Core modules:** `LR4`, `Follower`, `Hash` (tie-break).

**Parameters**

| id | name | range | default | unit | notes |
|---|---|---|---|---|---|
| `depth` | Depth | 0..1 | 0.7 | % | the macro |
| `slots` | Winners | 1..3 | 1 | | integer |
| `boost` | Boost | 0..12 | 6 | dB | |
| `cut` | Cut | 0..24 | 9 | dB | |
| `hunger` | Hunger | 0..1 | 0.5 | % | |
| `greed` | Greed | 0..1 | 0.5 | % | price paid per beat |
| `tax` | Tax | 0..1 | 0.3 | % | losers' savings per beat |
| `speed` | Speed | 0..1 | 0.5 | % | attack and release |
| `budget` | Budget | 0..6 | 0 | dB | allowed net rise |
| `low_split` | Low Split | 60..300 | 120 | Hz | log |
| `high_split` | High Split | 2000..10000 | 5000 | Hz | log |

**UI:** six RADS wealth bars with the winner lit, a market needle.

**Presets:** Init, Barkeep's Cut, Fair Trade (tax 1), Bandit Tax (cut 24), Sidorovich (slow), Winner Takes All
(tax 0), Quiet Market (depth 0.3).

**Tests**
- T1 depth 0: bands sum flat within 0.02 dB (X-0 test 21 pattern).
- T2 static six-tone input, tax 0.3: the winning band changes at least 4 times in 32 beats; tax 0: never.
- T3 loudness conservation: LUFS change within 1 dB at budget 0 on pink noise.
- T4 wealth stays in [0, 1]; gains within [-cut, +boost].

---

### 11. SARCOPHAGUS // САРКОФАГ

Code `L6Sa` · Wave 5 · Fx, Delay · zero latency · stereo in/out.

**Imported from:** radioactive decay chains, the Bateman equations. Lore: Reactor 4 under its sarcophagus, the
long tail of what was deposited.

**Mechanism.** Each onset deposits a parent isotope. Up to four generations decay into each other with their own
half-lives; each generation emits particles, grains of the onset's own audio, as Poisson events whose rate follows
its activity `λ_k N_k`. The hit's tail is dense and fast first, then sparse and slow, pitched and darkened per
generation.

**Algorithm**
- onset at sample `t0` with energy `E` (dual-envelope detector): capture start index in the history buffer;
  `N_1 += E × dose`.
- per control block: `N_1' = -λ_1 N_1`, `N_k' = λ_{k-1} N_{k-1} - λ_k N_k`, `λ_k = ln2 / (t_half_k × half_life)`,
  base half-lives 0.05, 0.3, 1.5, 6 s.
- emission: each generation k spawns grains with Poisson rate `activity × 50/s × λ_k N_k` (hashed per block);
  grain: Hann window of `grain` ms from the deposited onset position, pitch ratio `2^(k × pitch_step / 12)`,
  level `E × attenuation^(k-1)`, low-pass `20 kHz × (1 - darken)^(k-1)`, pan hashed within `spread`.
- 32 grain players, oldest stolen; deposits older than 10 × the longest half-life are dropped.
Stages: `chain`, `output`.

**Core modules:** `DelayLine` (history), `Onset`, grain reader (from X-3 `GrainCloud` reading logic), `SVF`, `Hash`.

**Parameters**

| id | name | range | default | unit | notes |
|---|---|---|---|---|---|
| `dose` | Dose | 0..1 | 0.6 | % | deposit per onset, the macro |
| `activity` | Activity | 0..1 | 0.5 | % | event density |
| `half_life` | Half-life | 0.1..10 | 1 | x | scales all generations, log |
| `daughters` | Daughters | 1..4 | 3 | | integer |
| `pitch_step` | Pitch Step | -12..12 | -5 | st | per generation |
| `attenuation` | Attenuation | 0.3..1 | 0.7 | x | level per generation |
| `darken` | Darken | 0..1 | 0.3 | % | |
| `grain` | Grain | 30..120 | 60 | ms | |
| `spread` | Spread | 0..1 | 0.7 | % | |
| `sensitivity` | Sensitivity | 0..1 | 0.5 | % | onset threshold |

**Presets:** Init, Reactor Four, Short Chain (1 daughter), Heavy Daughters (-12 st), Cesium Tail (half-life 10),
Sparse Decay, Dense Crackle.

**Tests**
- T1 one click: event rate per generation over time matches the Bateman solution within 20 % over four
  half-lives (counted in 100 ms bins).
- T2 after 10 longest half-lives: no events.
- T3 grains stay inside the history buffer; sentinel clean.
- T4 pitch of generation-2 grains at `pitch_step × 2` within 10 cents.

---

### 12. POLTERGEIST // ПОЛТЕРГЕЙСТ

Code `L6Po` · Wave 5 · Fx, Spatial · zero latency · stereo in/out.

**Imported from:** rigid-body physics with gravity, restitution and Doppler. Lore: poltergeists hurling crates
across the X-labs, the Burer's telekinesis.

**Mechanism.** An onset spawns an object carrying a grain of itself. The object is thrown; it flies in a room
(x stereo -1..1, y height 0..3 m, z depth 0..1), falls under gravity, bounces off floor and walls, and makes a
sound at every impact: the grain replayed at a level set by impact speed, panned by x, filtered and attenuated by
depth, Doppler-shifted by radial velocity. RAGE adds telekinetic lifts.

**Algorithm**
- spawn on onset (max `objects`, oldest replaced): position `(0, 1, 0.3)`, velocity `throw × hash-random
  direction × 6 m/s`; capture grain `[t0, t0 + 80 ms]`.
- integrate at control rate: `v_y -= gravity dt`; walls at x = ±1 and z in [0, 1] reflect with `bounce`;
  floor at y = 0 reflects with `bounce`, and an impact with `|v_y| > 0.3 m/s` replays the grain at level
  `min(1, |v_y| / 6)`; objects die after `life` s or when `|v| < 0.2` on the floor.
- rendering per replay: constant-power pan from x; depth: low-pass `20 kHz → 2 kHz` and level `1 / (1 + 3z)`;
  Doppler ratio `1 - doppler × v_radial / 40` (exaggerated speed of sound), applied as the grain's playback
  ratio; height adds a +3 dB shelf at 6 kHz × y/3.
- RAGE: Poisson lifts at `rage × 2/s` (hashed) add `+5 m/s` upward and a random lateral kick to every object.
Stages: `objects`, `output`.

**Core modules:** `DelayLine` (grain capture), `Onset`, grain reader, `SVF`, `panGains`, `Hash`; UI `ObjectsView`.

**Parameters**

| id | name | range | default | unit | notes |
|---|---|---|---|---|---|
| `throw` | Throw | 0..1 | 0.6 | % | initial speed |
| `gravity` | Gravity | 1..30 | 9.8 | m/s² | |
| `bounce` | Bounce | 0..0.95 | 0.6 | % | restitution |
| `life` | Life | 0.5..8 | 3 | s | |
| `objects` | Objects | 1..12 | 6 | | integer |
| `doppler` | Doppler | 0..1 | 0.5 | % | |
| `depth_filter` | Depth | 0..1 | 0.6 | % | distance filtering strength |
| `rage` | Rage | 0..1 | 0 | % | the macro: telekinetic lifts |
| `sensitivity` | Sensitivity | 0..1 | 0.5 | % | |

**Presets:** Init, Lab X-18 Lobby, Thrown Crates, Burer Lift (rage 0.8), Low Gravity (2 m/s²), Rubber Room
(bounce 0.95), Rage.

**Tests**
- T1 one click at throw 0: bounce count and intervals match free fall from 1 m with restitution `bounce` within
  10 % (first interval 0.9 s at 9.8 m/s²).
- T2 Doppler: an object approaching reads above the original pitch, receding below (sign check).
- T3 pans stay within [-1, 1]; levels never exceed the grain's level.
- T4 grain reads bounded.

---

### 13. SNORK // СНОРК

Code `L6Sn` · Wave 4 · Fx · zero latency · stereo in/out · sidechain optional (target).

**Imported from:** genetic algorithms: a population of genomes, fitness, tournament selection, crossover,
mutation, elitism. Lore: the mutants bred in X-18, the snork that was once a soldier.

**Mechanism.** Eight genomes each define a small effect chain. All eight chains run in parallel on the input; the
one with the best fitness over the last generation plays. Every `generation` bars the population breeds toward a
goal: brighter, darker, louder, quieter, wider, or matching the sidechain's spectral features.

**Genome (8 genes in 0..1)** high-pass 20-2000 Hz, low-pass 500-20000 Hz, tilt -12..+12 dB, drive 0-24 dB
(tube), crush bits 16-4, chorus depth (Ensemble) 0-1, delay 0-250 ms with feedback 0-0.6, pitch shift -12..+12 st
(MicroShift). Chain order as listed.

**Fitness.** Features per generation: log spectral centroid, RMS dB, crest dB, 8-band tilt vector, inter-channel
correlation. Goal targets: Brighter = centroid above the input's by `drive_goal` octaves; Darker = below;
Louder/Quieter = RMS ±6 dB; Wider = correlation toward 0; Match Sidechain = the sidechain's feature vector (falls
back to the input's when no sidechain). Fitness = negative weighted distance.

**Breeding (at the generation boundary, hashed by bar and `seed`)**: keep 2 elites; fill the rest by tournament
(size 3) crossover (uniform, rate `crossover`) and gaussian mutation (σ = 0.2 × `mutation`), genes clamped.
The best genome becomes alpha; the output crossfades to it over 50 ms. `evolve` Off freezes the population.
Stages: `alpha`, `output`.

**Core modules:** `SVF`, `Shapers` tube, `Crush`, `Ensemble`, `EchoDelay`, `MicroShift`, `Follower`, `Hash`.

**Parameters**

| id | name | range | default | unit | notes |
|---|---|---|---|---|---|
| `goal` | Goal | Brighter\|Darker\|Louder\|Quieter\|Wider\|Match Sidechain | Brighter | | |
| `drive_goal` | Goal Strength | 0..1 | 0.5 | % | how far the target sits from the input |
| `generation` | Generation | 1..8 | 2 | bars | |
| `population` | Population | 2..8 | 8 | | integer; CPU scales with it |
| `mutation` | Mutation | 0..1 | 0.3 | % | the macro |
| `crossover` | Crossover | 0..1 | 0.5 | % | |
| `evolve` | Evolve | Off\|On | On | | Off freezes |
| `seed` | Strain | 0..9999 | 1 | | integer; hashed with the bar |
| `instinct` | Instinct | 0..1 | 0 | % | blends the alpha with the best-ever genome |

**UI:** population strip (fitness bars), the alpha's genome as eight small needles, generation counter.

**Presets:** Init, Evolve Brighter, Evolve Darker, Match Sidechain, Mutant Strain (mutation 1), Frozen Specimen
(evolve off), Wide Breed.

**Tests**
- T1 Brighter on pink noise: the alpha's centroid after 16 generations at least 30 % above generation 0's.
- T2 Frozen: outputs of generations 4 and 8 identical.
- T3 determinism: two renders identical; a different `seed` differs.
- T4 CPU with population 8 within C8.

---

### 14. GAUSS // ГАУСС

Code `L6Ga` · Wave 2 · Fx, Dynamics · latency fixed 64 samples · stereo in/out.

**Imported from:** electromagnetic acceleration: a capacitor bank charges while nothing happens and dumps on the
next trigger. Lore: the Gauss rifle, the Monolith sniper on the antenna.

**Mechanism.** A charge `C` in [0, 1] rises while the input envelope is under `quiet_thr` and dumps on the next
onset into a transient boost whose size depends on the preceding silence: the opposite of a compressor. A coil
whine rises during the charge; a rail flash fires at discharge.

**Algorithm**
- `C += dt / recharge` while `env < quiet_thr`, clamped to 1; onset (dual envelope, 64-sample look-ahead so the
  boost starts on the true attack): gain envelope `g(t) = 1 + (10^(power/20) - 1) × C × exp(-t / pulse)` for
  `t` from the onset; then `C = residual`.
- whine: sine chirp from 2 kHz rising to 6 kHz as `C` goes 0 → 1, level `whine × C²`, cut at discharge.
- flash: 8 ms white burst through a 4 kHz high-pass at level `flash × C` at discharge.
Stages: `rail`, `output`.

**Core modules:** `Follower`, `Onset`, `LatencyAligner`, `SVF`.

**Parameters**

| id | name | range | default | unit | notes |
|---|---|---|---|---|---|
| `power` | Power | 0..24 | 12 | dB | the macro |
| `recharge` | Recharge | 0.1..10 | 2 | s | time to full charge, log |
| `quiet_thr` | Quiet | -80..-20 | -50 | dB | |
| `pulse` | Pulse | 5..60 | 20 | ms | boost decay |
| `residual` | Residual | 0..0.5 | 0.1 | % | charge left after a shot |
| `whine` | Coil Whine | 0..1 | 0.2 | % | |
| `flash` | Rail Flash | 0..1 | 0.2 | % | |
| `sensitivity` | Sensitivity | 0..1 | 0.5 | % | |

**Presets:** Init, Sniper's Charge, Full Rail (24 dB), Coil Whine, Quick Recharge (0.2 s), Flash Only, Heavy
Projectile (pulse 60 ms).

**Tests**
- T1 a hit after 2 s of silence (recharge 2 s) is boosted by `power` ±0.5 dB at its peak; a second hit 50 ms
  later by less than 1 dB.
- T2 C2: 64 samples at every rate.
- T3 whine present and rising during charge, absent after discharge.
- T4 residual: the hit after a shot and 0 s of silence is boosted by `power × residual` ±1 dB.

---

### 15. PSY-DOG // ПСИ-СОБАКА

Code `L6Pd` · Wave 2 · Fx, Spatial · zero latency · stereo in/out.

**Imported from:** the psy-dog's illusory pack: copies that vanish when struck. Mechanism: decorrelated phantom
doubles that live only in sustained material and disappear on attacks.

**Signal flow**
```
in -> N phantoms: Schroeder allpass chain (4 stages, 5-23 ms, hashed) + micro delay 7-30 ms + detune walk ±DETUNE
      panned around SPREAD, low-passed by DARKEN
   -> phantom gain = PHANTOM x (1 - attack) with BLOOM release, attack = fast env / slow env clipped, 60 ms hold
   -> + dry -> mix
```
Stages: `pack`, `output`.

**Core modules:** `DelayLine`, allpass, `MicroShift` (light), `Follower`, `SVF`, `panGains`, `Hash`.

**Parameters**

| id | name | range | default | unit | notes |
|---|---|---|---|---|---|
| `pack` | Pack | 1..6 | 3 | | integer |
| `phantom` | Phantom | 0..1 | 0.6 | % | the macro |
| `bloom` | Bloom | 50..2000 | 400 | ms | return time, log |
| `vanish` | Vanish | 5..200 | 30 | ms | how fast they disappear |
| `spread` | Spread | 0..1 | 0.8 | % | |
| `detune` | Detune | 0..20 | 5 | ct | random walk range |
| `darken` | Darken | 0..1 | 0.3 | % | |

**Presets:** Init, Pack of Three, Six Phantoms, Vanish Fast, Slow Bloom, Dark Pack, Wide Illusion.

**Tests**
- T1 phantom level within 50 ms after a transient at least 20 dB below its level in a sustained tone.
- T2 wet inter-channel correlation below 0.3 at spread 1, pack 6.
- T3 bloom: wet returns to within 1 dB of its sustained level after `bloom` ms (±20 %).

---

## FACTION B — THE ETHEREAL SIGNAL

### 16. RAINBOW // РАДУГА

Code `L6Rb` · Wave 1 · Fx, Delay · zero latency · stereo in/out · tail reported.

**Imported from:** Rainbow, the Brain Scorcher's codename: five antennas, one leaning and dead. Mechanism: five
tempo-synced taps with per-tap pan, level and tone, Ensemble motion on the taps, a darkening soft-limited feedback
path, and a DAMAGED fifth tap.

**Signal flow**
```
in -> 2 delay lines (L, R, 4 s) -> taps 1..5 at tap times (pattern or own sync/ms), each: level, pan, one-pole tone
   -> tap 5 DAMAGED: detune walk ±12 ct (MicroShift), band-pass 300-3000 Hz, light crush, -6 dB, erratic level
   -> feedback from FB SOURCE (tap 5 | sum) through TONE low-pass, 120 Hz high-pass, soft limit, into the lines
   -> MOTION: Ensemble-style slow sweeps on every tap time (depth, rate), right side half a cycle later
   -> PING-PONG: feedback crosses sides -> equal-power mix
```
Stages: `taps`, `output`.

**Core modules:** `EchoDelay` generalised, `DelayLine`, `MicroShift`, `Crush`, `SVF`, `Transport`.

**Tap patterns** (fractions of a quarter note, applied when a tap's sync is Pattern): Even 1, 2, 3, 4, 5 ·
Antenna 1/2, 3/4, 5/4, 2, 4 · Golden 1, 1.618, 2.618, 4.236, 6.854 · Dotted 3/4, 3/2, 9/4, 3, 15/4 ·
Triplet 2/3, 4/3, 2, 8/3, 10/3.

**Parameters**

| id | name | range | default | unit | notes |
|---|---|---|---|---|---|
| `pattern` | Pattern | Even\|Antenna\|Golden\|Dotted\|Triplet | Antenna | | |
| `tap1_sync` .. `tap5_sync` | Tap N Time | Pattern + TIME | Pattern | | |
| `tap1_ms` .. `tap5_ms` | Tap N ms | 10..4000 | 250 × N | ms | log; used when sync is Free |
| `tap1_level` .. `tap5_level` | Tap N Level | 0..1 | 0.8, 0.7, 0.6, 0.5, 0.4 | % | |
| `tap1_pan` .. `tap5_pan` | Tap N Pan | -1..1 | -0.6, 0.6, -0.3, 0.3, 0 | % | |
| `tone` | Tone | 500..16000 | 6000 | Hz | log; feedback and taps |
| `feedback` | Feedback | 0..0.95 | 0.35 | % | |
| `fb_source` | Feedback From | Tap 5\|Sum | Tap 5 | | |
| `motion` | Motion | 0..1 | 0.2 | % | |
| `motion_rate` | Motion Rate | 0.05..2 | 0.3 | Hz | log |
| `damaged` | Damaged Fifth | Off\|On | On | | |
| `pingpong` | Ping-Pong | Off\|On | Off | | |
| `transmit` | Transmit | 0..1 | 0 | % | macro: +feedback, +motion, tone up |

**Presets:** Init, Five Antennas, Damaged Fifth, Steel Yard (golden), Over the Horizon (long, ping-pong),
Woodpecker Echo (even 1/16, tight), Clean Carrier (damaged off, motion 0).

**Tests**
- T1 tap times at 120 BPM within 0.1 ms of the pattern.
- T2 feedback decay: with tap 5 at 1 beat and feedback 0.5, each repeat -6 dB ±0.3.
- T3 damaged tap: energy outside 300-3000 Hz at least 18 dB below inside.
- T4 tail reported ≥ measured decay to -60 dB.

---

### 17. WISH GRANTER // ИСПОЛНИТЕЛЬ ЖЕЛАНИЙ

Code `L6Wg` · Wave 1 · Fx, Reverb · zero latency · stereo in/out · tail reported.

**Imported from:** X-19 inside the plant: C-Consciousness, many minds as one, and the Wish Granter hologram
before the Monolith. Mechanism: the shimmer reverb with shimmer intervals taken from chord shapes, a FREEZE that
holds the moment, and a GRANT macro.

**Signal flow**
```
in -> pre-delay -> 80 Hz HP -> FDN 8 lines (Hadamard) with up to 3 shift heads feeding back: ratios from SHIMMER INTERVAL
   (Octave 2; Fifth 1.5; Octave + Fifth 2 and 1.5; Major 1.26, 1.5, 2; Minor 1.19, 1.5, 2; Sus2 1.12, 1.5, 2; Lydian 1.26, 1.5, 1.68)
   each head band-passed 300 Hz - 6 kHz, soft-limited, summed x SHIMMER x 0.55 into the network input
   -> FREEZE On: line gains 1.0, input gain 0 (the tail holds), shimmer continues
   -> equal-power mix
```
Stages: `fdn`, `output`.

**Core modules:** `Fdn` with multi-head shimmer, `MicroShift` heads, `DelayLine`, `SVF`.

**Parameters**

| id | name | range | default | unit | notes |
|---|---|---|---|---|---|
| `size` | Size | 0..1 | 0.7 | % | |
| `decay` | Decay | 0..1 | 0.6 | % | RT60 0.3-19 s as Noosphere |
| `damping` | Damping | 0..1 | 0.35 | % | |
| `predelay` | Pre-delay | 0..250 | 20 | ms | |
| `shimmer` | Shimmer | 0..1 | 0.3 | % | |
| `shimmer_interval` | Interval | Octave\|Fifth\|Octave+Fifth\|Major\|Minor\|Sus2\|Lydian | Octave | | |
| `freeze` | Freeze | Off\|On | Off | | |
| `brightness` | Brightness | -1..1 | 0 | % | tilt on the wet |
| `grant` | Grant | 0..1 | 0 | % | macro: +0.4 shimmer, +0.15 mix, -0.2 damping, +0.1 size, +brightness |

**Presets:** Init, Sarcophagus Hall, Monolith Glow (fifth), Many Minds (major), Granted (grant 1), Frozen Wish
(freeze), Octave Choir.

**Tests**
- T1 every interval at shimmer 1, decay 1: no 1 s window louder than the one before; -95 dB within 30 s
  (X-19 test 9 pattern).
- T2 freeze: level within ±1 dB over 30 s with the input removed.
- T3 shimmer spectrum: a 440 Hz burst shows energy at 880 Hz (Octave) and at 660 Hz (Fifth) at least 12 dB over
  the no-shimmer tail.
- T4 pre-delay exact (impulse).

---

### 18. CARRIER // НЕСУЩАЯ

Code `L6Cr` · Wave 2 · Fx, Filter · zero latency · stereo in/out · MIDI optional.

**Imported from:** the Radar array modelled on Duga-1, the Russian Woodpecker, whose signal tapped at 10 Hz, and
the Scorcher's psy-antenna voices. Mechanism: a formant bank and a comb bank keyed to a chord or MIDI, a true
frequency shifter by single-sideband modulation, the WOODPECKER pulse, and Ensemble width.

**Signal flow**
```
in -> FORMANT bank (4 TPT band-passes, vowel U-O-A-E-I, size) x FORMANT MIX
   -> COMB bank: up to 6 Resonator combs at the chord's notes (ROOT + CHORD shape, or held MIDI notes), feedback, 5 kHz damp
   -> SHIFT: SSB modulation (Hilbert pair) by SHIFT Hz, sideband Both|Upper|Lower (Both = ring modulation)
   -> WOODPECKER: 10 Hz (or synced) pulse gate, duty 50 %, 2 ms edges, depth
   -> ENSEMBLE width -> mix
```
Stages: `banks`, `shift`, `output`.

**Core modules:** `FormantBank`, `Resonator` ×6, `Hilbert` + `FrequencyShifter`, `Ensemble`, `Transport`,
X-19 `chordShapes`.

**Parameters**

| id | name | range | default | unit | notes |
|---|---|---|---|---|---|
| `vowel` | Vowel | 0..1 | 0.5 | % | |
| `formant_size` | Formant Size | -1..1 | 0 | % | |
| `formant_mix` | Formant Mix | 0..1 | 0.5 | % | |
| `pitch_source` | Pitch Source | Chord\|MIDI | Chord | | MIDI falls back to Chord without notes |
| `root` | Root | 24..84 | 48 | | integer MIDI note |
| `chord` | Chord | X-19 chordShapes list | Fifths | | |
| `comb_feedback` | Comb Feedback | 0..1 | 0.6 | % | |
| `comb_level` | Comb Level | 0..1 | 0.5 | % | |
| `shift_hz` | Shift | -2000..2000 | 0 | Hz | bipolar, linear |
| `sideband` | Sideband | Both\|Upper\|Lower | Upper | | |
| `am_depth` | Carrier Depth | 0..1 | 0 | % | mix of shifted vs unshifted |
| `woodpecker` | Woodpecker | 0..1 | 0 | % | |
| `wp_sync` | Woodpecker Rate | 10 Hz\|1/16\|1/32\|1/8 | 10 Hz | | |
| `width` | Width | 0..1 | 0.5 | % | Ensemble depth |
| `transmit` | Transmit | 0..1 | 0 | % | macro: +woodpecker, +am_depth, +width |

**Presets:** Init, Radar Carrier, Duga Pulse (woodpecker 1), Vowel Chord (minor), Shifted Up (+120 Hz),
Lower Sideband, Chant Bank (combs high, MIDI).

**Tests**
- T1 formant peaks at the table frequencies within 3 % for U and I.
- T2 combs at the chord notes within 0.1 semitone; MIDI replaces them (if F5 passed).
- T3 frequency shifter: 1 kHz sine, +100 Hz upper: peak at 1100 Hz, the 900 Hz image at least 40 dB down.
- T4 woodpecker rate 10.0 Hz ±1 %; synced 1/16 at 120 BPM = 8 Hz.

---

### 19. C-CONSCIOUSNESS // О-СОЗНАНИЕ

Code `L6Cc` · Wave 4 · Fx, Reverb · zero latency · stereo in/out · learned state in the blob.

**Imported from:** Hebbian learning (neurons that fire together wire together) and a pitch-class memory. Lore:
C-Consciousness, many minds fused into one, and X-3's goal of moving a mind into the Noosphere.

**Mechanism.** The FDN's mixing matrix learns from the correlation of its own lines, and a pitch-class memory
of what passes through tunes comb filters inside the loop. The space slowly takes the shape of the music. ABSORB
freezes learning and lets the tail ring at what was learned, even on noise. The learned state saves with the project.

**Algorithm**
- Matrix learning (every control block): `C_ij = (1/n) Σ_t o_i[t] o_j[t]` over the lines' damped outputs;
  Oja update `M_ij += η (C_ij - M_ij C_jj)`, `η = memory × 1e-4`; rows renormalised to unit norm; then `M` scaled
  so its spectral norm is ≤ 1 (8 power iterations on `MᵀM`, 8×8); `forget` pulls `M` toward Hadamard:
  `M += (H - M) × forget × 1e-3` per block. The FDN uses `M` in place of `hadamard(f)`.
- Pitch-class memory: 36 SVF band-passes (Q 30) at semitone centres over C2-B4 feed a 12-bin histogram
  `P[pc] += energy × memory`, decayed by `forget`; the top `voices` (1-4) pitch classes, when above 25 % of the
  maximum, tune Resonator combs inside the feedback path at the octave nearest `comb_octave`, glided over 200 ms,
  with loop gain `harmony × 0.4`.
- ABSORB On: `η = 0`, histogram frozen, input gain 0 so the tail rings. TRANSFER: when the input is silent for
  2 s, low-level noise (-48 dBFS) excites the loop so the learned harmony hums; level `transfer`.
- Blob: `M` (64 floats), `P` (12 floats), version 1.
Stages: `fdn`, `output`.

**Core modules:** `Fdn` with pluggable matrix, `Resonator`, `SVF` bank, `DelayLine`, `StateBlob`; UI `MatrixView`
(8×8 glow grid) and a 12-segment pitch-class ring.

**Parameters**

| id | name | range | default | unit | notes |
|---|---|---|---|---|---|
| `memory` | Memory | 0..1 | 0.5 | % | learning rate, the macro |
| `forget` | Forget | 0..1 | 0.2 | % | |
| `harmony` | Harmony | 0..1 | 0.6 | % | comb loop gain |
| `voices` | Voices | 1..4 | 3 | | integer pitch classes |
| `comb_octave` | Comb Octave | 2..5 | 3 | | integer |
| `absorb` | Absorb | Off\|On | Off | | |
| `transfer` | Transfer | 0..1 | 0 | % | |
| `size` | Size | 0..1 | 0.7 | % | |
| `decay` | Decay | 0..1 | 0.6 | % | |
| `damping` | Damping | 0..1 | 0.4 | % | |
| `predelay` | Pre-delay | 0..250 | 10 | ms | |
| `erase` | Erase | Off\|On | Off | | edge: resets M to H and P to 0 |

**Presets:** Init (blank mind), Absorbing, Collective (voices 4), Transfer Complete (transfer 0.5), Forgetful
(forget 1), Key of the Song (harmony 1).

**Tests**
- T1 stability: 10 min of random input at memory 1, forget 0, decay 1: no fault, spectral norm ≤ 1 each block.
- T2 learning: a C major chord for 10 s, then white noise: the tail's energy at C, E and G pitch classes at
  least 6 dB above a control run with memory 0.
- T3 erase returns the response to within 0.1 dB of the Init response.
- T4 blob round trip: M and P restored exactly; renders after reload identical.

---

### 20. ARTIFACT // АРТЕФАКТ

Code `L6Ar` · Wave 4 · Fx, Filter · zero latency · stereo in/out · Zone Bus listener (reseed) · evolved state
in the blob.

**Imported from:** crystallography (Bragg peaks at lattice spacings) and reaction-diffusion (Gray-Scott Turing
patterns). Lore: artifacts forming in anomalies after emissions, each with a property.

**Mechanism.** Twenty-four resonators sit at the frequency ratios of a crystal system. Their gains are the
inhibitor field of a one-dimensional Gray-Scott system on a ring, fed by each band's input energy, so spots and
stripes of resonance drift and crystallise over FORMATION seconds. An Emission on the bus reseeds the lattice.
PROPERTY presets add the artifact's behaviour.

**Algorithm**
- frequencies: `f_n = f0 × r_n` for the first 24 ratios of the lattice: Cubic `sqrt(h² + k² + l²)` over integer
  triples (1, √2, √3, 2, √5, √6, √8, 3, ...); Hexagonal `sqrt(4/3 (h² + hk + k²) + (l / ratio)²)`; Tetragonal
  `sqrt(h² + k² + (l / ratio)²)`; ratios sorted, duplicates removed, capped at 0.45 sr. Resonators are SVF
  band-passes with `k = 1 / q`.
- Gray-Scott on a ring of 24 cells, `u` activator, `v` inhibitor, `∇²x_i = x_{i-1} + x_{i+1} - 2x_i`:
  `u += (D_u ∇²u - u v² + F (1 - u) + feed × E_i) dt`, `v += (D_v ∇²v + u v² - (F + k) v) dt`, `D_u = 0.16`,
  `D_v = 0.08`, dt = 1 per step, steps per second `= 2000 / formation`; clamp to [0, 1]. `E_i` is resonator i's
  band energy, normalised 0-1. Regime presets `(F, k)`: Spots (0.035, 0.065), Stripes (0.058, 0.065), Mitosis
  (0.0367, 0.0649), Coral (0.0545, 0.062). Gains `g_i = depth × v_i / max(v)`, smoothed 50 ms.
- reseed: `u = 1`, `v = 0` with a hashed seed of 3 cells at `v = 0.25`; triggered by `reseed` edge or by the bus
  `reseedCounter` when `emission_reseed` is On.
- PROPERTY: Moonlight adds Whisper formant noise keyed by the resonator sum; Battery adds Dust crackle through a
  2-6 kHz band-pass at the pattern's peaks; Fireball adds tube heat (12 dB) on the resonator sum; Bubble adds a
  downward expander on the noise floor (-12 dB below -50 dBFS); Stone Flower softens with a dynamic 6 kHz shelf;
  Compass auto-pans the wet toward the pattern's centroid.
- Blob: `u`, `v` (48 floats), version 1.
Stages: `lattice`, `output`.

**Core modules:** `SVF` ×24, `Follower`, `Whisper`, `Dust`, `Shapers`, `Hash`, `ZoneBus Client` (listen only);
UI `LatticeView` (24 cells glowing with `v`).

**Parameters**

| id | name | range | default | unit | notes |
|---|---|---|---|---|---|
| `lattice` | Lattice | Cubic\|Hexagonal\|Tetragonal | Cubic | | |
| `f0` | Base | 40..1000 | 110 | Hz | log |
| `ratio` | Axis Ratio | 0.5..2 | 1.2 | x | c/a |
| `q` | Q | 20..200 | 60 | | log |
| `regime` | Regime | Spots\|Stripes\|Mitosis\|Coral | Spots | | |
| `formation` | Formation | 0.1..30 | 6 | s | log |
| `feed` | Feed | 0..1 | 0.5 | % | input drive into the field |
| `depth` | Depth | 0..1 | 0.7 | % | the macro |
| `wet_level` | Wet Level | 0..1 | 0.6 | % | |
| `property` | Property | None\|Moonlight\|Battery\|Fireball\|Bubble\|Stone Flower\|Compass | None | | |
| `emission_reseed` | Emission Reseed | Off\|On | On | | bus |
| `reseed` | Reseed | Off\|On | Off | | edge |

**Presets:** Init, Raw Lattice, Moonlight, Battery, Fireball, Bubble, Stone Flower, Compass.

**Tests**
- T1 resonator centres at `f0 × r_n` within 1 % (sweep).
- T2 `u`, `v` stay within [0, 1] over 10 min of fuzz; gains bounded.
- T3 formation 6 s: the gain vector's variance over 30 s exceeds a threshold; formation 30 with feed 0: nearly
  static after reseed.
- T4 reseed on a bus emission (`--instances 2`, one EMISSION siren): the lattice state jumps at the fire bar.

---

### 21. NUMBERS STATION // РАДИОСТАНЦИЯ

Code `L6Ns` · Wave 5 · Fx, Filter · zero latency · stereo in/out.

**Imported from:** cryptography, one-time pads and frequency-hopping spread spectrum; shortwave numbers stations
and UVB-76's buzzer. Lore: the Zone's radio chatter, the Monolith's jamming device in the Pripyat kindergarten,
the Duga array.

**Mechanism.** A KEY seeds a hop sequence locked to the bar. It drives a hopping band-pass or notch, an AM or
single-sideband carrier, number groups as tone pips, and the Buzzer. Two instances with the same key and
`complement` set opposite pass exactly complementary spectra: one keeps what the other removes, so two tracks
interleave in frequency and time.

**Algorithm**
- `seed = hash(key_text) ^ key_seed`; hop value `h = hash01(bar, step, "hop", seed)` at `hop_rate`.
- hopping filter: centre `f = f_lo × (f_hi / f_lo)^h`, SVF with `k = 1 / q`; Pass mode outputs `bp × k`
  (unity peak), Notch outputs `x - bp × k`; these sum to `x` exactly, which is the complement guarantee.
  `complement` On swaps Pass and Notch. Hops glide over `smooth` ms.
- carrier: SSB (shared `FrequencyShifter`) at `carrier_hz` with `sideband`, mixed by `carrier_depth`.
- numbers: five-digit groups hashed from the key and bar, digit d as a pip at `400 + 60 d` Hz, 80 ms on, 40 ms
  off, 400 ms between groups, level `numbers_level`, one group per bar when `numbers` is On.
- buzzer: 25 Hz pulse train with harmonics through a 300-3000 Hz band-pass, 0.8 s on and 0.45 s off (1.25 s
  period), pitch offset from the key, level `buzzer_level`.
Stages: `hop`, `carrier`, `output`.

**Core modules:** `SVF`, `FrequencyShifter`, `Hash`, `Transport`.

**Parameters**

| id | name | range | default | unit | notes |
|---|---|---|---|---|---|
| `key_seed` | Key | 0..9999 | 1 | | integer; the UI text KEY hashes into the state and XORs with this |
| `hop_rate` | Hop Rate | RATE without Free | 1/8 | | |
| `filter_mode` | Filter | Pass\|Notch\|Off | Pass | | |
| `complement` | Complement | Off\|On | Off | | swaps Pass/Notch for the partner track |
| `f_lo` | Hop Low | 50..2000 | 150 | Hz | log |
| `f_hi` | Hop High | 500..16000 | 6000 | Hz | log |
| `q` | Q | 0.5..20 | 4 | | log |
| `smooth` | Glide | 1..200 | 20 | ms | log |
| `carrier_hz` | Carrier | 50..5000 | 440 | Hz | log |
| `sideband` | Sideband | Both\|Upper\|Lower | Upper | | |
| `carrier_depth` | Carrier Depth | 0..1 | 0 | % | |
| `numbers` | Numbers | Off\|On | Off | | |
| `numbers_level` | Numbers Level | 0..1 | 0.3 | % | |
| `buzzer_level` | Buzzer | 0..1 | 0 | % | |
| `transmission` | Transmission | 0..1 | 0 | % | macro: +carrier_depth, +q, +numbers_level |

**Presets:** Init, UVB-76 (buzzer), Five Digit Groups, Hop Sixteenth, Complement A, Complement B, Jammer
(notch, q 1, carrier).

**Tests**
- T1 two instances, same key, complement Off and On: outputs sum to the input within 1e-5 at every hop.
- T2 hop sequence identical across two renders and across two instances with the same key; different with a
  different key.
- T3 buzzer period 1.25 s ±1 %; numbers cadence 80/40 ms.
- T4 SSB image rejection ≥ 40 dB (shared with CARRIER T3).

---

### 22. OASIS // ОАЗИС

Code `L6Oa` · Wave 2 · Fx, Restoration · latency fixed 64 samples · stereo in/out.

**Imported from:** the Oasis anomaly that heals. Mechanism: restoration that undoes the Deep Zone: a declipper,
a self-learning de-hisser and harmonic regeneration above a cutoff plus an octave-down sub return.

**Signal flow**
```
in -> DECLIP: runs of |x| >= CLIP THRESHOLD (auto: flat tops detected by a 64-sample look-ahead) up to 64 samples long
      replaced by cubic Hermite across the run, blended by DECLIP
   -> DEHISS: 8 SVF bands; per band a noise floor estimate (fast down, 1 dB/s up); downward expander 1:2 below
      floor + DEHISS x 12 dB, 80 ms release
   -> REGENERATE: 2-5 kHz band -> odd-order shaper (x³) -> high-pass at REGEN FROM -> 16 kHz low-pass, added at REGEN
   -> SUB RETURN: 60-160 Hz band -> zero-crossing octave divider -> 120 Hz low-pass, added at SUB REGEN
   -> mix
```
Stages: `declip`, `dehiss`, `output`.

**Core modules:** `LatencyAligner`, `SVF`, `Follower`, `Shapers`, `hermite`.

**Parameters**

| id | name | range | default | unit | notes |
|---|---|---|---|---|---|
| `heal` | Heal | 0..1 | 0 | % | macro: declip, dehiss, regen together |
| `declip` | Declip | 0..1 | 0.5 | % | |
| `clip_thr` | Clip Threshold | 0.5..1 | 0.98 | x | Auto detection above this |
| `dehiss` | Dehiss | 0..1 | 0.3 | % | |
| `regen` | Regenerate | 0..1 | 0.2 | % | |
| `regen_from` | Regen From | 4000..12000 | 8000 | Hz | log |
| `sub_regen` | Sub Return | 0..1 | 0 | % | |

**Presets:** Init, Heal, Declip Only, Dehiss Only, Regenerate Air, Sub Return, Full Restoration.

**Tests**
- T1 a sine clipped at -3 dB: THD after declip at least 15 dB lower than before.
- T2 speech-like signal with -50 dBFS hiss: hiss in the pauses down 20 dB, signal RMS within 1 dB.
- T3 a 4 kHz-capped signal gains energy above 8 kHz with regen; none at regen 0.
- T4 C2: 64 samples at every rate.

---

### 23. CLEAR SKY // ЧИСТОЕ НЕБО

Code `L6Cs` · Wave 5 · Fx, Modulation · zero latency · stereo in/out.

**Imported from:** meteorology: fronts, precipitation, wind, lightning and the day cycle, as a slow modulation
system. Lore: the Zone's weather and the Clear Sky faction's name. The day length is a parameter; the games'
time scales differ by title and are not assumed.

**Signal flow**
```
clock: TIME OF DAY from the host position over DAY LENGTH minutes (or the knob when the transport is stopped)
weather state (AUTO: hashed Markov chain every FRONT RATE bars; or the WEATHER choice): clear, overcast, rain, storm, emission sky
   PRESSURE -> FOG low-pass (20 kHz x (0.1 + 0.9 x clearness)) and a gentle tilt
   RAIN -> Poisson drops (2 ms clicks through random 2-6 kHz band-passes) + hiss bed, density x rain
   WIND -> pan motion (0.05-0.3 Hz) and Ensemble depth x wind + high-passed whoosh noise modulated by slow noise
   LIGHTNING -> Poisson flashes (storm x 0.2/s): +6 dB high shelf for 80 ms; RUMBLE after DISTANCE / 343 m/s: low noise burst 1-3 s
   NIGHT -> -3 dB above 4 kHz at midnight, 0 at noon; EMISSION SKY -> tilt up, 50 Hz hum, distant siren bed
   -> mix
```
Stages: `sky`, `output`.

**Core modules:** `SVF`, `Ensemble`, `Dust`-style noise, `Siren` (bed), `Hash`, `Transport`; UI `WeatherView`.

**Parameters**

| id | name | range | default | unit | notes |
|---|---|---|---|---|---|
| `weather` | Weather | Auto\|Clear\|Overcast\|Rain\|Storm\|Emission Sky | Auto | | |
| `front_rate` | Front Rate | BARS | 16 | bars | Auto changes |
| `day_length` | Day Length | 1..60 | 24 | min | |
| `time_of_day` | Time of Day | 0..24 | 12 | h | follows the clock while playing; the knob otherwise |
| `fog` | Fog | 0..1 | 0.5 | % | |
| `rain` | Rain | 0..1 | 0.3 | % | |
| `wind` | Wind | 0..1 | 0.3 | % | |
| `lightning` | Lightning | 0..1 | 0.5 | % | |
| `rumble` | Rumble | 0..1 | 0.5 | % | |
| `distance` | Storm Distance | 0.5..10 | 3 | km | rumble delay = distance / 0.343 s |
| `front` | Front | 0..1 | 0 | % | macro: pushes the state toward storm |

**Presets:** Init, Morning Clear, Overcast Swamp, Rain at Yantar, Storm Front, Night Watch, Emission Sky.

**Tests**
- T1 lightning rumble arrives `distance / 343` s after the flash within 5 %.
- T2 day cycle: the night tilt's period equals `day_length` while the transport plays.
- T3 rain density scales with `rain` (click counts in 10 s).
- T4 levels bounded: no weather state exceeds +6 dB over the input's peak; silence in gives only the beds.
