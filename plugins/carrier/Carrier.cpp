// CARRIER // НЕСУЩАЯ — the Radar array modelled on Duga-1, the Russian Woodpecker, whose signal tapped at about 10 Hz,
// and the Scorcher's psy-antenna voices. A vowel FORMANT bank, a COMB bank tuned to a chord (or to held MIDI notes), a
// true frequency SHIFT by single-sideband modulation, the WOODPECKER pulse gate and ensemble WIDTH.

#include "core/host/EffectProcessor.h"
#include "core/dsp/Ensemble.h"
#include "core/dsp/FormantBank.h"
#include "core/dsp/Hilbert.h"
#include "core/dsp/Resonator.h"
#include "ui/EffectEditor.h"

namespace
{
    using namespace lab;
    using host::Kind;
    using host::Smooth;

    enum Param : int { vowel = 0, formantSize, formantMix, pitchSource, root, chord, combFeedback, combLevel, shiftHz, sideband,
                       amDepth, woodpecker, wpSync, width, transmit };

    // LAB X-19's chord shapes: semitone offsets from the root.
    struct ChordShape { const char* name; int count; int intervals[6]; };
    constexpr ChordShape chordShapes[] = {
        { "Off", 1, { 0 } }, { "Major", 3, { 0, 4, 7 } }, { "Minor", 3, { 0, 3, 7 } }, { "Sus2", 3, { 0, 2, 7 } },
        { "Sus4", 3, { 0, 5, 7 } }, { "Add9", 4, { 0, 4, 7, 14 } }, { "Maj7", 4, { 0, 4, 7, 11 } }, { "Min7", 4, { 0, 3, 7, 10 } },
        { "Dom7", 4, { 0, 4, 7, 10 } }, { "Maj9", 5, { 0, 4, 7, 11, 14 } }, { "Min9", 5, { 0, 3, 7, 10, 14 } },
        { "6/9", 5, { 0, 4, 7, 9, 14 } }, { "Min11", 6, { 0, 3, 7, 10, 14, 17 } }, { "Fifths", 3, { 0, 7, 14 } },
        { "Octaves", 2, { 0, 12 } }, { "Lydian", 5, { 0, 4, 7, 11, 18 } },
    };
    constexpr const char* chordChoices = "Off|Major|Minor|Sus2|Sus4|Add9|Maj7|Min7|Dom7|Maj9|Min9|6/9|Min11|Fifths|Octaves|Lydian";
    constexpr int maxCombs = 6;

