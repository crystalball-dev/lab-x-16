// GAUSS // ГАУСС — electromagnetic acceleration: a capacitor bank charges while nothing happens and dumps on the next
// trigger. The Gauss rifle, the Monolith sniper on the antenna. The opposite of a compressor: the longer the silence
// before a hit, the harder the hit lands. A coil whine rises with the charge; a rail flash fires at discharge. The
// 64-sample look-ahead puts the boost on the true attack.

#include "core/host/EffectProcessor.h"
#include "core/dsp/Filters.h"
#include "core/dsp/Follower.h"
#include "core/dsp/Onset.h"
#include "ui/EffectEditor.h"

namespace
{
    using namespace lab;
    using host::Kind;
    using host::Smooth;

    enum Param : int { power = 0, recharge, quietThr, pulse, residual, whine, flash, sensitivity };
    constexpr int lookahead = 64;

    const host::EffectInfo& effectInfo()
    {
        static const host::EffectInfo i = []
        {
            host::EffectInfo e;
            e.product = LABX16_PRODUCT;
            e.subtitle = "ГАУСС";
            e.slug = LABX16_SLUG;
            e.version = LABX16_VERSION;
            e.code = LABX16_CODE;
            e.faction = host::Faction::deepZone;
            e.params = {
                { "power",       "Power",       Kind::real, 0.0f,   24.0f,  12.0f,  0.0f,  Smooth::linear, "dB", "" },
                { "recharge",    "Recharge",    Kind::real, 0.1f,   10.0f,  2.0f,   1.5f,  Smooth::linear, "s",  "" },
                { "quiet_thr",   "Quiet",       Kind::real, -80.0f, -20.0f, -50.0f, 0.0f,  Smooth::linear, "dB", "" },
                { "pulse",       "Pulse",       Kind::real, 5.0f,   60.0f,  20.0f,  20.0f, Smooth::linear, "ms", "" },
                { "residual",    "Residual",    Kind::real, 0.0f,   0.5f,   0.1f,   0.0f,  Smooth::linear, "%",  "" },
                { "whine",       "Coil Whine",  Kind::real, 0.0f,   1.0f,   0.2f,   0.0f,  Smooth::linear, "%",  "" },
                { "flash",       "Rail Flash",  Kind::real, 0.0f,   1.0f,   0.2f,   0.0f,  Smooth::linear, "%",  "" },
                { "sensitivity", "Sensitivity", Kind::real, 0.0f,   1.0f,   0.5f,   0.0f,  Smooth::linear, "%",  "" },
            };
            e.stages = { "rail" };
            e.presets = {
                { "Init", {} },
                { "Sniper's Charge", { { "power", 15.0f }, { "recharge", 3.0f }, { "quiet_thr", -40.0f }, { "whine", 0.3f }, { "output", -0.2f } } },
                { "Full Rail", { { "power", 24.0f }, { "recharge", 4.0f }, { "flash", 0.5f }, { "whine", 0.25f }, { "output", -0.1f } } },
                { "Coil Whine", { { "whine", 0.8f }, { "power", 9.0f }, { "output", 0.0f } } },
                { "Quick Recharge", { { "recharge", 0.2f }, { "quiet_thr", -30.0f }, { "power", 9.0f }, { "whine", 0.0f }, { "output", -3.4f } } },
                { "Flash Only", { { "power", 0.0f }, { "flash", 0.9f }, { "whine", 0.0f }, { "output", 0.0f } } },
                { "Heavy Projectile", { { "pulse", 60.0f }, { "power", 14.0f }, { "residual", 0.25f }, { "output", -0.1f } } },
            };
            return e;
        }();
        return i;
    }

    class GaussChain final : public host::Chain
    {
    public:
        void prepare (double sampleRate, int) override
        {
            sr = (float) sampleRate;
            detector.prepare (sr);
            quietFollower.set (0.005f, 0.1f, sr);
            flashHp.setHz (4000.0f, 0.7071f, sr);
            ramp = std::max (1, (int) (0.001f * sr));
            hold = std::max (1, (int) (0.002f * sr));
            flashLength = std::max (1, (int) (0.008f * sr));
            rng.seed (0x6A055u);
            reset();
        }

        void reset() override
        {
            ring.fill ({});
            write = 0;
            detector.reset();
            quietFollower.reset();
            flashHp.reset();
            charge = 0.0f;
            shotAt = std::numeric_limits<int64_t>::min() / 4;
            shotDb = 0.0f;
            flashLevel = 0.0f;
            whinePhase = 0.0f;
        }

        int latency (double) const override { return lookahead; }
        double tailSeconds (const float* v) const override { return (double) v[pulse] * 0.006 + 0.01; }

        void control (const float* v, const host::BlockContext&, int) override
        {
            powerDb = v[power];
            rechargeStep = 1.0f / (std::max (0.05f, v[recharge]) * sr);
            quietLevel = dsp::dbToGain (v[quietThr]);
            pulseSamples = v[pulse] * 0.001f * sr;
            residualNow = v[residual];
            whineGain = v[whine] * 0.1f;
            flashGain = v[flash];
            detector.setSensitivity (v[sensitivity]);
        }

