// WISH GRANTER // ИСПОЛНИТЕЛЬ ЖЕЛАНИЙ — X-19 inside the plant: C-Consciousness, many minds as one, and the Wish
// Granter hologram before the Monolith. The shimmer reverb with its intervals taken from chord shapes, a FREEZE that
// holds the moment, and GRANT, which gives a little of everything.

#include "core/host/EffectProcessor.h"
#include "core/dsp/Fdn.h"
#include "core/dsp/Filters.h"
#include "ui/EffectEditor.h"

namespace
{
    using namespace lab;
    using host::Kind;
    using host::Smooth;

    enum Param : int { size = 0, decay, damping, predelay, shimmer, interval, freeze, brightness, grant };

    // Shimmer head ratios per interval; heads past the interval's count stay silent.
    constexpr float intervalRatios[7][3] = {
        { 2.0f, 2.0f, 2.0f },      // Octave
        { 1.5f, 1.5f, 1.5f },      // Fifth
        { 2.0f, 1.5f, 1.5f },      // Octave + Fifth
        { 1.26f, 1.5f, 2.0f },     // Major
        { 1.19f, 1.5f, 2.0f },     // Minor
        { 1.12f, 1.5f, 2.0f },     // Sus2
        { 1.26f, 1.5f, 1.68f },    // Lydian
    };
    constexpr int intervalHeads[7] = { 1, 1, 2, 3, 3, 3, 3 };

    float rt60For (float decayValue) noexcept { return 0.3f * std::exp2 (std::clamp (decayValue, 0.0f, 1.0f) * 6.0f); }   // 0.3 .. 19.2 s

    const host::EffectInfo& effectInfo()
    {
        static const host::EffectInfo i = []
        {
            host::EffectInfo e;
            e.product = LABX16_PRODUCT;
            e.subtitle = "ИСПОЛНИТЕЛЬ ЖЕЛАНИЙ";
            e.slug = LABX16_SLUG;
            e.version = LABX16_VERSION;
            e.code = LABX16_CODE;
            e.faction = host::Faction::etherealSignal;
            e.mixLaw = host::MixLaw::equalPower;
            e.params = {
                { "size",             "Size",       Kind::real,   0.0f,  1.0f,   0.7f,  0.0f,  Smooth::linear, "%",  "" },
                { "decay",            "Decay",      Kind::real,   0.0f,  1.0f,   0.6f,  0.0f,  Smooth::linear, "%",  "" },
                { "damping",          "Damping",    Kind::real,   0.0f,  1.0f,   0.35f, 0.0f,  Smooth::linear, "%",  "" },
                { "predelay",         "Pre-delay",  Kind::real,   0.0f,  250.0f, 20.0f, 60.0f, Smooth::linear, "ms", "" },
                { "shimmer",          "Shimmer",    Kind::real,   0.0f,  1.0f,   0.3f,  0.0f,  Smooth::linear, "%",  "" },
                { "shimmer_interval", "Interval",   Kind::choice, 0.0f,  6.0f,   0.0f,  0.0f,  Smooth::none,   "",   "Octave|Fifth|Octave+Fifth|Major|Minor|Sus2|Lydian" },
                { "freeze",           "Freeze",     Kind::choice, 0.0f,  1.0f,   0.0f,  0.0f,  Smooth::none,   "",   "Off|On" },
                { "brightness",       "Brightness", Kind::real,   -1.0f, 1.0f,   0.0f,  0.0f,  Smooth::linear, "%",  "" },
                { "grant",            "Grant",      Kind::real,   0.0f,  1.0f,   0.0f,  0.0f,  Smooth::linear, "%",  "" },
            };
            e.stages = { "fdn" };
            e.presets = {
                { "Init", {} },
                { "Sarcophagus Hall", { { "mix", 0.3f }, { "size", 0.85f }, { "decay", 0.7f }, { "damping", 0.45f }, { "shimmer", 0.15f },
                                        { "predelay", 35.0f }, { "output", 0.7f } } },
                { "Monolith Glow", { { "mix", 0.3f }, { "shimmer_interval", 1.0f }, { "shimmer", 0.45f }, { "decay", 0.72f },
                                     { "brightness", 0.2f }, { "output", 0.8f } } },
                { "Many Minds", { { "mix", 0.3f }, { "shimmer_interval", 3.0f }, { "shimmer", 0.5f }, { "size", 0.8f }, { "decay", 0.75f }, { "output", 0.7f } } },
                { "Granted", { { "mix", 0.3f }, { "grant", 1.0f }, { "shimmer_interval", 2.0f }, { "output", 2.0f } } },
                { "Frozen Wish", { { "mix", 0.3f }, { "freeze", 1.0f }, { "shimmer", 0.35f }, { "size", 0.9f }, { "decay", 0.8f }, { "output", 1.0f } } },
                { "Octave Choir", { { "mix", 0.3f }, { "shimmer", 0.7f }, { "decay", 0.68f }, { "damping", 0.25f }, { "brightness", 0.15f }, { "output", 0.8f } } },
            };
            return e;
        }();
        return i;
    }

