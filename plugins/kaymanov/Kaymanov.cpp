// KAYMANOV // ИЗЛУЧАТЕЛЬ — the Kaymanov emitter under the Yantar factory and its failing cooling pumps.
// LAB X-10's EMITTER (an oversampled split-band shaper that keeps the sub clean) plus COOLANT, a beat-locked duty
// cycle whose dips drive the shaper harder: the pumps stop, the emitter heats.

#include "core/host/EffectProcessor.h"
#include "core/dsp/Modulation.h"
#include "core/dsp/SplitBandDrive.h"
#include "ui/EffectEditor.h"

namespace
{
    using namespace lab;
    using host::Kind;
    using host::Smooth;

    enum Param : int { mode = 0, drive, bias, tone, split, subMono, oversample, coolShape, coolSync, coolDepth, coolHold,
                       coolAmount, coolTarget, heat };

    const host::EffectInfo& effectInfo()
    {
        static const host::EffectInfo i = []
        {
            host::EffectInfo e;
            e.product = LABX16_PRODUCT;
            e.subtitle = "ИЗЛУЧАТЕЛЬ";
            e.slug = LABX16_SLUG;
            e.version = LABX16_VERSION;
            e.code = LABX16_CODE;
            e.faction = host::Faction::deepZone;
            e.params = {
                { "mode",        "Mode",           Kind::choice, 0.0f,  4.0f,    0.0f,   0.0f,   Smooth::none,   "",   "Tube|Clip|Fold|Rectify|Crush" },
                { "drive",       "Drive",          Kind::real,   0.0f,  60.0f,   12.0f,  0.0f,   Smooth::linear, "dB", "" },
                { "bias",        "Bias",           Kind::real,   -1.0f, 1.0f,    0.0f,   0.0f,   Smooth::linear, "%",  "" },
                { "tone",        "Tone",           Kind::real,   -1.0f, 1.0f,    0.0f,   0.0f,   Smooth::linear, "%",  "" },
                { "split",       "Split",          Kind::real,   20.0f, 1000.0f, 120.0f, 140.0f, Smooth::log,    "Hz", "" },
                { "sub_mono",    "Sub Mono",       Kind::choice, 0.0f,  1.0f,    1.0f,   0.0f,   Smooth::none,   "",   "Off|On" },
                { "oversample",  "Oversample",     Kind::choice, 0.0f,  2.0f,    1.0f,   0.0f,   Smooth::none,   "",   "Off|2x|4x" },
                { "cool_shape",  "Coolant",        Kind::choice, 0.0f,  4.0f,    0.0f,   0.0f,   Smooth::none,   "",   "Off|Pump|Gate|Saw|Sine" },
                { "cool_sync",   "Coolant Rate",   Kind::choice, 0.0f,  12.0f,   1.0f,   0.0f,   Smooth::none,   "",   dsp::rateChoicesSynced },
                { "cool_depth",  "Coolant Depth",  Kind::real,   0.0f,  1.0f,    0.5f,   0.0f,   Smooth::linear, "%",  "" },
                { "cool_hold",   "Coolant Hold",   Kind::real,   0.0f,  1.0f,    0.4f,   0.0f,   Smooth::linear, "%",  "" },
                { "cool_amount", "Coolant Heat",   Kind::real,   0.0f,  1.0f,    0.6f,   0.0f,   Smooth::linear, "%",  "" },
                { "cool_target", "Coolant Target", Kind::choice, 0.0f,  2.0f,    0.0f,   0.0f,   Smooth::none,   "",   "Drive|Level|Both" },
                { "heat",        "HEAT",           Kind::real,   0.0f,  1.0f,    0.0f,   0.0f,   Smooth::linear, "%",  "" },
            };
            e.stages = { "shaper" };
            e.presets = {
                { "Init", {} },
                { "Cooling Pumps Off", { { "drive", 20.0f }, { "cool_shape", 1.0f }, { "cool_sync", 2.0f }, { "cool_depth", 0.8f },
                                         { "cool_amount", 0.8f }, { "output", -14.3f } } },
                { "Lefty's Repair", { { "drive", 8.0f }, { "tone", 0.15f }, { "cool_shape", 4.0f }, { "cool_sync", 0.0f },
                                      { "cool_depth", 0.25f }, { "cool_amount", 0.2f }, { "output", -4.0f } } },
                { "Zombified Bass", { { "mode", 2.0f }, { "drive", 36.0f }, { "split", 90.0f }, { "tone", -0.2f }, { "output", -13.8f } } },
                { "Factory Floor", { { "mode", 1.0f }, { "drive", 24.0f }, { "cool_shape", 2.0f }, { "cool_sync", 2.0f },
                                     { "cool_depth", 0.6f }, { "cool_hold", 0.5f }, { "cool_target", 2.0f }, { "output", -14.2f } } },
                { "X-18 Components", { { "mode", 3.0f }, { "drive", 22.0f }, { "bias", 0.5f }, { "tone", 0.2f }, { "output", -6.7f } } },
                { "Full Power", { { "mode", 4.0f }, { "drive", 30.0f }, { "oversample", 2.0f }, { "heat", 1.0f }, { "cool_shape", 1.0f },
                                  { "cool_sync", 1.0f }, { "output", -0.7f } } },
            };
            return e;
        }();
        return i;
    }

    class KaymanovChain final : public host::Chain
    {
    public:
        std::atomic<float> heatDb { 0.0f };

