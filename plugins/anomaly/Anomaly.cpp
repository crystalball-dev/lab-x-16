// ANOMALY // АНОМАЛИЯ — the anomalies that bend space and time, and the emission as the Zone's clock. LAB X-0's TIME and
// VOLUME lanes as a standalone slicer: a 16 s history read by two crossfading heads (Springboard repeats, Whirligig
// scratches, Vortex reverse, Comet tape stop, Jelly half speed, Burner rolls, Bubble stutter) and Electro gates, fades,
// ducks and mutes, from an eight-pattern step grid locked to FL's bar, or held from MIDI notes.

#include "core/host/EffectProcessor.h"
#include "core/dsp/Anomaly.h"
#include "ui/EffectEditor.h"
#include "ui/Views.h"

namespace
{
    using namespace lab;
    using host::Kind;
    using host::Smooth;
    namespace Slot = dsp::AnomalySlot;

    enum Param : int { pattern = 0, resolution, length, scratchDepth, run, timeMix, volumeMix, field };

    constexpr int numPatterns = 8, numLanes = 2, maxSteps = 64;
    constexpr double resolutionQuarters[3] = { 0.5, 0.25, 0.125 };
    constexpr int lengths[3] = { 16, 32, 64 };

    // The factory bank: one bar at 1/16 per lane, repeated to 64 steps. Presets choose among them.
    constexpr const char* factoryBank[numPatterns][2] = {
        { "............ssss", "................" },   // 1 Field Test (Init): the last beat stutters
        { "........ee..ssss", "................" },   // 2 Springboard Field
        { "....WW......wwww", "................" },   // 3 Whirligig Alley
        { "........vvvvvvvv", "................" },   // 4 Vortex Pull
        { "............cccc", "iiii............" },   // 5 Comet Trail
        { "jjjjjjjj........", "........rrrrrrrr" },   // 6 Jelly Pond
        { "........bbbbbbbb", "ssss............" },   // 7 Burner Row
        { "oooooooo....oooo", "....ssss....tttt" },   // 8 Bubble Cluster
    };

    const host::EffectInfo& effectInfo()
    {
        static const host::EffectInfo i = []
        {
            host::EffectInfo e;
            e.product = LABX16_PRODUCT;
            e.subtitle = "АНОМАЛИЯ";
            e.slug = LABX16_SLUG;
            e.version = LABX16_VERSION;
            e.code = LABX16_CODE;
            e.faction = host::Faction::deepZone;
            e.midi = true;
            e.params = {
                { "pattern",       "Pattern",     Kind::integer, 1.0f, 8.0f, 1.0f, 0.0f, Smooth::none,   "",  "" },
                { "resolution",    "Resolution",  Kind::choice,  0.0f, 2.0f, 1.0f, 0.0f, Smooth::none,   "",  "1/8|1/16|1/32" },
                { "length",        "Length",      Kind::choice,  0.0f, 2.0f, 0.0f, 0.0f, Smooth::none,   "",  "16|32|64" },
                { "scratch_depth", "Scratch",     Kind::real,    0.0f, 1.0f, 0.6f, 0.0f, Smooth::linear, "%", "" },
                { "run",           "Run",         Kind::choice,  0.0f, 1.0f, 0.0f, 0.0f, Smooth::none,   "",  "Host|Free" },
                { "time_mix",      "Time Mix",    Kind::real,    0.0f, 1.0f, 1.0f, 0.0f, Smooth::linear, "%", "" },
                { "volume_mix",    "Volume Mix",  Kind::real,    0.0f, 1.0f, 1.0f, 0.0f, Smooth::linear, "%", "" },
                { "field",         "Field",       Kind::real,    0.0f, 1.0f, 0.0f, 0.0f, Smooth::linear, "%", "" },
            };
            e.stages = { "anomaly" };
            e.presets = {
                { "Init", {} },
                { "Springboard Field", { { "pattern", 2.0f }, { "output", -0.9f } } },
                { "Whirligig Alley", { { "pattern", 3.0f }, { "scratch_depth", 0.8f }, { "output", -0.2f } } },
                { "Vortex Pull", { { "pattern", 4.0f }, { "output", 0.0f } } },
                { "Comet Trail", { { "pattern", 5.0f }, { "output", 2.4f } } },
                { "Jelly Pond", { { "pattern", 6.0f }, { "output", -0.3f } } },
                { "Burner Row", { { "pattern", 7.0f }, { "output", 3.7f } } },
                { "Bubble Cluster", { { "pattern", 8.0f }, { "field", 0.2f }, { "output", -0.5f } } },
            };
            return e;
        }();
        return i;
    }

