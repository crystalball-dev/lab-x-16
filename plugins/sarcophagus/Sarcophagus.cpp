// SARCOPHAGUS // САРКОФАГ — radioactive decay chains, the Bateman equations. Reactor 4 under its sarcophagus, the long
// tail of what was deposited. Each hit deposits a parent isotope and its own sound; up to four generations decay into
// one another with their own half-lives, and each emits particles, grains of the hit replayed, at a rate following its
// activity (lambda x N). So a hit's tail is dense and fast first, then sparse and slow, pitched and darkened generation
// by generation. Spawns are hash01 decisions on the block and the callsign, so a render repeats exactly.

#include "core/host/EffectProcessor.h"
#include "core/dsp/Filters.h"
#include "core/dsp/Onset.h"
#include "ui/EffectEditor.h"
#include "ui/Views.h"

namespace
{
    using namespace lab;
    using host::Kind;
    using host::Smooth;

    enum Param : int { dose = 0, activity, halfLife, daughters, pitchStep, attenuation, darken, grain, spread, sensitivity };
    constexpr int maxDeposits = 16, maxGrains = 32, generationsMax = 4;
    constexpr float baseHalfLives[generationsMax] = { 0.05f, 0.3f, 1.5f, 6.0f };
    constexpr double captureSeconds = 0.25, preRollSeconds = 0.002;

    const host::EffectInfo& effectInfo()
    {
        static const host::EffectInfo i = []
        {
            host::EffectInfo e;
            e.product = LABX16_PRODUCT;
            e.subtitle = "САРКОФАГ";
            e.slug = LABX16_SLUG;
            e.version = LABX16_VERSION;
            e.code = LABX16_CODE;
            e.faction = host::Faction::deepZone;
            e.params = {
                { "dose",        "Dose",        Kind::real,    0.0f,   1.0f,   0.6f,  0.0f,  Smooth::none,   "%",  "" },
                { "activity",    "Activity",    Kind::real,    0.0f,   1.0f,   0.5f,  0.0f,  Smooth::none,   "%",  "" },
                { "half_life",   "Half-life",   Kind::real,    0.1f,   10.0f,  1.0f,  1.0f,  Smooth::none,   "x",  "" },
                { "daughters",   "Daughters",   Kind::integer, 1.0f,   4.0f,   3.0f,  0.0f,  Smooth::none,   "",   "" },
                { "pitch_step",  "Pitch Step",  Kind::real,    -12.0f, 12.0f,  -5.0f, 0.0f,  Smooth::none,   "st", "" },
                { "attenuation", "Attenuation", Kind::real,    0.3f,   1.0f,   0.7f,  0.0f,  Smooth::none,   "x",  "" },
                { "darken",      "Darken",      Kind::real,    0.0f,   1.0f,   0.3f,  0.0f,  Smooth::none,   "%",  "" },
                { "grain",       "Grain",       Kind::real,    30.0f,  120.0f, 60.0f, 60.0f, Smooth::none,   "ms", "" },
                { "spread",      "Spread",      Kind::real,    0.0f,   1.0f,   0.7f,  0.0f,  Smooth::none,   "%",  "" },
                { "sensitivity", "Sensitivity", Kind::real,    0.0f,   1.0f,   0.5f,  0.0f,  Smooth::linear, "%",  "" },
            };
            e.stages = { "chain" };
            e.presets = {
                { "Init", {} },
                { "Reactor Four", { { "dose", 0.8f }, { "daughters", 4.0f }, { "activity", 0.6f } } },
                { "Short Chain", { { "daughters", 1.0f }, { "activity", 0.7f } } },
                { "Heavy Daughters", { { "pitch_step", -12.0f }, { "darken", 0.5f } } },
                { "Cesium Tail", { { "half_life", 10.0f }, { "activity", 0.4f } } },
                { "Sparse Decay", { { "activity", 0.2f }, { "grain", 100.0f } } },
                { "Dense Crackle", { { "activity", 1.0f }, { "grain", 30.0f }, { "half_life", 0.3f } } },
            };
            return e;
        }();
        return i;
    }

    class SarcophagusChain final : public host::Chain
    {
    public:
        std::array<std::atomic<float>, generationsMax> uiActivity {};