    const host::EffectInfo& effectInfo()
    {
        static const host::EffectInfo i = []
        {
            host::EffectInfo e;
            e.product = LABX16_PRODUCT;
            e.subtitle = "НЕСУЩАЯ";
            e.slug = LABX16_SLUG;
            e.version = LABX16_VERSION;
            e.code = LABX16_CODE;
            e.faction = host::Faction::etherealSignal;
            e.midi = true;
            e.params = {
                { "vowel",         "Vowel",          Kind::real,    0.0f,     1.0f,    0.5f,  0.0f, Smooth::linear, "%",  "" },
                { "formant_size",  "Formant Size",   Kind::real,    -1.0f,    1.0f,    0.0f,  0.0f, Smooth::linear, "%",  "" },
                { "formant_mix",   "Formant Mix",    Kind::real,    0.0f,     1.0f,    0.5f,  0.0f, Smooth::linear, "%",  "" },
                { "pitch_source",  "Pitch Source",   Kind::choice,  0.0f,     1.0f,    0.0f,  0.0f, Smooth::none,   "",   "Chord|MIDI" },
                { "root",          "Root",           Kind::integer, 24.0f,    84.0f,   48.0f, 0.0f, Smooth::none,   "",   "" },
                { "chord",         "Chord",          Kind::choice,  0.0f,     15.0f,   13.0f, 0.0f, Smooth::none,   "",   chordChoices },
                { "comb_feedback", "Comb Feedback",  Kind::real,    0.0f,     1.0f,    0.6f,  0.0f, Smooth::linear, "%",  "" },
                { "comb_level",    "Comb Level",     Kind::real,    0.0f,     1.0f,    0.5f,  0.0f, Smooth::linear, "%",  "" },
                { "shift_hz",      "Shift",          Kind::real,    -2000.0f, 2000.0f, 0.0f,  0.0f, Smooth::linear, "Hz", "" },
                { "sideband",      "Sideband",       Kind::choice,  0.0f,     2.0f,    1.0f,  0.0f, Smooth::none,   "",   "Both|Upper|Lower" },
                { "am_depth",      "Carrier Depth",  Kind::real,    0.0f,     1.0f,    0.0f,  0.0f, Smooth::linear, "%",  "" },
                { "woodpecker",    "Woodpecker",     Kind::real,    0.0f,     1.0f,    0.0f,  0.0f, Smooth::linear, "%",  "" },
                { "wp_sync",       "Woodpecker Rate",Kind::choice,  0.0f,     3.0f,    0.0f,  0.0f, Smooth::none,   "",   "10 Hz|1/16|1/32|1/8" },
                { "width",         "Width",          Kind::real,    0.0f,     1.0f,    0.5f,  0.0f, Smooth::linear, "%",  "" },
                { "transmit",      "Transmit",       Kind::real,    0.0f,     1.0f,    0.0f,  0.0f, Smooth::linear, "%",  "" },
            };
            e.stages = { "banks", "shift" };
            e.presets = {
                { "Init", {} },
                { "Radar Carrier", { { "am_depth", 0.35f }, { "shift_hz", 7.0f }, { "comb_level", 0.35f }, { "width", 0.7f }, { "output", 10.6f } } },
                { "Duga Pulse", { { "woodpecker", 1.0f }, { "comb_level", 0.45f }, { "formant_mix", 0.3f }, { "output", 8.0f } } },
                { "Vowel Chord", { { "chord", 2.0f }, { "vowel", 0.75f }, { "formant_mix", 0.7f }, { "comb_level", 0.6f }, { "comb_feedback", 0.75f }, { "output", 10.6f } } },
                { "Shifted Up", { { "shift_hz", 120.0f }, { "am_depth", 1.0f }, { "comb_level", 0.2f }, { "formant_mix", 0.2f }, { "output", 3.8f } } },
                { "Lower Sideband", { { "shift_hz", 80.0f }, { "sideband", 2.0f }, { "am_depth", 0.8f }, { "width", 0.8f }, { "output", 9.3f } } },
                { "Chant Bank", { { "comb_level", 0.85f }, { "comb_feedback", 0.85f }, { "pitch_source", 1.0f }, { "vowel", 0.25f },
                                  { "formant_mix", 0.6f }, { "output", 9.4f } } },
            };
            return e;
        }();
        return i;
    }

    class CarrierChain final : public host::Chain
    {
    public:
        void prepare (double sampleRate, int) override
        {
            sr = (float) sampleRate;
            formants.prepare (sr);
            for (auto& c : combs)
                c.prepare (sr, 20.0f);
            for (auto& s : shifters)
                s.prepare (sampleRate);
            ensemble.prepare (sr);
            edge = 1.0f / std::max (1.0f, 0.002f * sr);
            reset();
        }

        void reset() override
        {
            formants.reset();
            for (auto& c : combs)
                c.reset();
            for (auto& s : shifters)
                s.reset();
            ensemble.reset();
            gate = 1.0f;
            wpPhase = 0.0;
        }

        double tailSeconds (const float* v) const override { return 0.2 + 3.0 * (double) v[combFeedback]; }