    int slotFromChar (int lane, char c) noexcept
    {
        const int count = lane == 0 ? (int) Slot::numTime : (int) Slot::numVolume;
        for (int s = 0; s < count; ++s)
            if ((lane == 0 ? Slot::timeInfo[s].text : Slot::volumeInfo[s].text) == c)
                return s;
        return 0;
    }

    // The pattern grid, the region source the engine reads. Cells are atomics: the editor writes, the audio thread reads,
    // neither waits. A pattern change takes effect at the next boundary of the pattern playing.
    class PatternStore final : public dsp::AnomalyRegionSource
    {
    public:
        PatternStore() { restoreAll(); }

        int cell (int pat, int lane, int step) const noexcept { return cells[index (pat, lane, step)].load (std::memory_order_relaxed); }
        void setCell (int pat, int lane, int step, int slot) noexcept
        {
            const int count = lane == 0 ? (int) Slot::numTime : (int) Slot::numVolume;
            cells[index (pat, lane, step)].store ((uint8_t) std::clamp (slot, 0, count - 1), std::memory_order_relaxed);
        }

        void restore (int pat) noexcept
        {
            for (int lane = 0; lane < numLanes; ++lane)
                for (int s = 0; s < maxSteps; ++s)
                    setCell (pat, lane, s, slotFromChar (lane, factoryBank[pat][lane][s % 16]));
        }
        void restoreAll() noexcept { for (int p = 0; p < numPatterns; ++p) restore (p); }

        // "N:TIME|VOLUME; ..." with the slots' text characters; a lane's text repeats to 64 steps.
        bool loadText (const juce::String& text)
        {
            bool any = false;
            for (const auto& entry : juce::StringArray::fromTokens (text, ";", ""))
            {
                const auto e = entry.trim();
                if (e.isEmpty())
                    continue;
                const int pat = e.upToFirstOccurrenceOf (":", false, false).getIntValue() - 1;
                if (! juce::isPositiveAndBelow (pat, numPatterns) || ! e.contains (":"))
                    return false;
                const auto lanesText = e.fromFirstOccurrenceOf (":", false, false);
                const juce::String laneText[2] = { lanesText.upToFirstOccurrenceOf ("|", false, false).trim(),
                                                   lanesText.fromFirstOccurrenceOf ("|", false, false).trim() };
                for (int lane = 0; lane < numLanes; ++lane)
                {
                    const auto& t = laneText[lane];
                    for (int s = 0; s < maxSteps; ++s)
                        setCell (pat, lane, s, t.isEmpty() ? 0 : slotFromChar (lane, (char) t[s % t.length()]));
                }
                any = true;
            }
            return any;
        }

        // Audio thread, per control block.
        void update (int wantedPattern, int res, int len, float fieldAmount, uint32_t salt, double q, bool running, bool fresh) noexcept
        {
            stepQ = resolutionQuarters[std::clamp (res, 0, 2)];
            length = lengths[std::clamp (len, 0, 2)];
            field = fieldAmount;
            callsign = salt;
            const double patternQ = stepQ * (double) length;
            if (fresh || ! running)
            {
                current = pending = wantedPattern;
                switchQ = -1.0e18;
            }
            else if (wantedPattern != pending)
            {
                // The next boundary of the pattern playing (half a sample of slack: a boundary on the block edge counts).
                pending = wantedPattern;
                switchQ = std::ceil (q / patternQ - 1.0e-7) * patternQ;
            }
            if (pending != current && q >= switchQ - 1.0e-7)
                current = pending;
        }

