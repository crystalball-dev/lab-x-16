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