        void prepare (double sampleRate, int) override
        {
            sr = (float) sampleRate;
            detector.prepare (sr);
            captureLength = (int) std::ceil (captureSeconds * sampleRate);
            preRoll = std::max (1, (int) std::lround (preRollSeconds * sampleRate));
            densityCoef = dsp::onePoleCoef (0.002f, sr);
            for (auto& d : deposits)
            {
                d.l.assign ((size_t) captureLength + 4, 0.0f);
                d.r.assign ((size_t) captureLength + 4, 0.0f);
            }
            reset();
        }

        void reset() override
        {
            detector.reset();
            for (auto& d : deposits)
                d.active = false;
            for (auto& g : grains)
                g.active = false;
            preL.fill (0.0f);
            preR.fill (0.0f);
            prePos = 0;
            spawned.fill (0);
            blocks = 0;
            density = 1.0f;
        }

        double tailSeconds (const float* v) const override { return std::min (120.0, 10.0 * baseHalfLives[3] * (double) v[halfLife]); }

        void control (const float* v, const host::BlockContext& ctx, int n) override
        {
            if (ctx.callsign != callsign)
                callsign = ctx.callsign;
            detector.setSensitivity (v[sensitivity]);
            doseNow = v[dose];
            daughtersNow = std::clamp ((int) std::lround (v[daughters]), 1, generationsMax);
            for (int k = 0; k < generationsMax; ++k)
                lambda[(size_t) k] = 0.69314718f / (baseHalfLives[k] * std::max (0.05f, v[halfLife]));
            grainSamples = std::max (16, (int) (v[grain] * 0.001f * sr));
            spreadNow = v[spread];
            for (int k = 0; k < generationsMax; ++k)
            {
                ratio[(size_t) k] = dsp::semisToRatio ((float) (k + 1) * v[pitchStep]);
                level[(size_t) k] = std::pow (v[attenuation], (float) k);
                lpCoef[(size_t) k] = dsp::onePoleCoefHz (std::min (20000.0f * std::pow (1.0f - 0.95f * v[darken], (float) k), 0.45f * sr), sr);
            }

            // The Bateman equations, one Euler step per block (lambda dt stays under 0.05), and the spawns they cause.
            const float dt = (float) n / sr;
            const float rateScale = v[activity] * 50.0f;
            std::array<float, generationsMax> total {};
            for (int d = 0; d < maxDeposits; ++d)
            {
                auto& dep = deposits[(size_t) d];
                if (! dep.active)
                    continue;
                std::array<float, generationsMax> decays {};
                for (int k = 0; k < daughtersNow; ++k)
                    decays[(size_t) k] = lambda[(size_t) k] * dep.n[(size_t) k] * dt;
                for (int k = 0; k < daughtersNow; ++k)
                {
                    dep.n[(size_t) k] -= decays[(size_t) k];
                    if (k + 1 < daughtersNow)
                        dep.n[(size_t) k + 1] += decays[(size_t) k];
                    // Particles: activity x 50 per unit decayed, spread over the block by hash01 rolls.
                    const float expected = rateScale * decays[(size_t) k];
                    total[(size_t) k] += lambda[(size_t) k] * dep.n[(size_t) k];
                    const int whole = (int) expected;
                    const float frac = expected - (float) whole;
                    const int count = whole + (dsp::hash01 (blocks, d * 8 + k, 0x5A2Cu, callsign) < frac ? 1 : 0);
                    for (int c = 0; c < count; ++c)
                        spawn (d, k, c);
                }
                float left = 0.0f;
                for (int k = 0; k < daughtersNow; ++k)
                    left += dep.n[(size_t) k];
                if (left < 1.0e-4f * dep.initial || ++dep.age > (int64_t) (10.0f * baseHalfLives[3] * std::max (0.05f, v[halfLife]) * sr / (float) n))
                    dep.active = false;
            }
            for (int k = 0; k < generationsMax; ++k)
                uiActivity[(size_t) k].store (total[(size_t) k]);
            ++blocks;
        }