        // MIDI holds: a TIME or VOLUME slot for as long as its key is down, anchored where the key went down.
        void hold (int lane, int slot, double q) noexcept
        {
            heldSlot[lane] = slot;
            heldQ[lane] = q;
            heldId[lane] = -2 - (++holdCount);
        }
        void release (int lane, int slot) noexcept
        {
            if (heldSlot[lane] == slot)
                heldSlot[lane] = 0;
        }
        void releaseAll() noexcept { heldSlot[0] = heldSlot[1] = 0; }
        bool anyHeld() const noexcept { return heldSlot[0] > 0 || heldSlot[1] > 0; }

        double stepQuarters() const noexcept override { return stepQ; }
        double nextBoundary (double q) const noexcept override { return (std::floor (q / stepQ + 1.0e-9) + 1.0) * stepQ; }

        dsp::AnomalyRegion regionAt (int lane, double q) const noexcept override
        {
            dsp::AnomalyRegion r;
            if (heldSlot[lane] > 0)
            {
                r.slot = heldSlot[lane];
                r.startQ = heldQ[lane];
                r.endQ = heldQ[lane] + 1.0;   // a held slot's region is a beat long (Comet stops over it)
                r.id = heldId[lane];
                return r;
            }
            const int pat = q >= switchQ - 1.0e-7 ? pending : current;
            const auto s = (int64_t) std::floor (q / stepQ + 1.0e-9);
            const auto cycleStart = s - (int64_t) floorMod (s, length);
            const int slot = slotAt (pat, lane, s, cycleStart);
            if (slot == 0)
                return r;
            int64_t first = s, last = s;
            while (first - 1 >= cycleStart && slotAt (pat, lane, first - 1, cycleStart) == slot)
                --first;
            while (last + 1 < cycleStart + length && slotAt (pat, lane, last + 1, cycleStart) == slot)
                ++last;
            r.slot = slot;
            r.startQ = (double) first * stepQ;
            r.endQ = (double) (last + 1) * stepQ;
            r.id = first * 2 + lane;
            return r;
        }

        int playing() const noexcept { return current; }
        int laneStep (double q) const noexcept { return floorMod ((int64_t) std::floor (q / stepQ + 1.0e-9), length); }
        int getLength() const noexcept { return length; }

    private:
        static size_t index (int pat, int lane, int step) noexcept
        {
            return (size_t) ((std::clamp (pat, 0, numPatterns - 1) * numLanes + std::clamp (lane, 0, 1)) * maxSteps + std::clamp (step, 0, maxSteps - 1));
        }
        static int floorMod (int64_t a, int m) noexcept { const auto r = a % m; return (int) (r < 0 ? r + m : r); }

        // FIELD fills empty TIME steps with random anomalies, a hash01 decision per step (a render repeats exactly).
        int slotAt (int pat, int lane, int64_t absStep, int64_t cycleStart) const noexcept
        {
            const int slot = cell (pat, lane, (int) (absStep - cycleStart));
            if (slot != 0 || lane != 0 || field <= 0.001f)
                return slot;
            if (dsp::hash01 (absStep, 0, 0xF1E1Du, callsign) < field * 0.35f)
                return 1 + (int) (dsp::hash01 (absStep, 1, 0xF1E1Du, callsign) * (float) (Slot::numTime - 1));
            return 0;
        }

        std::array<std::atomic<uint8_t>, numPatterns * numLanes * maxSteps> cells {};
        double stepQ = 0.25, switchQ = -1.0e18;
        int length = 16, current = 0, pending = 0;
        float field = 0.0f;
        uint32_t callsign = 0;
        int heldSlot[2] { 0, 0 };
        double heldQ[2] { 0.0, 0.0 };
        int64_t heldId[2] { -1, -1 }, holdCount = 0;
    };

    class AnomalyChain final : public host::Chain
    {
    public:
        PatternStore store;
        std::atomic<int> uiStep { 0 }, uiPattern { 0 }, uiTime { 0 }, uiVolume { 0 };

        void prepare (double sampleRate, int) override
        {
            sr = sampleRate;
            engine.prepare (sampleRate, 16.0);
            fresh = true;
        }

