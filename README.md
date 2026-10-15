# LAB X-16

Twenty-three standalone mixer effects for FL Studio in two factions, built as VST® 3 plugins for Windows. The fifth
LAB X- release from OPERATION FAIRWAY, after [LAB X-3](https://github.com/crystalball-dev/lab-x-3),
[LAB X-10](https://github.com/crystalball-dev/lab-x-10), [LAB X-19](https://github.com/crystalball-dev/LAB-X-19) and
[LAB X-0](https://github.com/crystalball-dev/lab-x-0).

Below the Yantar factory lies Laboratory X-16 and its Miracle Machine; across the swamp sits Sakharov's laboratory, and
beyond the Red Forest the Radar carries the Rainbow signal. **The Deep Zone** degrades, drifts, presses and infects.
**The Ethereal Signal** diffuses, resonates, learns and transmits.

LAB X-16 is a fan project. It is not affiliated with or endorsed by GSC Game World. Every sound in the effects line is
synthesized; this repository contains no game audio and no samples.

**Status:** all twenty-three effects are built and tested (version 0.1.0 each). See `docs/EFFECTS.md` for every
effect's contract, `docs/DESIGN.md` for what the build changed and the test results, and `docs/ui` for a picture of
every editor.

## The effects

### The Deep Zone

| Effect | What it does |
|---|---|
| **KAYMANOV** // ИЗЛУЧАТЕЛЬ | Oversampled split-band distortion (tube, clip, fold, rectify, crush) that keeps the sub clean. COOLANT is a beat-locked duty cycle whose dips drive the shaper harder: the pumps stop, the emitter heats. |
| **DOSIMETER** // ДОЗИМЕТР | One lo-fi stage: bit and rate crush, a tape, radio or phone tone cap, Geiger clicks whose rate follows the input, and dust and hiss that follow its envelope. |
| **SILO** // ШАХТА | A small, ringing chamber: five LEVELS that step the geometry, a modal blend that makes the room resonate, a wet that ducks under the input, and TUNNEL, narrow band-passed multi-taps. |
| **PSY-FIELD** // ПСИ-ПОЛЕ | Exposure builds while the track (or a sidechain) stays loud and deepens everything: a wandering pitch drift, a vowel resonator tuned by a knob, MIDI or the input's own pitch, whispers and a controller whine that follow the input's envelope. |
| **ANOMALY** // АНОМАЛИЯ | LAB X-0's TIME and VOLUME lanes as a mixer effect: repeats, scratches, reverse, tape stop, half speed, rolls and stutters, gates, fades and ducks from an eight-pattern grid locked to FL's bar, or held from MIDI notes. |
| **APPARITION** // ПРИЗРАК | Uses 250 ms of compensated latency as look-ahead: pre-echoes, a reverse swell and a rising whisper that arrive before each hit, and phantom doubles that vanish the instant it lands. |
| **GAUSS** // ГАУСС | The opposite of a compressor: a capacitor charges during silence and dumps on the next hit, so the longer the gap, the harder the hit lands. Coil whine rises with the charge; a rail flash fires at discharge. |
| **PSY-DOG** // ПСИ-СОБАКА | Up to six detuned, smeared, wide phantom copies that live only in sustained material: an attack makes them vanish, and they bloom back. |
| **EMISSION** // ВЫБРОС | A blowout on FL's bar grid. One instance is the siren and schedules it on the Zone Bus; every instance plays the same warning, P-wave, S-wave pressure and aftermath from its own song position, scaled by SEVERITY and SHELTER. ANABIOTICS sleep through it. |
| **CONTAGION** // ЗАРАЖЕНИЕ | An SIR epidemic between tracks: a Patient Zero (or a loud field source) infects its peers over the Zone Bus at R0 per bar; infected tracks wobble, choke, crush and whisper through incubation and fever, recover and stay immune for a while. |
| **100 RADS** // 100 РЕНТГЕН | Six bands bid for loudness every block in a second-price auction: winners are boosted and taxed, losers are cut and saved up. GREED makes the rich richer; TAX moves the loudness around the spectrum. |
| **SNORK** // СНОРК | A genetic algorithm breeds small effect chains (filters, tilt, drive, crush, chorus, delay, pitch) toward a goal (brighter, darker, louder, quieter, wider, or like the sidechain) every few bars; what you hear glides to the fittest. |
| **ZEALOTS** // ФАНАТИКИ | A flock of up to sixteen voices reading one delay line: position is delay, speed is detune. ZEALOTRY pulls them into a chanting unison; a transient is the predator that scatters them. |
| **SARCOPHAGUS** // САРКОФАГ | Each hit deposits a parent isotope that decays through up to four generations by the Bateman equations; every decay replays a grain of the hit, pitched and darkened generation by generation. A tail dense and fast first, then sparse and slow. |
| **POLTERGEIST** // ПОЛТЕРГЕЙСТ | Every hit throws an object into a small room: it falls, bounces and replays its sound on each floor impact, panned where it lands, Doppler-shifted by its speed. RAGE adds telekinetic lifts. |

### The Ethereal Signal

| Effect | What it does |
|---|---|
| **RAINBOW** // РАДУГА | Five tempo-synced taps with their own time, level, pan and tone, slow motion on the tap times, a darkening feedback path, and a DAMAGED fifth antenna that detunes, narrows and stutters. |
| **WISH GRANTER** // ИСПОЛНИТЕЛЬ ЖЕЛАНИЙ | Shimmer reverb whose shimmer intervals come from chord shapes (octave, fifth, major, minor, sus2, lydian), a FREEZE that holds the moment, and GRANT, a macro that gives a little of everything. |
| **CARRIER** // НЕСУЩАЯ | The Duga array: a vowel formant bank, a comb bank tuned to a chord or to held MIDI notes, a true single-sideband frequency shift, the Woodpecker's 10 Hz pulse gate and ensemble width. |
| **OASIS** // ОАЗИС | Restoration: DECLIP redraws flat-topped runs, DEHISS learns each band's noise floor and expands below it, REGENERATE rebuilds the top octave, SUB RETURN adds an octave under the bass. |
| **C-CONSCIOUSNESS** // О-СОЗНАНИЕ | A reverb that learns: its mixing matrix adapts to the correlations of its own lines and a pitch-class memory tunes combs inside the loop, so the space takes the shape of the music. The mind saves with the project. |
| **ARTIFACT** // АРТЕФАКТ | Twenty-four resonators at a crystal lattice's frequency ratios, their gains a Gray-Scott reaction-diffusion pattern fed by the input: spots and stripes of resonance drift and crystallise. An EMISSION reseeds the lattice. |
| **NUMBERS STATION** // РАДИОСТАНЦИЯ | A key seeds a bar-locked frequency-hopping band-pass (or notch), a single-sideband carrier, number groups as tone pips and the buzzer. Two instances with one key and COMPLEMENT set opposite split the spectrum exactly between two tracks. |
| **CLEAR SKY** // ЧИСТОЕ НЕБО | Weather as slow modulation: fronts on a Markov chain shared by every instance in the project, fog, rain drops and hiss, swirling wind, lightning with thunder at distance / 343 m/s, the day's light, and an emission sky. |

Every effect has MIX, OUTPUT and a latency-matched BYPASS, six to eight factory presets (loudness-matched to the
input), and a fault guard that silences and logs a runaway stage instead of letting it reach the speakers.

### In FL Studio

- Put the effects in mixer slots. Use each effect's own **MIX** rather than the slot's mix knob.
- Reverbs and delays report their tail, so *Smart disable* does not cut them. A frozen WISH GRANTER reports an
  endless tail while FREEZE is on.
- KAYMANOV reports a few samples of latency (its oversampler); APPARITION 250 ms, OASIS and GAUSS 64 samples (their
  look-ahead). FL compensates them.
- **The Zone Bus.** Instances in one FL process see each other (the PEERS panel): EMISSION's siren schedules every
  instance's blowout, CONTAGION spreads between tracks, ARTIFACT reseeds on an emission, CLEAR SKY shares one sky. A
  bridged plugin runs in another process and shows ALONE; EMISSION and CONTAGION then run their own timeline.
- **MIDI** reaches a mixer effect through a MIDI Out channel whose *Port* matches the wrapper's *Settings > MIDI >
  Input port* (PSY-FIELD, ANOMALY and CARRIER take notes).
- **Sidechain**: route a track to the effect's track with "Sidechain to this track" and pick the input in the
  wrapper's *Processing > Connections*. PSY-FIELD and CONTAGION (FIELD SOURCE) and ZEALOTS (PREDATOR) listen to it
  when asked to; SNORK's Match Sidechain goal breeds toward it.

## Building

Windows, Visual Studio 2022 (MSVC), CMake 3.22+, JUCE 9.

```
powershell -ExecutionPolicy Bypass -File scripts/build.ps1 [-Only kaymanov,silo]
powershell -ExecutionPolicy Bypass -File scripts/validate.ps1 -All -Strictness 10
powershell -ExecutionPolicy Bypass -File scripts/install.ps1 -All
```

Tests (Python 3 with numpy, scipy and soundfile): the common checks C1-C9, each effect's own tests and the preset
loudness check, through each effect's render harness. Reports land in `build/test-report-<slug>.json`.

```
python -I test/run_tests.py --all
python -I test/run_tests.py --plugin silo --only C1,T2 --quick
```

## Licence

AGPL-3.0. VST is a registered trademark of Steinberg Media Technologies GmbH.