        void process (float* l, float* r, const float*, const float*, int n, host::Guard& guard) override
        {
            for (int i = 0; i < n; ++i)
            {
                // The hit itself: a new deposit captures 250 ms from 2 ms before the onset.
                if (detector.process (l[i], r[i]))
                    deposit();
                for (auto& dep : deposits)
                    if (dep.active && dep.captured < captureLength)
                    {
                        dep.l[(size_t) dep.captured] = l[i];
                        dep.r[(size_t) dep.captured] = r[i];
                        ++dep.captured;
                    }
                preL[(size_t) prePos] = l[i];
                preR[(size_t) prePos] = r[i];
                prePos = (prePos + 1) % (int) preL.size();

                // The particles. Grains of one hit start within its first 20 ms at one pitch, so a dense burst adds up
                // nearly in phase: the bus is scaled by the number sounding (1 / sqrt (n / 2), smoothed over 2 ms) and
                // soft-limited at 0 dBFS, and a burst crackles instead of piling up (it reached +27 dB over the hit).
                float wl = 0.0f, wr = 0.0f;
                int sounding = 0;
                for (auto& g : grains)
                {
                    if (! g.active)
                        continue;
                    const auto& dep = deposits[(size_t) g.deposit];
                    const int k = (int) g.pos;
                    if (g.age >= g.length || k + 1 >= dep.captured || dep.serial != g.serial)
                    {
                        g.active = false;
                        continue;
                    }
                    ++sounding;
                    const float f = (float) (g.pos - (double) k);
                    const float w = dsp::sin2pi (0.5f * (float) g.age / (float) g.length);
                    const float a = (dep.l[(size_t) k] + f * (dep.l[(size_t) k + 1] - dep.l[(size_t) k])) * w * w * g.gain;
                    const float b = (dep.r[(size_t) k] + f * (dep.r[(size_t) k + 1] - dep.r[(size_t) k])) * w * w * g.gain;
                    g.lpL += (a - g.lpL) * lpCoef[(size_t) g.generation];
                    g.lpR += (b - g.lpR) * lpCoef[(size_t) g.generation];
                    wl += g.lpL * g.panL;
                    wr += g.lpR * g.panR;
                    g.pos += g.rate;
                    ++g.age;
                }
                density += (1.0f / std::sqrt (std::max (1.0f, 0.5f * (float) sounding)) - density) * densityCoef;
                l[i] += dsp::fastTanh (wl * density);
                r[i] += dsp::fastTanh (wr * density);
            }
            guard (0, l, r, n);
        }

        void meter (host::Meters& m) const override
        {
            for (int k = 0; k < generationsMax; ++k)
                m.custom[(size_t) k].store ((float) spawned[(size_t) k]);   // particles per generation since prepare
            int active = 0;
            for (const auto& d : deposits)
                active += d.active ? 1 : 0;
            m.custom[4].store ((float) active);
        }

    private:
        struct Deposit
        {
            std::vector<float> l, r;
            std::array<float, generationsMax> n {};
            float initial = 0.0f;
            int captured = 0;
            int64_t age = 0, serial = 0, born = 0;
            bool active = false;
        };

        struct Grain
        {
            int deposit = 0, generation = 0, length = 0, age = 0;
            int64_t serial = 0;
            double pos = 0.0, rate = 1.0;
            float gain = 0.0f, panL = 1.0f, panR = 1.0f, lpL = 0.0f, lpR = 0.0f;
            bool active = false;
        };

        void deposit() noexcept
        {
            // The oldest deposit gives way when all are in use.
            Deposit* slot = &deposits[0];
            for (auto& d : deposits)
            {
                if (! d.active) { slot = &d; break; }
                if (d.born < slot->born) slot = &d;
            }
            const float e = std::clamp (detector.peak() * 2.0f, 0.05f, 1.0f);
            slot->n.fill (0.0f);
            slot->n[0] = slot->initial = e * doseNow;
            slot->age = 0;
            slot->serial = ++serials;
            slot->born = serials;
            slot->active = doseNow > 1.0e-4f;
            // The pre-roll from the running ring, then the capture continues sample by sample.
            const int pre = std::min (preRoll, (int) preL.size());
            for (int k = 0; k < pre; ++k)
            {
                const int idx = (prePos - pre + k + (int) preL.size()) % (int) preL.size();
                slot->l[(size_t) k] = preL[(size_t) idx];
                slot->r[(size_t) k] = preR[(size_t) idx];
            }
            slot->captured = pre;
        }