        void reset() override
        {
            engine.reset();
            freeQ = 0.0;
        }

        double tailSeconds (const float*) const override { return 0.1; }

        void control (const float* v, const host::BlockContext& ctx, int) override
        {
            const auto& t = *ctx.transport;
            const bool freeRun = v[run] > 0.5f;
            // A MIDI hold plays while the transport is stopped too, on the internal clock at the last tempo.
            const bool internal = freeRun || ! t.playing;
            running = freeRun || t.playing || store.anyHeld();
            qps = t.ppqPerSample;
            q0 = internal ? freeQ : t.ppq;
            store.update ((int) std::lround (v[pattern]) - 1, (int) std::lround (v[resolution]), (int) std::lround (v[length]),
                          v[field], ctx.callsign, q0, running, fresh);
            fresh = false;
            context.source = &store;
            context.running = running;
            context.q0 = q0;
            context.qps = qps;
            context.scratchDepth = std::clamp (v[scratchDepth] + 0.4f * v[field], 0.0f, 1.0f);
            context.timeMix = std::clamp (v[timeMix] + 0.3f * v[field], 0.0f, 1.0f);
            context.volumeMix = v[volumeMix];
            context.salt = ctx.callsign;
            midiQ = q0;
        }

        // Notes 60-71 hold TIME slots 1-12, 72-79 VOLUME slots 1-8 while the key is down.
        void midi (const juce::MidiMessage& m, const host::BlockContext& ctx) override
        {
            juce::ignoreUnused (ctx);
            const int note = m.getNoteNumber();
            const double q = midiQ;   // the latest control block's position; the engine anchors where it first sees the hold
            holdsChanged = true;
            if (m.isNoteOn())
            {
                if (note >= 60 && note < 60 + Slot::numTime - 1)
                    store.hold (0, note - 59, q);
                else if (note >= 72 && note < 72 + Slot::numVolume - 1)
                    store.hold (1, note - 71, q);
            }
            else if (m.isNoteOff())
            {
                if (note >= 60 && note < 72)
                    store.release (0, note - 59);
                else if (note >= 72 && note < 84)
                    store.release (1, note - 71);
            }
            else if (m.isAllNotesOff() || m.isAllSoundOff())
                store.releaseAll();
        }

        void process (float* l, float* r, const float*, const float*, int n, host::Guard& guard) override
        {
            if (holdsChanged)
            {
                engine.invalidate();
                holdsChanged = false;
            }
            engine.process (l, r, n, context);
            if (running)
                freeQ += (double) n * qps;
            guard (0, l, r, n);
            uiStep.store (store.laneStep (q0));
            uiPattern.store (store.playing());
            uiTime.store (engine.activeTimeSlot());
            uiVolume.store (engine.activeVolumeSlot());
        }

        void meter (host::Meters& m) const override
        {
            m.custom[0].store ((float) engine.activeTimeSlot());
            m.custom[1].store ((float) engine.activeVolumeSlot());
            m.custom[2].store ((float) uiStep.load());
            m.custom[3].store ((float) (store.playing() + 1));
            m.custom[4].store (running ? 1.0f : 0.0f);
        }

        int blobVersion() const override { return 1; }
        void saveBlob (juce::MemoryOutputStream& out) const override
        {
            for (int p = 0; p < numPatterns; ++p)
                for (int lane = 0; lane < numLanes; ++lane)
                    for (int s = 0; s < maxSteps; ++s)
                        out.writeByte ((char) store.cell (p, lane, s));
        }
        bool loadBlob (juce::MemoryInputStream& in, int version) override
        {
            if (version != 1 || in.getNumBytesRemaining() < numPatterns * numLanes * maxSteps)
                return false;
            for (int p = 0; p < numPatterns; ++p)
                for (int lane = 0; lane < numLanes; ++lane)
                    for (int s = 0; s < maxSteps; ++s)
                        store.setCell (p, lane, s, (uint8_t) in.readByte());
            return true;
        }
        bool loadText (const juce::String& text) override { return store.loadText (text); }