        void process (float* l, float* r, const float*, const float*, int n, host::Guard& guard) override
        {
            for (int i = 0; i < n; ++i)
            {
                ring[(size_t) (write & (ringSize - 1))] = { l[i], r[i] };
                const int64_t w = write++;

                // The charge builds while the input stays under QUIET; an onset fires it.
                const float env = quietFollower.processStereo (l[i], r[i]);
                if (env < quietLevel)
                    charge = std::min (1.0f, charge + rechargeStep);
                if (detector.process (l[i], r[i]))
                {
                    shotAt = w;
                    shotDb = powerDb * charge;
                    flashLevel = flashGain * charge * std::clamp (detector.peak() * 3.0f, 0.1f, 1.0f);
                    charge *= residualNow;
                    ++shots;
                }

                // The output, 64 samples back: the boost rises over 1 ms before the hit, holds 2 ms, then decays.
                const int64_t u = w - lookahead;
                const auto& x = ring[(size_t) (u & (ringSize - 1))];
                const int64_t k = u - shotAt;
                float boost = 0.0f;
                if (k >= -ramp)
                {
                    if (k < 0)
                        boost = shotDb * (float) (k + ramp) / (float) ramp;
                    else if (k < hold)
                        boost = shotDb;
                    else
                        boost = shotDb * std::exp (-(float) (k - hold) / std::max (1.0f, pulseSamples));
                }
                const float g = boost > 1.0e-4f ? dsp::dbToGain (boost) : 1.0f;
                float extra = 0.0f;
                if (whineGain > 0.0f && charge > 1.0e-3f)
                {
                    whinePhase = dsp::wrap01 (whinePhase + (2000.0f + 4000.0f * charge) / sr);
                    extra += dsp::sin2pi (whinePhase) * whineGain * charge * charge;
                }
                if (k >= 0 && k < flashLength && flashLevel > 0.0f)
                    extra += flashHp.highpass (rng.bi()) * flashLevel * (1.0f - (float) k / (float) flashLength);
                l[i] = x.l * g + extra;
                r[i] = x.r * g + extra;
                lastBoost = std::max (lastBoost * 0.9999f, boost);
            }
            guard (0, l, r, n);
        }

        void meter (host::Meters& m) const override
        {
            m.custom[0].store (charge);
            m.custom[1].store (lastBoost);
            m.custom[2].store ((float) shots);
        }

    private:
        struct Frame { float l = 0.0f, r = 0.0f; };
        static constexpr int ringSize = 128;

        std::array<Frame, ringSize> ring {};
        dsp::OnsetDetector detector;
        dsp::Follower quietFollower;
        dsp::SVF flashHp;
        dsp::Rng rng;
        int64_t write = 0, shotAt = 0, shots = 0;
        int ramp = 48, hold = 96, flashLength = 384;
        float sr = 48000.0f, charge = 0.0f, shotDb = 0.0f, flashLevel = 0.0f, whinePhase = 0.0f, lastBoost = 0.0f;
        float powerDb = 12.0f, rechargeStep = 0.0f, quietLevel = 0.003f, pulseSamples = 960.0f, residualNow = 0.1f, whineGain = 0.02f, flashGain = 0.2f;
    };

    class GaussProcessor final : public host::EffectProcessor
    {
    public:
        GaussProcessor() : EffectProcessor (effectInfo(), std::make_unique<GaussChain>()) {}
        juce::AudioProcessorEditor* createEditor() override;
    };

    class GaussEditor final : public ui::EffectEditor
    {
    public:
        explicit GaussEditor (GaussProcessor& p) : EffectEditor (p)
        {
            needle = own (std::make_unique<ui::Needle> ("CHARGE", 0.9f));
            addRow ({ panel ("CAPACITOR // КОНДЕНСАТОР", { { needle, 1.8f }, { knob ("recharge", "RECHARGE"), 1.0f },
                                                          { knob ("quiet_thr", "QUIET"), 1.0f } }, true),
                      panel ("RAIL // РЕЛЬС", { { knob ("power", "POWER", true), 1.0f }, { knob ("pulse", "PULSE"), 1.0f },
                                               { knob ("residual", "RESIDUAL"), 1.0f } }) });
            addRow ({ panel ("COIL // КАТУШКА", { { knob ("whine", "WHINE"), 1.0f }, { knob ("flash", "FLASH"), 1.0f } }),
                      panel ("TRIGGER // СПУСК", { { knob ("sensitivity", "SENSE"), 1.0f } }),
                      outputPanel() });
            finish();
        }

    private:
        void refresh (float seconds) override
        {
            const auto& m = processor.meters;
            const float c = m.custom[0].load();
            needle->setReadout (juce::String (juce::roundToInt (c * 100.0f)) + "%  +" + juce::String (m.custom[1].load(), 1) + " dB");
            needle->advance (c, seconds, 0.5f * c * c);
        }

        ui::Needle* needle = nullptr;
    };

    juce::AudioProcessorEditor* GaussProcessor::createEditor() { return new GaussEditor (*this); }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new GaussProcessor();
}