    class WishGranterChain final : public host::Chain
    {
    public:
        void prepare (double sampleRate, int) override
        {
            sr = (float) sampleRate;
            fdn.prepare (sr, 2.6f, 0.26f);
            for (auto& t : tilt)
                t.prepare (1500.0f, sr);
            glowCoef = dsp::onePoleCoef (0.3f, sr);
            reset();
        }

        void reset() override
        {
            fdn.reset();
            for (auto& t : tilt)
                t.reset();
            glow = 0.0f;
        }

        void flush() override
        {
            fdn.flush();
            reset();
        }

        double tailSeconds (const float* v) const override
        {
            if (v[freeze] > 0.5f)
                return std::numeric_limits<double>::infinity();   // frozen: the tail holds as long as FREEZE does
            const double rt60 = rt60For (v[decay]);
            const double shim = std::clamp (v[shimmer] + 0.4f * v[grant], 0.0f, 1.0f);
            return std::min (60.0, rt60 * (1.6 + 0.6 * shim) + (double) v[predelay] * 0.001 + 0.5);
        }

        void control (const float* v, const host::BlockContext& ctx, int) override
        {
            const float g = v[grant];
            settings.scale = 0.3f + 2.2f * std::clamp (v[size] + 0.1f * g, 0.0f, 1.0f);
            settings.rt60 = rt60For (v[decay]);
            settings.dampHz = 9000.0f * std::exp2 (-std::clamp (v[damping] - 0.2f * g, 0.0f, 1.0f) * 2.6f);
            settings.predelaySeconds = v[predelay] * 0.001f;
            const int iv = std::clamp ((int) std::lround (v[interval]), 0, 6);
            settings.heads = intervalHeads[iv];
            for (int k = 0; k < 3; ++k)
                settings.ratios[k] = intervalRatios[iv][k];
            settings.shimmer = std::clamp (v[shimmer] + 0.4f * g, 0.0f, 1.0f);
            settings.freeze = v[freeze] > 0.5f;
            brightnessNow = std::clamp (v[brightness] + 0.4f * g, -1.0f, 1.0f);
            if (ctx.mixOffset != nullptr)
                *ctx.mixOffset = 0.15f * g;
        }

        void process (float* l, float* r, const float*, const float*, int n, host::Guard& guard) override
        {
            fdn.process (l, r, n, settings);
            const float tone = brightnessNow * 0.7f;
            for (int i = 0; i < n; ++i)
            {
                l[i] = tilt[0].process (l[i], tone);
                r[i] = tilt[1].process (r[i], tone);
            }
            guard (0, l, r, n);
            const float peak = dsp::blockPeak (l, r, n);
            if (std::isfinite (peak))
                glow += (peak - glow) * (1.0f - std::pow (1.0f - glowCoef, (float) n));
        }

        void meter (host::Meters& m) const override
        {
            m.custom[0].store (settings.rt60);
            m.custom[1].store (fdn.frozenAmount());
            m.custom[2].store (dsp::gainToDb (std::max (glow, 1.0e-5f)));
        }

    private:
        dsp::Fdn fdn;
        dsp::Fdn::Settings settings;
        std::array<dsp::Tilt, 2> tilt;
        float sr = 48000.0f, brightnessNow = 0.0f, glow = 0.0f, glowCoef = 0.0001f;
    };

    class WishGranterProcessor final : public host::EffectProcessor
    {
    public:
        WishGranterProcessor() : EffectProcessor (effectInfo(), std::make_unique<WishGranterChain>()) {}
        juce::AudioProcessorEditor* createEditor() override;
    };

    class WishGranterEditor final : public ui::EffectEditor
    {
    public:
        explicit WishGranterEditor (WishGranterProcessor& p) : EffectEditor (p)
        {
            needle = own (std::make_unique<ui::Needle> ("GLOW", 0.85f));
            addRow ({ panel ("HALL // ЗАЛ", { { knob ("size", "SIZE"), 1.0f }, { knob ("decay", "DECAY"), 1.0f },
                                             { knob ("damping", "DAMPING"), 1.0f }, { knob ("predelay", "PRE-DELAY"), 1.0f } }),
                      panel ("SHIMMER // МЕРЦАНИЕ", { { knob ("shimmer", "SHIMMER"), 1.0f }, { choices ({ { "shimmer_interval", "INTERVAL" } }), 1.4f } }) });
            addRow ({ panel ("MONOLITH // МОНОЛИТ", { { needle, 1.9f }, { knob ("grant", "GRANT", true), 1.0f } }, true),
                      panel ("FREEZE // ЗАМОРОЗКА", { { choices ({ { "freeze", "FREEZE" } }), 1.2f } }),
                      panel ("TONE // ТОН", { { knob ("brightness", "BRIGHTNESS"), 1.0f } }),
                      outputPanel() });
            finish();
        }

    private:
        void refresh (float seconds) override
        {
            const auto& m = processor.meters;
            const float db = m.custom[2].load();
            const bool frozen = m.custom[1].load() > 0.5f;
            needle->setReadout (frozen ? juce::String ("FROZEN") : "RT60 " + juce::String (m.custom[0].load(), 1) + " s");
            needle->advance ((db + 60.0f) / 60.0f, seconds);
        }

        ui::Needle* needle = nullptr;
    };

    juce::AudioProcessorEditor* WishGranterProcessor::createEditor() { return new WishGranterEditor (*this); }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new WishGranterProcessor();
}