        void midi (const juce::MidiMessage& m, const host::BlockContext&) override
        {
            if (m.isNoteOn())
            {
                for (int k = 0; k < heldCount; ++k)
                    if (held[(size_t) k] == m.getNoteNumber())
                        return;
                if (heldCount < maxCombs)
                    held[(size_t) heldCount++] = m.getNoteNumber();
            }
            else if (m.isNoteOff())
            {
                for (int k = 0; k < heldCount; ++k)
                    if (held[(size_t) k] == m.getNoteNumber())
                    {
                        for (int j = k; j + 1 < heldCount; ++j)
                            held[(size_t) j] = held[(size_t) j + 1];
                        --heldCount;
                        break;
                    }
            }
            else if (m.isAllNotesOff() || m.isAllSoundOff())
                heldCount = 0;
        }

        void control (const float* v, const host::BlockContext& ctx, int n) override
        {
            const float tx = v[transmit];
            formants.set (v[vowel], v[formantSize]);
            formantMixNow = v[formantMix];

            // The comb notes: the chord on ROOT, or the held MIDI notes (the chord while none are held).
            const auto& shape = chordShapes[std::clamp ((int) std::lround (v[chord]), 0, 15)];
            if (v[pitchSource] > 0.5f && heldCount > 0)
            {
                combCount = heldCount;
                for (int k = 0; k < heldCount; ++k)
                    notes[(size_t) k] = (float) held[(size_t) k];
            }
            else
            {
                combCount = shape.count;
                for (int k = 0; k < shape.count; ++k)
                    notes[(size_t) k] = v[root] + (float) shape.intervals[k];
            }
            const float loop = v[combFeedback] * 0.98f;
            for (int k = 0; k < maxCombs; ++k)
                combs[(size_t) k].set (dsp::midiToHz (notes[(size_t) std::min (k, combCount - 1)]), loop, n);
            combGain = v[combLevel] / std::sqrt ((float) std::max (1, combCount)) * (1.0f - 0.6f * loop);

            shiftNow = v[shiftHz];
            sidebandNow = (int) std::lround (v[sideband]);
            amNow = std::clamp (v[amDepth] + 0.5f * tx, 0.0f, 1.0f);
            wpDepth = std::clamp (v[woodpecker] + 0.5f * tx, 0.0f, 1.0f);
            widthNow = std::clamp (v[width] + 0.4f * tx, 0.0f, 1.0f);

            // WOODPECKER: 10 Hz, or a division locked to FL's beat while it plays.
            const auto& t = *ctx.transport;
            const int wp = (int) std::lround (v[wpSync]);
            static constexpr double perBeat[4] = { 0.0, 4.0, 8.0, 2.0 };
            if (wp == 0)
                wpInc = 10.0 / (double) sr;
            else
            {
                wpInc = t.bpm / 60.0 * perBeat[wp] / (double) sr;
                if (t.playing)
                    wpPhase = t.ppq * perBeat[wp] - std::floor (t.ppq * perBeat[wp]);
            }
        }

        void process (float* l, float* r, const float*, const float*, int n, host::Guard& guard) override
        {
            for (int i = 0; i < n; ++i)
            {
                const float fl = formants.process (0, l[i]), fr = formants.process (1, r[i]);
                float a = l[i] + (fl - l[i]) * formantMixNow;
                float b = r[i] + (fr - r[i]) * formantMixNow;
                float comb = 0.0f;
                const float mono = 0.5f * (a + b);
                for (int k = 0; k < combCount; ++k)
                    comb += combs[(size_t) k].process (mono);
                a += comb * combGain;
                b += comb * combGain;
                l[i] = a;
                r[i] = b;
            }
            guard (0, l, r, n);

            for (int i = 0; i < n; ++i)
            {
                const float sl = shifters[0].process (l[i], shiftNow, sidebandNow);
                const float srr = shifters[1].process (r[i], shiftNow, sidebandNow);
                float a = l[i] + (sl - l[i]) * amNow;
                float b = r[i] + (srr - r[i]) * amNow;
                // The pulse: on for half of each cycle, 2 ms edges.
                wpPhase += wpInc;
                wpPhase -= std::floor (wpPhase);
                const float target = wpPhase < 0.5 ? 1.0f : 0.0f;
                gate += std::clamp (target - gate, -edge, edge);
                const float g = 1.0f - wpDepth * (1.0f - gate);
                l[i] = a * g;
                r[i] = b * g;
            }
            ensemble.process (l, r, n, widthNow * 0.6f, widthNow, 0.6f);
            guard (1, l, r, n);
        }