    private:
        dsp::AnomalyEngine engine;
        dsp::AnomalyEngine::Context context;
        double sr = 48000.0, freeQ = 0.0, q0 = 0.0, qps = 0.0, midiQ = 0.0;
        bool running = false, fresh = true, holdsChanged = false;
    };

    class AnomalyProcessor final : public host::EffectProcessor
    {
    public:
        AnomalyProcessor() : EffectProcessor (effectInfo(), std::make_unique<AnomalyChain>()) {}
        AnomalyChain& anomaly() { return static_cast<AnomalyChain&> (getChain()); }
        juce::AudioProcessorEditor* createEditor() override;
    };

    // The lane grid: a palette per lane (click a slot), the TIME and VOLUME rows of the pattern being edited (click or
    // drag to paint, right-click to erase), the playing step, and COPY, PASTE, CLEAR, SHUFFLE and RESTORE.
    class LaneGrid final : public juce::Component, public ui::Themed
    {
    public:
        explicit LaneGrid (AnomalyProcessor& p) : processor (p) {}

        void refresh()
        {
            const int step = processor.anomaly().uiStep.load();
            const int playingPattern = processor.anomaly().uiPattern.load();
            if (step != shownStep || playingPattern != shownPattern || editPattern() != shownEdit)
            {
                shownStep = step;
                shownPattern = playingPattern;
                shownEdit = editPattern();
                repaint();
            }
        }

        void paint (juce::Graphics& g) override
        {
            const auto& t = *theme;
            layout();
            g.setFont (ui::monoFont (9.5f, true));
            for (int lane = 0; lane < 2; ++lane)
            {
                const int count = lane == 0 ? (int) Slot::numTime : (int) Slot::numVolume;
                for (int s = 1; s < count; ++s)
                {
                    const auto r = paletteCell (lane, s);
                    const bool on = selected[lane] == s;
                    g.setColour (on ? (lane == 0 ? t.accent : t.phosphor).withAlpha (0.35f) : t.panel);
                    g.fillRoundedRectangle (r, 2.0f);
                    g.setColour (on ? t.bright : t.dim);
                    g.drawRoundedRectangle (r, 2.0f, 1.0f);
                    g.drawText (lane == 0 ? Slot::timeInfo[s].code : Slot::volumeInfo[s].code, r.toNearestInt(), juce::Justification::centred);
                }
            }
            const int len = visibleLength();
            const bool playingHere = shownPattern == editPattern();
            for (int lane = 0; lane < 2; ++lane)
            {
                g.setColour (t.dim);
                g.drawText (lane == 0 ? "TIME" : "VOL", rowLabel (lane).toNearestInt(), juce::Justification::centredLeft);
                for (int s = 0; s < len; ++s)
                {
                    const auto r = gridCell (lane, s, len);
                    const int slot = processor.anomaly().store.cell (editPattern(), lane, s);
                    const bool beat = (s % 4) == 0;
                    g.setColour (slot != 0 ? (lane == 0 ? t.accent : t.phosphor).withAlpha (0.55f) : (beat ? t.faint.brighter (0.2f) : t.faint));
                    g.fillRect (r);
                    if (playingHere && s == shownStep % len)
                    {
                        g.setColour (t.bright);
                        g.drawRect (r, 1.5f);
                    }
                    if (slot != 0 && r.getWidth() > 13.0f)
                    {
                        g.setColour (t.bright);
                        g.setFont (ui::monoFont (std::min (9.0f, r.getWidth() * 0.32f), true));
                        g.drawText (lane == 0 ? Slot::timeInfo[slot].code : Slot::volumeInfo[slot].code, r.toNearestInt(), juce::Justification::centred);
                    }
                }
            }
            g.setFont (ui::monoFont (10.0f, true));
            g.setColour (t.phosphor);
            g.drawText ("EDITING PATTERN " + juce::String (editPattern() + 1) + "   PLAYING " + juce::String (shownPattern + 1),
                        footer().toNearestInt(), juce::Justification::centredLeft);
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            layout();
            for (int lane = 0; lane < 2; ++lane)
            {
                const int count = lane == 0 ? (int) Slot::numTime : (int) Slot::numVolume;
                for (int s = 1; s < count; ++s)
                    if (paletteCell (lane, s).contains (e.position))
                    {
                        selected[lane] = s;
                        repaint();
                        return;
                    }
            }
            paintAt (e);
        }

