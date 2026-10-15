// DOSIMETER // ДОЗИМЕТР — radiation and the detectors that tick with it.
// One lo-fi stage: CRUSH (bits and sample-hold), a MEDIUM tone cap, Poisson Geiger clicks whose rate follows the
// input's level, and dust and hiss that follow its envelope. The needle counts the clicks.

#include "core/host/EffectProcessor.h"
#include "core/dsp/Crush.h"
#include "core/dsp/Filters.h"
#include "core/dsp/Follower.h"
#include "core/dsp/Generators.h"
#include "ui/EffectEditor.h"

namespace
{
    using namespace lab;
    using host::Kind;
    using host::Smooth;

    enum Param : int { dose = 0, track, tone, clickLevel, dust, bits, hold, crushMix, medium, stereo };
    enum Medium : int { clean = 0, tape, radio, phone };

    const host::EffectInfo& effectInfo()
    {
        static const host::EffectInfo i = []
        {
            host::EffectInfo e;
            e.product = LABX16_PRODUCT;
            e.subtitle = "ДОЗИМЕТР";
            e.slug = LABX16_SLUG;
            e.version = LABX16_VERSION;
            e.code = LABX16_CODE;
            e.faction = host::Faction::deepZone;
            e.params = {
                { "dose",        "Dose",        Kind::real,   0.0f,   1.0f,    0.3f,    0.0f,    Smooth::linear, "%",   "" },
                { "track",       "Track",       Kind::real,   0.0f,   1.0f,    0.7f,    0.0f,    Smooth::linear, "%",   "" },
                { "tone",        "Click Tone",  Kind::real,   500.0f, 8000.0f, 3000.0f, 2000.0f, Smooth::log,    "Hz",  "" },
                { "click_level", "Click Level", Kind::real,   0.0f,   1.0f,    0.5f,    0.0f,    Smooth::linear, "%",   "" },
                { "dust",        "Dust",        Kind::real,   0.0f,   1.0f,    0.2f,    0.0f,    Smooth::linear, "%",   "" },
                { "bits",        "Bits",        Kind::real,   1.0f,   16.0f,   16.0f,   0.0f,    Smooth::linear, "bit", "" },
                { "hold",        "Rate Divide", Kind::real,   1.0f,   64.0f,   1.0f,    8.0f,    Smooth::linear, "x",   "" },
                { "crush_mix",   "Crush Mix",   Kind::real,   0.0f,   1.0f,    1.0f,    0.0f,    Smooth::linear, "%",   "" },
                { "medium",      "Medium",      Kind::choice, 0.0f,   3.0f,    0.0f,    0.0f,    Smooth::none,   "",    "Clean|Tape|Radio|Phone" },
                { "stereo",      "Clicks",      Kind::choice, 0.0f,   1.0f,    1.0f,    0.0f,    Smooth::none,   "",    "Mono|Stereo" },
            };
            e.stages = { "crush" };
            e.presets = {
                { "Init", {} },
                { "Background Count", { { "dose", 0.2f }, { "track", 0.3f }, { "click_level", 0.35f }, { "dust", 0.15f }, { "tone", 2500.0f }, { "output", 0.0f } } },
                { "Garbage Heap", { { "dose", 0.45f }, { "dust", 0.6f }, { "bits", 10.0f }, { "hold", 2.0f }, { "medium", 1.0f },
                                    { "click_level", 0.4f }, { "output", 0.2f } } },
                { "Agroprom Tunnels", { { "mix", 0.8f }, { "medium", 3.0f }, { "bits", 8.0f }, { "hold", 3.0f }, { "dose", 0.35f }, { "dust", 0.3f },
                                        { "tone", 1800.0f }, { "output", 11.4f } } },
                { "Pripyat Hotspot", { { "dose", 1.0f }, { "track", 0.8f }, { "click_level", 0.6f }, { "tone", 4000.0f }, { "dust", 0.3f }, { "output", -0.4f } } },
                { "Burnt Fuzz", { { "bits", 5.0f }, { "hold", 4.0f }, { "crush_mix", 0.85f }, { "dose", 0.15f }, { "dust", 0.35f },
                                  { "medium", 2.0f }, { "output", 0.2f } } },
                { "Clicks Only", { { "crush_mix", 0.0f }, { "dose", 0.5f }, { "track", 0.5f }, { "dust", 0.0f }, { "output", -0.1f } } },
            };
            return e;
        }();
        return i;
    }