        void meter (host::Meters& m) const override
        {
            m.custom[0].store ((float) combCount);
            m.custom[1].store (notes[0]);
            m.custom[2].store (gate);
        }

    private:
        dsp::FormantBank formants;
        std::array<dsp::CombFilter, maxCombs> combs;
        std::array<dsp::FrequencyShifter, 2> shifters;
        dsp::Ensemble ensemble;
        std::array<float, maxCombs> notes { 48.0f, 55.0f, 62.0f, 48.0f, 48.0f, 48.0f };
        std::array<int, maxCombs> held {};
        int heldCount = 0, combCount = 3, sidebandNow = 1;
        float sr = 48000.0f, formantMixNow = 0.5f, combGain = 0.3f, shiftNow = 0.0f, amNow = 0.0f, wpDepth = 0.0f, widthNow = 0.5f;
        float gate = 1.0f, edge = 0.01f;
        double wpInc = 0.0, wpPhase = 0.0;
    };

    class CarrierProcessor final : public host::EffectProcessor
    {
    public:
        CarrierProcessor() : EffectProcessor (effectInfo(), std::make_unique<CarrierChain>()) {}
        juce::AudioProcessorEditor* createEditor() override;
    };

    class CarrierEditor final : public ui::EffectEditor
    {
    public:
        explicit CarrierEditor (CarrierProcessor& p) : EffectEditor (p)
        {
            needle = own (std::make_unique<ui::Needle> ("CARRIER", 0.9f));
            addRow ({ panel ("FORMANT // ФОРМАНТА", { { knob ("vowel", "VOWEL"), 1.0f }, { knob ("formant_size", "SIZE"), 1.0f },
                                                     { knob ("formant_mix", "MIX"), 1.0f } }),
                      panel ("COMB // ГРЕБЁНКА", { { choices ({ { "pitch_source", "PITCH" }, { "chord", "CHORD" } }), 1.3f },
                                                  { knob ("root", "ROOT"), 1.0f }, { knob ("comb_feedback", "FEEDBACK"), 1.0f },
                                                  { knob ("comb_level", "LEVEL"), 1.0f } }) });
            addRow ({ panel ("SHIFT // СДВИГ", { { knob ("shift_hz", "SHIFT"), 1.0f }, { choices ({ { "sideband", "SIDEBAND" } }), 1.2f },
                                                { knob ("am_depth", "DEPTH"), 1.0f } }),
                      panel ("WOODPECKER // ДЯТЕЛ", { { knob ("woodpecker", "PULSE"), 1.0f }, { choices ({ { "wp_sync", "RATE" } }), 1.2f } }),
                      panel ("WIDTH // ШИРИНА", { { knob ("width", "WIDTH"), 1.0f } }) });
            addRow ({ panel ("TRANSMIT // ПЕРЕДАЧА", { { needle, 1.8f }, { knob ("transmit", "TRANSMIT", true), 1.0f } }, true),
                      outputPanel() });
            finish();
        }

    private:
        void refresh (float seconds) override
        {
            const auto& m = processor.meters;
            const int combsNow = juce::roundToInt (m.custom[0].load());
            static const char* names[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
            const int note = juce::roundToInt (m.custom[1].load());
            needle->setReadout (juce::String (names[((note % 12) + 12) % 12]) + juce::String (note / 12 - 1) + " x" + juce::String (combsNow));
            needle->advance (0.15f + 0.85f * m.custom[2].load() * processor.effectRaw (Param::transmit) + 0.2f, seconds);
        }

        ui::Needle* needle = nullptr;
    };

    juce::AudioProcessorEditor* CarrierProcessor::createEditor() { return new CarrierEditor (*this); }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new CarrierProcessor();
}
