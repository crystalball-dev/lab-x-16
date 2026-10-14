// SILO // ШАХТА — X-16's circular, silo-shaped emitter chamber with a switch on every level, and the drainage tunnels
// under the factory. The NOOSPHERE network at small size with a modal blend that makes the room ring, LEVELS that step
// the geometry (a staircase, not a slider), a wet ducked by the input, and TUNNEL, narrow band-passed multi-taps.

#include "core/host/EffectProcessor.h"
#include "core/dsp/Fdn.h"
#include "core/dsp/Follower.h"
#include "ui/EffectEditor.h"

namespace
{
    using namespace lab;
    using host::Kind;
    using host::Smooth;

    enum Param : int { levels = 0, size, decay, damping, modal, predelay, duck, duckRelease, tunnel, tunnelLength,
                       tunnelFeedback, claustrophobia };

    const host::EffectInfo& effectInfo()
    {
        static const host::EffectInfo i = []
        {
            host::EffectInfo e;
            e.product = LABX16_PRODUCT;
            e.subtitle = "ШАХТА";
            e.slug = LABX16_SLUG;
            e.version = LABX16_VERSION;
            e.code = LABX16_CODE;
            e.faction = host::Faction::deepZone;
            e.mixLaw = host::MixLaw::equalPower;
            e.params = {
                { "levels",          "Level",           Kind::integer, 1.0f,  5.0f,    3.0f,   0.0f,   Smooth::none,   "",   "" },
                { "size",            "Size",            Kind::real,    0.0f,  1.0f,    0.3f,   0.0f,   Smooth::linear, "%",  "" },
                { "decay",           "Decay",           Kind::real,    0.1f,  6.0f,    1.2f,   1.0f,   Smooth::log,    "s",  "" },
                { "damping",         "Damping",         Kind::real,    0.0f,  1.0f,    0.5f,   0.0f,   Smooth::linear, "%",  "" },
                { "modal",           "Modal",           Kind::real,    0.0f,  1.0f,    0.3f,   0.0f,   Smooth::linear, "%",  "" },
                { "predelay",        "Pre-delay",       Kind::real,    0.0f,  30.0f,   4.0f,   0.0f,   Smooth::linear, "ms", "" },
                { "duck",            "Duck",            Kind::real,    0.0f,  1.0f,    0.4f,   0.0f,   Smooth::linear, "%",  "" },
                { "duck_release",    "Duck Release",    Kind::real,    20.0f, 1000.0f, 180.0f, 150.0f, Smooth::log,    "ms", "" },
                { "tunnel",          "Tunnel",          Kind::choice,  0.0f,  1.0f,    0.0f,   0.0f,   Smooth::none,   "",   "Off|On" },
                { "tunnel_length",   "Tunnel Length",   Kind::real,    0.5f,  2.0f,    1.0f,   1.0f,   Smooth::log,    "x",  "" },
                { "tunnel_feedback", "Tunnel Feedback", Kind::real,    0.0f,  0.8f,    0.5f,   0.0f,   Smooth::linear, "%",  "" },
                { "claustrophobia",  "Claustrophobia",  Kind::real,    0.0f,  1.0f,    0.0f,   0.0f,   Smooth::linear, "%",  "" },
            };
            e.stages = { "fdn" };
            e.presets = {
                { "Init", {} },
                { "Emitter Chamber", { { "mix", 0.3f }, { "levels", 4.0f }, { "size", 0.6f }, { "decay", 2.2f }, { "modal", 0.45f },
                                       { "damping", 0.35f }, { "duck", 0.2f }, { "output", 0.9f } } },
                { "Level Three Switch", { { "mix", 0.3f }, { "levels", 3.0f }, { "size", 0.4f }, { "decay", 1.0f }, { "modal", 0.25f }, { "output", 1.0f } } },
                { "Controller's Storage", { { "mix", 0.25f }, { "levels", 1.0f }, { "size", 0.15f }, { "decay", 0.7f }, { "modal", 0.85f },
                                            { "damping", 0.6f }, { "duck", 0.3f }, { "output", 0.8f } } },
                { "Drainage Tunnel", { { "mix", 0.3f }, { "tunnel", 1.0f }, { "tunnel_length", 1.25f }, { "tunnel_feedback", 0.55f },
                                       { "duck", 0.25f }, { "output", 0.9f } } },
                { "Elevator Shaft", { { "mix", 0.3f }, { "levels", 5.0f }, { "size", 1.0f }, { "decay", 4.5f }, { "modal", 0.2f },
                                      { "damping", 0.4f }, { "predelay", 12.0f }, { "output", 0.9f } } },
                { "Airlock", { { "mix", 0.35f }, { "levels", 2.0f }, { "size", 0.5f }, { "decay", 1.6f }, { "duck", 1.0f },
                               { "duck_release", 260.0f }, { "output", 1.4f } } },
            };
            return e;
        }();
        return i;
    }

