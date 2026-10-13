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