    class DosimeterChain final : public host::Chain
    {
    public:
        void prepare (double sampleRate, int) override
        {
            sr = (float) sampleRate;
            follower.set (0.005f, 0.25f, sr);
            countWindow = std::max (1, (int) std::lround (0.1 * sampleRate));
            playing.prepare (sr);
            setupMedium();
            reset();
            fresh = true;
        }

        void reset() override
        {
            crush.reset();
            follower.reset();
            for (auto& c : chan)
            {
                c.hp.reset();
                c.lp.reset();
                c.tapeLp.reset();
            }
            // Seeded from the instance's callsign: an FL render of the project repeats click for click.
            for (int k = 0; k < 2; ++k)
            {
                geiger[(size_t) k].prepare (sr, dsp::hash32 (0, k, 0x6E16u, callsign));
                dustGen[(size_t) k].prepare (sr, dsp::hash32 (0, k, 0xD057u, callsign));
            }
            geiger[0].setTone (toneHz);
            geiger[1].setTone (toneHz);
            windowClicks = 0;
            windowSamples = 0;
        }

        // A MEDIUM change (the base has faded the effect out): the new filters start from silence.
        void flush() override
        {
            appliedMedium = wantedMedium;
            setupMedium();
            reset();
        }

        void control (const float* v, const host::BlockContext& ctx, int) override
        {
            playing.set (ctx.transport->playing);
            if (ctx.callsign != callsign)
            {
                callsign = ctx.callsign;
                reset();
            }
            doseNow = v[dose];
            trackNow = v[track];
            clickGain = v[clickLevel];
            dustAmount = v[dust];
            bitsNow = v[bits];
            holdNow = v[hold];
            crushMixNow = v[crushMix];
            stereoClicks = v[stereo] > 0.5f;
            if (std::abs (v[tone] - toneHz) > 0.01f)
            {
                toneHz = v[tone];
                geiger[0].setTone (toneHz);
                geiger[1].setTone (toneHz);
            }
            // Changes fade through the base's switch; the first block after prepare takes the setting directly.
            wantedMedium = std::clamp ((int) std::lround (v[medium]), 0, 3);
            if (fresh && wantedMedium != appliedMedium)
            {
                appliedMedium = wantedMedium;
                setupMedium();
            }
            else if (wantedMedium != appliedMedium && ctx.requestSwitch != nullptr)
                *ctx.requestSwitch = true;
            fresh = false;
        }

        void process (float* l, float* r, const float*, const float*, int n, host::Guard& guard) override
        {
            std::array<float, host::controlBlock> dryL {}, dryR {}, env {};
            for (int i = 0; i < n; ++i)
            {
                dryL[(size_t) i] = l[i];
                dryR[(size_t) i] = r[i];
                env[(size_t) i] = dsp::Follower::normalised (follower.processStereo (l[i], r[i]));
            }

            // CRUSH, blended by CRUSH MIX, then the MEDIUM cap.
            if (crushMixNow > 1.0e-4f)
            {
                std::array<float, host::controlBlock> cl {}, cr {};
                std::copy (l, l + n, cl.begin());
                std::copy (r, r + n, cr.begin());
                crush.process (cl.data(), cr.data(), n, bitsNow, holdNow);
                for (int i = 0; i < n; ++i)
                {
                    l[i] += (cl[(size_t) i] - l[i]) * crushMixNow;
                    r[i] += (cr[(size_t) i] - r[i]) * crushMixNow;
                }
            }
            if (appliedMedium != clean)
                for (int i = 0; i < n; ++i)
                {
                    l[i] = chan[0].process (l[i], appliedMedium);
                    r[i] = chan[1].process (r[i], appliedMedium);
                }
            guard (0, l, r, n);

            // GEIGER: Poisson clicks at dose^2 x 400 /s, from a constant rate (TRACK 0) to the input's level (TRACK 1).
            // The counter and the dust sound while the song plays: FL keeps processing a stopped track.
            const float base = doseNow * doseNow * 400.0f;
            for (int i = 0; i < n; ++i)
            {
                const float rate = base * ((1.0f - trackNow) + trackNow * env[(size_t) i]);
                const float cl = geiger[0].next (rate);
                const float cr = stereoClicks ? geiger[1].next (rate) : cl;
                const float gate = playing.next();
                l[i] += (cl * clickGain + dustGen[0].next (dryL[(size_t) i], dustAmount)) * gate;
                r[i] += (cr * clickGain + dustGen[1].next (dryR[(size_t) i], dustAmount)) * gate;
            }

            const int clicks = geiger[0].takeClicks();
            geiger[1].takeClicks();
            totalClicks += clicks;
            windowClicks += clicks;
            windowSamples += n;
            if (windowSamples >= countWindow)
            {
                clickRate = (float) windowClicks * sr / (float) windowSamples;
                windowClicks = 0;
                windowSamples = 0;
            }
        }