    class SiloChain final : public host::Chain
    {
    public:
        static constexpr float levelScale[5] = { 0.35f, 0.5f, 0.65f, 0.82f, 1.0f };   // of the small set (NOOSPHERE x 0.3)
        static constexpr float tunnelMs[6] = { 23.0f, 41.0f, 67.0f, 89.0f, 113.0f, 151.0f };
        static constexpr float tunnelLevel[6] = { 1.0f, 0.86f, 0.74f, 0.64f, 0.55f, 0.47f };

        void prepare (double sampleRate, int) override
        {
            sr = (float) sampleRate;
            fdn.prepare (sr, 0.4f, 0.035f);
            tunnelLine.prepare ((int) std::ceil (0.151f * 2.0f * sr) + 8);
            for (auto& b : bandHp)
                b.setHz (300.0f, 0.7071f, sr);
            for (auto& b : bandLp)
                b.setHz (3000.0f, 0.7071f, sr);
            for (int k = 0; k < 6; ++k)
                dsp::panGains ((k & 1) == 0 ? -0.35f : 0.35f, tapPanL[(size_t) k], tapPanR[(size_t) k]);
            attackCoef = dsp::onePoleCoef (0.010f, sr);
            reset();
            fresh = true;
        }

        void reset() override
        {
            fdn.reset();
            tunnelLine.reset();
            for (auto& b : bandHp) b.reset();
            for (auto& b : bandLp) b.reset();
            duckEnv = 0.0f;
            duckGain = 1.0f;
            lengthNow = -1.0f;
        }

        // A LEVEL or TUNNEL change (the base has faded the effect out): the new chamber starts empty, at once.
        void flush() override
        {
            appliedLevel = wantedLevel;
            tunnelOn = wantedTunnel;
            fdn.flush();
            reset();
        }

        double tailSeconds (const float* v) const override
        {
            if (v[tunnel] > 0.5f)
            {
                const float loop = 0.151f * v[tunnelLength];
                const float fb = std::clamp (v[tunnelFeedback], 0.0f, 0.8f);
                const float repeats = fb > 0.01f ? 60.0f / -dsp::gainToDb (fb) : 0.0f;
                return std::min (30.0, (double) (loop * (repeats + 1.0f) + 0.1f));
            }
            return (double) v[decay] * 1.2 + (double) v[predelay] * 0.001 + 0.1;
        }

        void control (const float* v, const host::BlockContext& ctx, int n) override
        {
            const float c = v[claustrophobia];
            // LEVEL and TUNNEL fade through the base's switch; the first block after prepare takes them directly.
            wantedLevel = std::clamp ((int) std::lround (v[levels]), 1, 5);
            wantedTunnel = v[tunnel] > 0.5f;
            if (fresh)
            {
                appliedLevel = wantedLevel;
                tunnelOn = wantedTunnel;
            }
            else if ((wantedLevel != appliedLevel || wantedTunnel != tunnelOn) && ctx.requestSwitch != nullptr)
                *ctx.requestSwitch = true;
            fresh = false;

            const float sizeNow = v[size] * (1.0f - 0.8f * c);
            settings.scale = 0.3f * levelScale[appliedLevel - 1] * (0.8f + 0.4f * sizeNow);
            settings.rt60 = v[decay];
            const float damp = std::clamp (v[damping] + 0.4f * c, 0.0f, 1.0f);
            settings.dampHz = 1500.0f * std::exp2 ((1.0f - damp) * 4.0f);   // 1.5 kHz .. 24 kHz (none)
            settings.modal = std::clamp (v[modal] + 0.5f * c, 0.0f, 1.0f);
            settings.modulation = 0.25f * (1.0f - settings.modal);          // a ringing room barely moves
            settings.predelaySeconds = v[predelay] * 0.001f;

            duckAmount = std::clamp (v[duck] + 0.5f * c, 0.0f, 1.0f);
            releaseCoef = dsp::onePoleCoef (v[duckRelease] * 0.001f, sr);
            lengthTarget = v[tunnelLength];
            feedback = std::clamp (v[tunnelFeedback], 0.0f, 0.8f);
            juce::ignoreUnused (n);
        }

        void process (float* l, float* r, const float*, const float*, int n, host::Guard& guard) override
        {
            // DUCK: wet x (1 - duck x env), env following the input's level on a -60..0 dB scale (10 ms attack).
            float peak = 0.0f;
            for (int i = 0; i < n; ++i)
                peak = std::max (peak, std::max (std::abs (l[i]), std::abs (r[i])));
            const float level = dsp::Follower::normalised (peak);
            const float coef = 1.0f - std::pow (1.0f - (level > duckEnv ? attackCoef : releaseCoef), (float) n);
            duckEnv += (level - duckEnv) * coef;
            const float gainEnd = 1.0f - duckAmount * duckEnv;

            if (tunnelOn)
                processTunnel (l, r, n);
            else
                fdn.process (l, r, n, settings);

            const float step = (gainEnd - duckGain) / (float) n;
            for (int i = 0; i < n; ++i)
            {
                duckGain += step;
                l[i] *= duckGain;
                r[i] *= duckGain;
            }
            duckGain = gainEnd;
            guard (0, l, r, n);
        }

