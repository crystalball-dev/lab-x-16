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

**Status:** in development. Milestone M0 (core base, Zone Bus, both themes, host-check stubs) is built; the effects
follow in five waves. See `docs/DESIGN.md`.

## Building

Windows, Visual Studio 2022 (MSVC), CMake 3.22+, JUCE 9.

```
powershell -ExecutionPolicy Bypass -File scripts/build.ps1 [-Only kaymanov,silo]
powershell -ExecutionPolicy Bypass -File scripts/validate.ps1 -All -Strictness 10
powershell -ExecutionPolicy Bypass -File scripts/install.ps1 -All
```

## Licence

AGPL-3.0. VST is a registered trademark of Steinberg Media Technologies GmbH.