        void spawn (int d, int generation, int c) noexcept
        {
            Grain* slot = &grains[0];
            for (auto& g : grains)
            {
                if (! g.active) { slot = &g; break; }
                if (g.age > slot->age) slot = &g;
            }
            const auto& dep = deposits[(size_t) d];
            const float jitter = dsp::hash01 (blocks, d * 64 + generation * 8 + c, 0x6A1Eu, callsign);
            const float pan = (dsp::hash01 (blocks, d * 64 + generation * 8 + c, 0x9A11u, callsign) * 2.0f - 1.0f) * spreadNow;
            slot->deposit = d;
            slot->generation = generation;
            slot->serial = dep.serial;
            slot->rate = ratio[(size_t) generation];
            slot->pos = (double) (jitter * 0.02f * sr);   // the hit, up to 20 ms in
            slot->length = std::max (16, (int) ((float) grainSamples / std::max (1.0f, (float) slot->rate)));
            slot->age = 0;
            slot->gain = std::max (0.05f, dep.initial / std::max (doseNow, 1.0e-4f)) * level[(size_t) generation] * 0.7f;
            dsp::panGains (pan, slot->panL, slot->panR);
            slot->lpL = slot->lpR = 0.0f;
            slot->active = true;
            ++spawned[(size_t) generation];
        }

        std::array<Deposit, maxDeposits> deposits;
        std::array<Grain, maxGrains> grains {};
        std::array<float, 128> preL {}, preR {};
        std::array<float, generationsMax> lambda {}, ratio {}, level {}, lpCoef {};
        std::array<int64_t, generationsMax> spawned {};
        dsp::OnsetDetector detector;
        float sr = 48000.0f, doseNow = 0.6f, spreadNow = 0.7f, density = 1.0f, densityCoef = 0.01f;
        int daughtersNow = 3, grainSamples = 2880, captureLength = 12000, preRoll = 96, prePos = 0;
        int64_t blocks = 0, serials = 0;
        uint32_t callsign = 0;
    };

    class SarcophagusProcessor final : public host::EffectProcessor
    {
    public:
        SarcophagusProcessor() : EffectProcessor (effectInfo(), std::make_unique<SarcophagusChain>()) {}
        SarcophagusChain& reactor() { return static_cast<SarcophagusChain&> (getChain()); }
        juce::AudioProcessorEditor* createEditor() override;
    };

    class SarcophagusEditor final : public ui::EffectEditor
    {
    public:
        explicit SarcophagusEditor (SarcophagusProcessor& p) : EffectEditor (p), fx (p)
        {
            chainBars = own (std::make_unique<ui::BarsView>());
            chainBars->setCaption ("ACTIVITY BY GENERATION");
            chainBars->setLabels ({ "PARENT", "D1", "D2", "D3" });
            needle = own (std::make_unique<ui::Needle> ("DOSE RATE", 0.8f));
            addRow ({ panel ("REACTOR // РЕАКТОР", { { chainBars, 3.2f }, { needle, 1.8f } }, true),
                      panel ("DEPOSIT // ОТЛОЖЕНИЕ", { { knob ("dose", "DOSE", true), 1.0f }, { knob ("sensitivity", "SENSE"), 1.0f } }) });
            addRow ({ panel ("DECAY // РАСПАД", { { knob ("half_life", "HALF-LIFE"), 1.0f }, { knob ("daughters", "DAUGHTERS"), 1.0f },
                                                 { knob ("activity", "ACTIVITY"), 1.0f } }),
                      panel ("PARTICLES // ЧАСТИЦЫ", { { knob ("grain", "GRAIN"), 1.0f }, { knob ("pitch_step", "PITCH"), 1.0f },
                                                      { knob ("attenuation", "ATTEN"), 1.0f }, { knob ("darken", "DARKEN"), 1.0f },
                                                      { knob ("spread", "SPREAD"), 1.0f } }) });
            addRow ({ outputPanel() });
            finish();
        }

    private:
        void refresh (float seconds) override
        {
            auto& c = fx.reactor();
            std::array<float, generationsMax> a {};
            float top = 1.0e-6f, sum = 0.0f;
            for (int k = 0; k < generationsMax; ++k)
            {
                a[(size_t) k] = c.uiActivity[(size_t) k].load();
                top = std::max (top, a[(size_t) k]);
                sum += a[(size_t) k];
            }
            for (auto& x : a)
                x /= top;
            chainBars->setValues (a.data(), generationsMax);
            needle->setReadout (juce::String (sum, 2) + " Bq");
            needle->advance (std::min (1.0f, sum * 0.5f), seconds, std::min (1.0f, sum));
        }

        SarcophagusProcessor& fx;
        ui::BarsView* chainBars = nullptr;
        ui::Needle* needle = nullptr;
    };

    juce::AudioProcessorEditor* SarcophagusProcessor::createEditor() { return new SarcophagusEditor (*this); }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SarcophagusProcessor();
}