        void meter (host::Meters& m) const override
        {
            m.custom[0].store (dsp::gainToDb (std::max (1.0e-3f, duckGain)));                   // duck needle (dB)
            m.custom[1].store (fdn.lineLength (0, settings));                                  // shortest line (samples)
            m.custom[2].store (fdn.lineLength (dsp::Fdn::numLines - 1, settings));             // longest line (samples)
            m.custom[3].store ((float) appliedLevel);
            m.custom[4].store (tunnelOn ? 1.0f : 0.0f);
        }

    private:
        // TUNNEL: the input (mono) through a 300-3000 Hz band into one line; six taps at the table times x LENGTH,
        // panned narrowly left and right; the longest tap feeds back, soft-limited, through the band again.
        void processTunnel (float* l, float* r, int n) noexcept
        {
            if (lengthNow < 0.0f)
                lengthNow = lengthTarget;
            const float glide = 1.0f - std::exp (-1.0f / (0.05f * sr));
            for (int i = 0; i < n; ++i)
            {
                lengthNow += (lengthTarget - lengthNow) * glide;
                float wl = 0.0f, wr = 0.0f, last = 0.0f;
                for (int k = 0; k < 6; ++k)
                {
                    const float d = tunnelMs[k] * 0.001f * sr * lengthNow;
                    const float tap = tunnelLine.readHermite (0, (double) std::max (1.0f, d - 1.0f));
                    wl += tap * tunnelLevel[k] * tapPanL[(size_t) k];
                    wr += tap * tunnelLevel[k] * tapPanR[(size_t) k];
                    last = tap;
                }
                const float fb = dsp::fastTanh (last * feedback * 1.2f) / 1.2f;
                float x = 0.5f * (l[i] + r[i]) + fb;
                x = bandLp[0].lowpass (bandHp[0].highpass (x));
                tunnelLine.push (x);
                l[i] = wl * 0.6f;
                r[i] = wr * 0.6f;
            }
        }

        dsp::Fdn fdn;
        dsp::Fdn::Settings settings;
        dsp::DelayLine tunnelLine;
        std::array<dsp::SVF, 1> bandHp, bandLp;
        std::array<float, 6> tapPanL {}, tapPanR {};
        float sr = 48000.0f, duckEnv = 0.0f, duckGain = 1.0f, duckAmount = 0.4f, attackCoef = 0.002f, releaseCoef = 0.0001f;
        float lengthNow = -1.0f, lengthTarget = 1.0f, feedback = 0.5f;
        int appliedLevel = 3, wantedLevel = 3;
        bool tunnelOn = false, wantedTunnel = false, fresh = true;
    };

    class SiloProcessor final : public host::EffectProcessor
    {
    public:
        SiloProcessor() : EffectProcessor (effectInfo(), std::make_unique<SiloChain>()) {}
        juce::AudioProcessorEditor* createEditor() override;
    };

    class SiloEditor final : public ui::EffectEditor
    {
    public:
        explicit SiloEditor (SiloProcessor& p) : EffectEditor (p)
        {
            needle = own (std::make_unique<ui::Needle> ("DUCK", 0.75f));
            addRow ({ panel ("CHAMBER // КАМЕРА", { { knob ("levels", "LEVEL"), 1.0f }, { knob ("size", "SIZE"), 1.0f },
                                                   { knob ("decay", "DECAY"), 1.0f }, { knob ("damping", "DAMPING"), 1.0f } }),
                      panel ("MODES // МОДЫ", { { knob ("modal", "MODAL"), 1.0f }, { knob ("predelay", "PRE-DELAY"), 1.0f } }),
                      panel ("CLAUSTROPHOBIA // КЛАУСТРОФОБИЯ", { { knob ("claustrophobia", "CLAUSTRO", true), 1.0f } }, true) });
            addRow ({ panel ("DUCK // ПРИГЛУШЕНИЕ", { { needle, 1.7f }, { knob ("duck", "DUCK"), 1.0f }, { knob ("duck_release", "RELEASE"), 1.0f } }),
                      panel ("TUNNEL // ТОННЕЛЬ", { { choices ({ { "tunnel", "TUNNEL" } }), 1.2f }, { knob ("tunnel_length", "LENGTH"), 1.0f },
                                                   { knob ("tunnel_feedback", "FEEDBACK"), 1.0f } }),
                      outputPanel() });
            finish();
        }

    private:
        void refresh (float seconds) override
        {
            const float db = processor.meters.custom[0].load();
            needle->setReadout (juce::String (db, 1) + " dB  L" + juce::String (juce::roundToInt (processor.meters.custom[3].load())));
            needle->advance (std::clamp (-db / 24.0f, 0.0f, 1.0f), seconds);
        }

        ui::Needle* needle = nullptr;
    };

    juce::AudioProcessorEditor* SiloProcessor::createEditor() { return new SiloEditor (*this); }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SiloProcessor();
}
