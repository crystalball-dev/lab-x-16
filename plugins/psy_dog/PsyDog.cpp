// PSY-DOG // ПСИ-СОБАКА — the psy-dog's illusory pack: copies that vanish when struck. Up to six phantoms, each the
// input delayed, slowly detuned and smeared through all-pass chains (a different chain per side, so each phantom is wide
// on its own), panned out to alternate sides, each 25-140 ms behind the track so the pack is heard as a pack and not as
// a doubling. An attack makes them vanish within VANISH; they hold off for 25 ms and BLOOM brings them back, so between
// the hits of a loop they slip in again.

#include "core/host/EffectProcessor.h"
#include "core/dsp/Allpass.h"
#include "core/dsp/Filters.h"
#include "core/dsp/Generators.h"
#include "core/dsp/MicroShift.h"
#include "core/dsp/Onset.h"
#include "ui/EffectEditor.h"

namespace
{
    using namespace lab;
    using host::Kind;
    using host::Smooth;

    enum Param : int { pack = 0, phantom, bloom, vanish, spread, detune, darken };
    constexpr int maxPhantoms = 6, stages = 4;

    const host::EffectInfo& effectInfo()
    {
        static const host::EffectInfo i = []
        {
            host::EffectInfo e;
            e.product = LABX16_PRODUCT;
            e.subtitle = "ПСИ-СОБАКА";
            e.slug = LABX16_SLUG;
            e.version = LABX16_VERSION;
            e.code = LABX16_CODE;
            e.faction = host::Faction::deepZone;
            e.params = {
                { "pack",    "Pack",    Kind::integer, 1.0f,  6.0f,    3.0f,   0.0f,   Smooth::none,   "",   "" },
                { "phantom", "Phantom", Kind::real,    0.0f,  1.0f,    0.6f,   0.0f,   Smooth::linear, "%",  "" },
                { "bloom",   "Bloom",   Kind::real,    50.0f, 2000.0f, 180.0f, 400.0f, Smooth::none,   "ms", "" },
                { "vanish",  "Vanish",  Kind::real,    5.0f,  200.0f,  30.0f,  40.0f,  Smooth::none,   "ms", "" },
                { "spread",  "Spread",  Kind::real,    0.0f,  1.0f,    0.8f,   0.0f,   Smooth::linear, "%",  "" },
                { "detune",  "Detune",  Kind::real,    0.0f,  30.0f,   12.0f,  0.0f,   Smooth::linear, "ct", "" },
                { "darken",  "Darken",  Kind::real,    0.0f,  1.0f,    0.3f,   0.0f,   Smooth::linear, "%",  "" },
            };
            e.stages = { "pack" };
            e.presets = {
                { "Init", {} },
                { "Pack of Three", { { "pack", 3.0f }, { "phantom", 0.75f }, { "output", -1.1f } } },
                { "Six Phantoms", { { "pack", 6.0f }, { "phantom", 0.85f }, { "spread", 1.0f }, { "detune", 16.0f }, { "output", -1.3f } } },
                { "Vanish Fast", { { "vanish", 5.0f }, { "bloom", 90.0f }, { "phantom", 0.8f }, { "output", -1.6f } } },
                { "Slow Bloom", { { "bloom", 900.0f }, { "vanish", 20.0f }, { "phantom", 0.8f }, { "output", -0.2f } } },
                { "Dark Pack", { { "darken", 0.85f }, { "pack", 5.0f }, { "detune", 18.0f }, { "phantom", 0.8f }, { "output", -1.1f } } },
                { "Wide Illusion", { { "spread", 1.0f }, { "pack", 4.0f }, { "detune", 24.0f }, { "phantom", 0.75f }, { "output", -1.2f } } },
            };
            return e;
        }();
        return i;
    }

    class PsyDogChain final : public host::Chain
    {
    public:
        void prepare (double sampleRate, int) override
        {
            sr = (float) sampleRate;
            detector.prepare (sr);
            for (auto& ph : phantoms)
            {
                ph.delay.prepare ((int) std::ceil (0.15f * sr));
                ph.shift.prepare (sr, 0.03f);
                ph.shift.setWindow (0.02f);
                for (auto& side : ph.allpass)
                    for (auto& a : side)
                        a.prepare ((int) std::ceil (0.024f * sr) + 4);
            }
            holdSamples = (int) (0.025f * sr);
            configure (callsign);
            reset();
        }

        void reset() override
        {
            detector.reset();
            for (auto& ph : phantoms)
            {
                ph.delay.reset();
                ph.shift.reset();
                for (auto& side : ph.allpass)
                    for (auto& a : side)
                        a.reset();
                ph.lpL.reset();
                ph.lpR.reset();
                ph.walk.reset (sr);
            }
            duckGain = 1.0f;
            holdLeft = 0;
        }

        double tailSeconds (const float*) const override { return 0.4; }

        void control (const float* v, const host::BlockContext& ctx, int n) override
        {
            if (ctx.callsign != callsign)
            {
                callsign = ctx.callsign;
                configure (callsign);
            }
            count = std::clamp ((int) std::lround (v[pack]), 1, maxPhantoms);
            level = v[phantom] * 1.1f / std::sqrt ((float) count);
            vanishStep = 1.0f / std::max (1.0f, v[vanish] * 0.001f * sr);
            bloomCoef = dsp::onePoleCoef (v[bloom] * 0.001f / 2.2f, sr);   // within 1 dB of full after BLOOM
            const float darkHz = 18000.0f * (1.0f - 0.85f * v[darken]);
            for (int k = 0; k < maxPhantoms; ++k)
            {
                auto& ph = phantoms[(size_t) k];
                ph.lpL.setHz (darkHz, sr);
                ph.lpR.setHz (darkHz, sr);
                const float pan = std::clamp (ph.side * v[spread] * ph.panAmount, -1.0f, 1.0f);
                dsp::panGains (pan, ph.gainL, ph.gainR);
                ph.ratio = dsp::MicroShift::centsToRatio (std::max (1.0f, v[detune]) * ph.walk.advance (n, 0.6f));
            }
        }