        void mouseDrag (const juce::MouseEvent& e) override { paintAt (e); }

        int editPattern() const { return std::clamp (juce::roundToInt (processor.effectRaw (Param::pattern)) - 1, 0, numPatterns - 1); }

        void copy() { for (int lane = 0; lane < 2; ++lane) for (int s = 0; s < maxSteps; ++s) clip[(size_t) (lane * maxSteps + s)] = (uint8_t) processor.anomaly().store.cell (editPattern(), lane, s); hasClip = true; }
        void paste() { if (! hasClip) return; for (int lane = 0; lane < 2; ++lane) for (int s = 0; s < maxSteps; ++s) processor.anomaly().store.setCell (editPattern(), lane, s, clip[(size_t) (lane * maxSteps + s)]); repaint(); }
        void clear() { for (int lane = 0; lane < 2; ++lane) for (int s = 0; s < maxSteps; ++s) processor.anomaly().store.setCell (editPattern(), lane, s, 0); repaint(); }
        void restore() { processor.anomaly().store.restore (editPattern()); repaint(); }

        // SHUFFLE: rotate each lane by a hashed number of beats and swap a few steps (the result is saved state, so an FL
        // render repeats it).
        void shuffle()
        {
            ++shuffleCount;
            const int len = visibleLength();
            auto& st = processor.anomaly().store;
            for (int lane = 0; lane < 2; ++lane)
            {
                std::array<int, maxSteps> row {};
                for (int s = 0; s < len; ++s)
                    row[(size_t) s] = st.cell (editPattern(), lane, s);
                const int rot = 4 * (int) (dsp::hash01 (shuffleCount, lane, 0x5F1Eu, processor.getInstanceId()) * (float) (len / 4));
                for (int s = 0; s < len; ++s)
                    st.setCell (editPattern(), lane, s, row[(size_t) ((s + rot) % len)]);
                for (int k = 0; k < len / 8; ++k)
                {
                    const int a = (int) (dsp::hash01 (shuffleCount, 10 + k, 0x5F1Eu, processor.getInstanceId()) * (float) len);
                    const int b = (int) (dsp::hash01 (shuffleCount, 40 + k, 0x5F1Eu, processor.getInstanceId()) * (float) len);
                    const int sa = st.cell (editPattern(), lane, a);
                    st.setCell (editPattern(), lane, a, st.cell (editPattern(), lane, b));
                    st.setCell (editPattern(), lane, b, sa);
                }
            }
            repaint();
        }

    private:
        int visibleLength() const { return lengths[std::clamp (juce::roundToInt (processor.effectRaw (Param::length)), 0, 2)]; }

        void layout()
        {
            area = getLocalBounds().toFloat();
            palette = area.removeFromTop (44.0f);
            area.removeFromTop (6.0f);
            foot = area.removeFromBottom (16.0f);
        }

        juce::Rectangle<float> paletteCell (int lane, int s) const
        {
            const float w = palette.getWidth() / 12.0f;
            return { palette.getX() + w * (float) (s - 1) + 1.0f, palette.getY() + (float) lane * 22.0f + 1.0f, w - 2.0f, 20.0f };
        }

        juce::Rectangle<float> rowLabel (int lane) const
        {
            const float h = area.getHeight() / 2.0f;
            return { area.getX(), area.getY() + h * (float) lane, 34.0f, h };
        }

        juce::Rectangle<float> gridCell (int lane, int s, int len) const
        {
            const float x0 = area.getX() + 36.0f, w = (area.getRight() - x0) / (float) len;
            const float h = area.getHeight() / 2.0f;
            return { x0 + w * (float) s + 1.0f, area.getY() + h * (float) lane + 2.0f, std::max (1.0f, w - 2.0f), h - 4.0f };
        }

        juce::Rectangle<float> footer() const { return foot; }