        void prepare (double sampleRate, int) override
        {
            sr = (float) sampleRate;
            emitter.prepare (sampleRate);
            cooldown.prepare (sr);
            heatCoef = dsp::onePoleCoef (0.25f, sr);
            fresh = true;
        }

        void reset() override
        {
            emitter.reset();
            cooldown.reset();
            heatFollow = 0.0f;
        }

        // Preset or oversampling change (the base has faded the effect out): apply the pending path.
        void flush() override
        {
            emitter.setOversampling (wantedOversampling);
            reset();
        }

        int latency (double) const override { return emitter.getLatency(); }

        void control (const float* v, const host::BlockContext& ctx, int n) override
        {
            const float heatMacro = v[heat];
            const int shape = (int) std::lround (v[coolShape]);
            const float depth = std::clamp (v[coolDepth] + 0.3f * heatMacro, 0.0f, 1.0f);
            const float beats = shape > 0 ? dsp::syncedRateBeats ((int) std::lround (v[coolSync])) : 0.0f;
            std::array<float, host::controlBlock> gains {};
            cooldown.process (gains.data(), n, beats, std::max (0, shape - 1), depth, v[coolHold], *ctx.transport);
            const float c = gains[(size_t) std::max (0, n - 1)];   // the duty cycle at the block end

            const int target = (int) std::lround (v[coolTarget]);
            const bool toDrive = target != 1, toLevel = target != 0;
            settings.mode = (int) std::lround (v[mode]);
            settings.driveDb = v[drive] + 24.0f * heatMacro + (toDrive ? v[coolAmount] * 24.0f * (1.0f - c) : 0.0f);
            settings.bias = std::clamp (v[bias] + 0.3f * heatMacro, -1.0f, 1.0f);
            settings.tone = std::clamp (v[tone] - 0.3f * heatMacro, -1.0f, 1.0f);
            settings.splitHz = v[split];
            settings.subMono = v[subMono] > 0.5f;
            settings.highGain = toLevel ? c : 1.0f;

            // Changes fade through the base's switch; the first block after prepare takes the setting directly.
            wantedOversampling = (int) std::lround (v[oversample]);
            if (fresh)
                emitter.setOversampling (wantedOversampling);
            else if (wantedOversampling != emitter.getOversampling() && ctx.requestSwitch != nullptr)
                *ctx.requestSwitch = true;
            fresh = false;

            heatFollow += (settings.driveDb - heatFollow) * (1.0f - std::pow (1.0f - heatCoef, (float) n));
            heatDb.store (heatFollow);
        }

        void process (float* l, float* r, const float*, const float*, int n, host::Guard& guard) override
        {
            emitter.process (l, r, n, settings);
            guard (0, l, r, n);
        }

        void meter (host::Meters& m) const override { m.custom[0].store (heatFollow); }

    private:
        dsp::SplitBandDrive emitter;
        dsp::SplitBandDrive::Settings settings;
        dsp::Cooldown cooldown;
        float sr = 48000.0f, heatFollow = 0.0f, heatCoef = 0.001f;
        int wantedOversampling = 1;
        bool fresh = true;
    };

    class KaymanovProcessor final : public host::EffectProcessor
    {
    public:
        KaymanovProcessor() : EffectProcessor (effectInfo(), std::make_unique<KaymanovChain>()) {}
        juce::AudioProcessorEditor* createEditor() override;
    };

    class KaymanovEditor final : public ui::EffectEditor
    {
    public:
        explicit KaymanovEditor (KaymanovProcessor& p) : EffectEditor (p)
        {
            needle = own (std::make_unique<ui::Needle> ("HEAT", 0.8f));
            addRow ({ panel ("EMITTER // ИЗЛУЧАТЕЛЬ", { { knob ("drive", "DRIVE"), 1.0f }, { knob ("bias", "BIAS"), 1.0f }, { knob ("tone", "TONE"), 1.0f },
                                                       { choices ({ { "mode", "MODE" }, { "oversample", "OVERSAMPLE" } }), 1.3f } }),
                      panel ("SPLIT // РАЗДЕЛ", { { knob ("split", "SPLIT"), 1.0f }, { choices ({ { "sub_mono", "SUB MONO" } }), 1.2f } }),
                      panel ("HEAT // НАГРЕВ", { { needle, 1.9f }, { knob ("heat", "HEAT", true), 1.0f } }, true) });
            addRow ({ panel ("COOLANT // ОХЛАЖДЕНИЕ", { { choices ({ { "cool_shape", "COOLANT" }, { "cool_sync", "RATE" }, { "cool_target", "TARGET" } }), 1.4f },
                                                      { knob ("cool_depth", "DEPTH"), 1.0f }, { knob ("cool_hold", "HOLD"), 1.0f },
                                                      { knob ("cool_amount", "HEAT"), 1.0f } }),
                      outputPanel() });
            finish();
        }

    private:
        void refresh (float seconds) override
        {
            const float heatDb = processor.meters.custom[0].load();
            const float drive = processor.effectRaw (Param::drive);
            needle->setReadout (juce::String (heatDb, 1) + " dB");
            needle->advance (heatDb / 60.0f, seconds, drive / 60.0f);
        }

        ui::Needle* needle = nullptr;
    };

    juce::AudioProcessorEditor* KaymanovProcessor::createEditor() { return new KaymanovEditor (*this); }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new KaymanovProcessor();
}