        void meter (host::Meters& m) const override
        {
            m.custom[0].store (clickRate);              // the needle: clicks per second over the last 100 ms
            m.custom[1].store ((float) totalClicks);    // test readout: clicks since prepare (left channel)
        }

    private:
        struct Channel
        {
            dsp::Butter4 hp, lp;
            dsp::SVF tapeLp;
            float process (float x, int m) noexcept
            {
                if (m == Medium::tape)
                    return tapeLp.lowpass (x);
                if (m == Medium::phone)
                    return lp.process (hp.process (x));
                return lp.process (x);   // radio
            }
        };

        void setupMedium()
        {
            for (auto& c : chan)
            {
                c.tapeLp.setHz (6500.0f, 0.7071f, sr);
                c.hp.setHz (300.0f, true, sr);
                c.lp.setHz (appliedMedium == radio ? 3000.0f : 3400.0f, false, sr);
            }
        }

        dsp::Crush crush;
        dsp::Follower follower;
        std::array<dsp::Geiger, 2> geiger;
        dsp::PlayGate playing;
        std::array<dsp::Dust, 2> dustGen;
        std::array<Channel, 2> chan;
        float sr = 48000.0f, toneHz = 3000.0f, clickRate = 0.0f;
        float doseNow = 0.3f, trackNow = 0.7f, clickGain = 0.5f, dustAmount = 0.2f, bitsNow = 16.0f, holdNow = 1.0f, crushMixNow = 1.0f;
        bool stereoClicks = true, fresh = true;
        int appliedMedium = clean, wantedMedium = clean, countWindow = 4800, windowClicks = 0, windowSamples = 0;
        int64_t totalClicks = 0;
        uint32_t callsign = 0;
    };

    class DosimeterProcessor final : public host::EffectProcessor
    {
    public:
        DosimeterProcessor() : EffectProcessor (effectInfo(), std::make_unique<DosimeterChain>()) {}
        juce::AudioProcessorEditor* createEditor() override;
    };

    class DosimeterEditor final : public ui::EffectEditor
    {
    public:
        explicit DosimeterEditor (DosimeterProcessor& p) : EffectEditor (p)
        {
            needle = own (std::make_unique<ui::Needle> ("COUNT", 0.8f));
            addRow ({ panel ("DOSE // ДОЗА", { { needle, 1.9f }, { knob ("dose", "DOSE", true), 1.0f }, { knob ("track", "TRACK"), 1.0f } }, true),
                      panel ("GEIGER // СЧЁТЧИК", { { knob ("tone", "TONE"), 1.0f }, { knob ("click_level", "CLICKS"), 1.0f },
                                                   { choices ({ { "stereo", "CLICKS" } }), 1.2f } }),
                      panel ("DUST // ПЫЛЬ", { { knob ("dust", "DUST"), 1.0f } }) });
            addRow ({ panel ("CRUSH // ДРОБЛЕНИЕ", { { knob ("bits", "BITS"), 1.0f }, { knob ("hold", "RATE DIV"), 1.0f },
                                                    { knob ("crush_mix", "CRUSH MIX"), 1.0f } }),
                      panel ("MEDIUM // СРЕДА", { { choices ({ { "medium", "MEDIUM" } }), 1.3f } }),
                      outputPanel() });
            finish();
        }

    private:
        void refresh (float seconds) override
        {
            const float rate = processor.meters.custom[0].load();
            needle->setReadout (juce::String (juce::roundToInt (rate)) + " /s");
            needle->advance (rate / 400.0f, seconds, std::min (1.0f, rate / 200.0f));
        }

        ui::Needle* needle = nullptr;
    };

    juce::AudioProcessorEditor* DosimeterProcessor::createEditor() { return new DosimeterEditor (*this); }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DosimeterProcessor();
}
