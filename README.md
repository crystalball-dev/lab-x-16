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

**Status:** in development. Wave 1 (five effects) is built and tested; the other eighteen follow in four more waves.
See `docs/EFFECTS.md` for every effect's contract and `docs/DESIGN.md` for what the build changed and the test results.

## Wave 1

| Effect | Faction | What it does |
|---|---|---|
| **KAYMANOV** // ИЗЛУЧАТЕЛЬ | Deep Zone | Oversampled split-band distortion (tube, clip, fold, rectify, crush) that keeps the sub clean. COOLANT is a beat-locked duty cycle whose dips drive the shaper harder: the pumps stop, the emitter heats. |
| **DOSIMETER** // ДОЗИМЕТР | Deep Zone | One lo-fi stage: bit and rate crush, a tape, radio or phone tone cap, Geiger clicks whose rate follows the input, and dust and hiss that follow its envelope. |
| **SILO** // ШАХТА | Deep Zone | A small, ringing chamber: five LEVELS that step the geometry, a modal blend that makes the room resonate, a wet that ducks under the input, and TUNNEL, narrow band-passed multi-taps. |
| **RAINBOW** // РАДУГА | Ethereal Signal | Five tempo-synced taps with their own time, level, pan and tone, slow motion on the tap times, a darkening feedback path, and a DAMAGED fifth antenna that detunes, narrows and stutters. |
| **WISH GRANTER** // ИСПОЛНИТЕЛЬ ЖЕЛАНИЙ | Ethereal Signal | Shimmer reverb whose shimmer intervals come from chord shapes (octave, fifth, major, minor, sus2, lydian), a FREEZE that holds the moment, and GRANT, a macro that gives a little of everything. |

Every effect has MIX, OUTPUT and a latency-matched BYPASS, six to eight factory presets (loudness-matched to the
input), and a fault guard that silences and logs a runaway stage instead of letting it reach the speakers.

### In FL Studio

- Put the effects in mixer slots. Use each effect's own **MIX** rather than the slot's mix knob.
- Reverbs and delays report their tail, so *Smart disable* does not cut them. A frozen WISH GRANTER reports an
  endless tail while FREEZE is on.
- KAYMANOV reports a few samples of latency (its oversampler); FL compensates it.

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