        void paintAt (const juce::MouseEvent& e)
        {
            const int len = visibleLength();
            for (int lane = 0; lane < 2; ++lane)
                for (int s = 0; s < len; ++s)
                    if (gridCell (lane, s, len).expanded (1.0f).contains (e.position))
                    {
                        processor.anomaly().store.setCell (editPattern(), lane, s, e.mods.isRightButtonDown() ? 0 : selected[lane]);
                        repaint();
                        return;
                    }
        }

        AnomalyProcessor& processor;
        juce::Rectangle<float> area, palette, foot;
        int selected[2] { (int) Slot::spring16, (int) Slot::gate16 };
        int shownStep = -1, shownPattern = -1, shownEdit = -1, shuffleCount = 0;
        std::array<uint8_t, 2 * maxSteps> clip {};
        bool hasClip = false;
    };

    class AnomalyEditor final : public ui::EffectEditor
    {
    public:
        explicit AnomalyEditor (AnomalyProcessor& p) : EffectEditor (p)
        {
            grid = own (std::make_unique<LaneGrid> (p));
            std::vector<Item> tools;
            for (const auto& [label, tip] : { std::pair { "COPY", "Copy the pattern being edited" }, std::pair { "PASTE", "Paste over it" },
                                               std::pair { "CLEAR", "Empty it" }, std::pair { "SHUFFLE", "Rotate and swap its steps" },
                                               std::pair { "RESTORE", "Back to the factory pattern" } })
            {
                auto* b = own (std::make_unique<juce::TextButton> (label));
                ui::styleButton (*b, tip);
                buttons.push_back (b);
                tools.push_back ({ b, 0.8f });
            }
            buttons[0]->onClick = [this] { grid->copy(); };
            buttons[1]->onClick = [this] { grid->paste(); };
            buttons[2]->onClick = [this] { grid->clear(); };
            buttons[3]->onClick = [this] { grid->shuffle(); };
            buttons[4]->onClick = [this] { grid->restore(); };
            readout = own (std::make_unique<ui::BigReadout>());

            addRow ({ panel ("LANES // ПОЛОСЫ", { { grid, 10.0f } }, true) });
            addRow ({ panel ("PATTERN // СХЕМА", { { knob ("pattern", "PATTERN", true), 1.0f },
                                                  { choices ({ { "resolution", "STEP" }, { "length", "LENGTH" } }), 1.2f },
                                                  { choices ({ { "run", "RUN" } }), 1.1f } }),
                      panel ("TOOLS // ИНСТРУМЕНТЫ", std::move (tools)),
                      panel ("FIELD // ПОЛЕ", { { readout, 2.0f }, { knob ("field", "FIELD", true), 1.0f } }, true) });
            addRow ({ panel ("ANOMALY // АНОМАЛИЯ", { { knob ("time_mix", "TIME MIX"), 1.0f }, { knob ("volume_mix", "VOL MIX"), 1.0f },
                                                     { knob ("scratch_depth", "SCRATCH"), 1.0f } }),
                      outputPanel() });
            finish();
        }

        void resized() override
        {
            EffectEditor::resized();
            for (auto* b : buttons)
                b->setBounds (b->getBounds().withSizeKeepingCentre (b->getWidth() - 4, 26));
        }

    private:
        void refresh (float) override
        {
            grid->refresh();
            const auto& m = processor.meters;
            const int ts = juce::roundToInt (m.custom[0].load()), vs = juce::roundToInt (m.custom[1].load());
            juce::String now = ts > 0 ? juce::String (Slot::timeInfo[std::clamp (ts, 0, (int) Slot::numTime - 1)].name) : juce::String ("LIVE");
            if (vs > 0)
                now << " + " << Slot::volumeInfo[std::clamp (vs, 0, (int) Slot::numVolume - 1)].code;
            readout->set (m.custom[4].load() > 0.5f ? "ACTIVE" : "TRANSPORT STOPPED", now.toUpperCase(), ts > 0);
        }

        LaneGrid* grid = nullptr;
        ui::BigReadout* readout = nullptr;
        std::vector<juce::TextButton*> buttons;
    };

    juce::AudioProcessorEditor* AnomalyProcessor::createEditor() { return new AnomalyEditor (*this); }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AnomalyProcessor();
}