        void process (float* l, float* r, const float*, const float*, int n, host::Guard& guard) override
        {
            for (int i = 0; i < n; ++i)
            {
                // An attack (the fast envelope 6 dB over the slow one) sends the pack away; it holds off, then blooms back.
                detector.process (l[i], r[i]);
                if (detector.attackRatio() > 2.0f && detector.fastEnvelope() > 1.0e-3f)
                    holdLeft = holdSamples;
                if (holdLeft > 0)
                {
                    --holdLeft;
                    duckGain = std::max (0.0f, duckGain - vanishStep);
                }
                else
                    duckGain += (1.0f - duckGain) * bloomCoef;

                const float mono = 0.5f * (l[i] + r[i]);
                float wl = 0.0f, wr = 0.0f;
                for (int k = 0; k < count; ++k)
                {
                    auto& ph = phantoms[(size_t) k];
                    ph.delay.push (mono);
                    const float d = ph.shift.process (ph.delay.readInt (0, ph.delaySamples), ph.ratio);
                    float a = d, b = d;
                    for (auto& ap : ph.allpass[0]) a = ap.process (a);
                    for (auto& ap : ph.allpass[1]) b = ap.process (b);
                    wl += ph.lpL.lowpass (a) * ph.gainL;
                    wr += ph.lpR.lowpass (b) * ph.gainR;
                }
                const float g = level * duckGain;
                l[i] += wl * g;
                r[i] += wr * g;
            }
            guard (0, l, r, n);
        }

        void meter (host::Meters& m) const override
        {
            m.custom[0].store (duckGain);
            m.custom[1].store ((float) count);
        }

    private:
        struct Phantom
        {
            dsp::DelayLine delay;
            dsp::MicroShift shift;
            std::array<std::array<dsp::Allpass, stages>, 2> allpass;
            dsp::OnePole lpL, lpR;
            dsp::Programmer walk;
            int delaySamples = 480;
            float side = 1.0f, panAmount = 0.85f, gainL = 1.0f, gainR = 1.0f, ratio = 1.0f;
        };

        // Each phantom's delay (7-30 ms), all-pass delays (5-23 ms, different per side), side and walk are hash01
        // decisions on the callsign: an instance always summons the same pack.
        void configure (uint32_t salt)
        {
            for (int k = 0; k < maxPhantoms; ++k)
            {
                auto& ph = phantoms[(size_t) k];
                ph.delaySamples = (int) ((25.0f + 115.0f * dsp::hash01 (k, 0, 0xD06u, salt)) * 0.001f * sr);
                for (int s = 0; s < 2; ++s)
                    for (int j = 0; j < stages; ++j)
                        ph.allpass[(size_t) s][(size_t) j].set ((int) ((5.0f + 18.0f * dsp::hash01 (k, 10 + s * stages + j, 0xD06u, salt)) * 0.001f * sr), 0.6f);
                ph.side = (k & 1) == 0 ? -1.0f : 1.0f;
                ph.panAmount = 0.7f + 0.3f * dsp::hash01 (k, 30, 0xD06u, salt);
                ph.walk.seed (dsp::hash32 (k, 40, 0xD06u, salt) | 1u);
                ph.walk.reset (sr);
            }
        }

        std::array<Phantom, maxPhantoms> phantoms;
        dsp::OnsetDetector detector;
        float sr = 48000.0f, level = 0.3f, duckGain = 1.0f, vanishStep = 0.001f, bloomCoef = 0.0001f;
        int count = 3, holdSamples = 2880, holdLeft = 0;
        uint32_t callsign = 0;
    };

    class PsyDogProcessor final : public host::EffectProcessor
    {
    public:
        PsyDogProcessor() : EffectProcessor (effectInfo(), std::make_unique<PsyDogChain>()) {}
        juce::AudioProcessorEditor* createEditor() override;
    };

    class PsyDogEditor final : public ui::EffectEditor
    {
    public:
        explicit PsyDogEditor (PsyDogProcessor& p) : EffectEditor (p)
        {
            needle = own (std::make_unique<ui::Needle> ("PACK", 0.95f));
            addRow ({ panel ("PACK // СТАЯ", { { needle, 1.8f }, { knob ("pack", "PACK"), 1.0f }, { knob ("phantom", "PHANTOM", true), 1.0f } }, true),
                      panel ("ILLUSION // ИЛЛЮЗИЯ", { { knob ("vanish", "VANISH"), 1.0f }, { knob ("bloom", "BLOOM"), 1.0f } }) });
            addRow ({ panel ("SHAPE // ФОРМА", { { knob ("spread", "SPREAD"), 1.0f }, { knob ("detune", "DETUNE"), 1.0f },
                                                { knob ("darken", "DARKEN"), 1.0f } }),
                      outputPanel() });
            finish();
        }

    private:
        void refresh (float seconds) override
        {
            const auto& m = processor.meters;
            const float present = m.custom[0].load();
            needle->setReadout (juce::String (juce::roundToInt (m.custom[1].load())) + " x " + juce::String (juce::roundToInt (present * 100.0f)) + "%");
            needle->advance (present, seconds, 0.3f * (1.0f - present));
        }

        ui::Needle* needle = nullptr;
    };

    juce::AudioProcessorEditor* PsyDogProcessor::createEditor() { return new PsyDogEditor (*this); }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PsyDogProcessor();
}
