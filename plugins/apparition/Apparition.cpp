// APPARITION // ПРИЗРАК — forecasting, and a deliberate use of plugin delay compensation. The effect reports 250 ms of
// latency and treats that window as look-ahead: an onset found at the write head is in the output's future, so its
// pre-echoes, a reverse swell and a rising whisper can sound BEFORE the hit once FL shifts the output back. PHANTOM adds
// decorrelated doubles that vanish the instant the real transient lands. The apparitions around the Scorcher that
// disappear when attacked; Ghost's body in X-16.
//
// Events read the input history when they play (not when the onset is found), so only causality limits them: a pre-echo
// k echo-times early needs k x ECHO <= 240 ms, and the reverse swell, which plays audio from after the hit backwards,
// needs twice its length inside the window (SWELL LENGTH is 20-120 ms).

#include "core/host/EffectProcessor.h"
#include "core/dsp/Allpass.h"
#include "core/dsp/Filters.h"
#include "core/dsp/Generators.h"
#include "core/dsp/Modulation.h"
#include "core/dsp/Onset.h"
#include "ui/EffectEditor.h"

namespace
{
    using namespace lab;
    using host::Kind;
    using host::Smooth;

    enum Param : int { sensitivity = 0, echoLevel, echoSync, echoMs, echoCount, echoFeedback, grain, swellLevel, swellLen,
                       swellTone, whisperLevel, whisperLead, phantom, haunt };

    constexpr double lookaheadSeconds = 0.25, budgetSeconds = 0.24, preRollSeconds = 0.002;

    const host::EffectInfo& effectInfo()
    {
        static const host::EffectInfo i = []
        {
            host::EffectInfo e;
            e.product = LABX16_PRODUCT;
            e.subtitle = "ПРИЗРАК";
            e.slug = LABX16_SLUG;
            e.version = LABX16_VERSION;
            e.code = LABX16_CODE;
            e.faction = host::Faction::deepZone;
            e.params = {
                { "sensitivity",   "Sensitivity",    Kind::real,    0.0f,   1.0f,     0.5f,    0.0f,    Smooth::linear, "%",  "" },
                { "echo_level",    "Pre-echo",       Kind::real,    0.0f,   1.0f,     0.5f,    0.0f,    Smooth::linear, "%",  "" },
                { "echo_sync",     "Pre-echo Time",  Kind::choice,  0.0f,   10.0f,    5.0f,    0.0f,    Smooth::none,   "",   dsp::timeChoices },
                { "echo_ms",       "Pre-echo ms",    Kind::real,    10.0f,  200.0f,   60.0f,   60.0f,   Smooth::none,   "ms", "" },
                { "echo_count",    "Pre-echo Count", Kind::integer, 1.0f,   6.0f,     3.0f,    0.0f,    Smooth::none,   "",   "" },
                { "echo_feedback", "Pre-echo Decay", Kind::real,    0.0f,   0.9f,     0.6f,    0.0f,    Smooth::linear, "%",  "" },
                { "grain",         "Grain",          Kind::real,    10.0f,  120.0f,   40.0f,   40.0f,   Smooth::none,   "ms", "" },
                { "swell_level",   "Swell",          Kind::real,    0.0f,   1.0f,     0.4f,    0.0f,    Smooth::linear, "%",  "" },
                { "swell_len",     "Swell Length",   Kind::real,    20.0f,  120.0f,   90.0f,   60.0f,   Smooth::none,   "ms", "" },
                { "swell_tone",    "Swell Tone",     Kind::real,    500.0f, 16000.0f, 5000.0f, 3000.0f, Smooth::log,    "Hz", "" },
                { "whisper_level", "Whisper",        Kind::real,    0.0f,   1.0f,     0.2f,    0.0f,    Smooth::linear, "%",  "" },
                { "whisper_lead",  "Whisper Lead",   Kind::real,    50.0f,  200.0f,   120.0f,  110.0f,  Smooth::none,   "ms", "" },
                { "phantom",       "Phantom",        Kind::real,    0.0f,   1.0f,     0.3f,    0.0f,    Smooth::linear, "%",  "" },
                { "haunt",         "Haunt",          Kind::real,    0.0f,   1.0f,     0.0f,    0.0f,    Smooth::linear, "%",  "" },
            };
            e.stages = { "apparition" };
            e.presets = {
                { "Init", {} },
                { "Red Forest Walk", { { "echo_level", 0.35f }, { "swell_level", 0.3f }, { "whisper_level", 0.35f }, { "phantom", 0.4f }, { "output", -0.3f } } },
                { "Ghost's Trail", { { "echo_level", 0.75f }, { "echo_count", 4.0f }, { "echo_sync", 9.0f }, { "echo_feedback", 0.7f },
                                     { "swell_level", 0.1f }, { "phantom", 0.2f }, { "output", -0.9f } } },
                { "Pre-echo Drums", { { "echo_level", 0.6f }, { "echo_sync", 5.0f }, { "echo_count", 2.0f }, { "swell_level", 0.0f },
                                      { "whisper_level", 0.0f }, { "phantom", 0.0f }, { "grain", 30.0f }, { "output", -0.3f } } },
                { "Reverse Choir", { { "swell_level", 0.9f }, { "swell_len", 120.0f }, { "swell_tone", 3000.0f }, { "echo_level", 0.15f },
                                     { "phantom", 0.45f }, { "output", -1.0f } } },
                { "Phantom Pack", { { "phantom", 0.9f }, { "echo_level", 0.2f }, { "swell_level", 0.2f }, { "output", -0.4f } } },
                { "Vanishing", { { "haunt", 0.8f }, { "echo_feedback", 0.4f }, { "output", -1.2f } } },
            };
            return e;
        }();
        return i;
    }

    class ApparitionChain final : public host::Chain
    {
    public:
        void prepare (double sampleRate, int) override
        {
            sr = sampleRate;
            L = latencyFor (sampleRate);
            history.prepare (L + (int) std::ceil (0.13 * sampleRate) + 16);
            detector.prepare ((float) sampleRate);
            whisperGen.prepare ((float) sampleRate, 0x9A05u);
            whisperGen.setFormant (180.0f);
            for (int ch = 0; ch < 2; ++ch)
                for (int k = 0; k < 4; ++k)
                    allpass[(size_t) ch][(size_t) k].prepare ((int) std::ceil (0.024 * sampleRate) + 4);
            preRoll = std::max (1, (int) std::lround (preRollSeconds * sampleRate));
            duckAttack = dsp::onePoleCoef (0.004f, (float) sampleRate);
            duckRelease = dsp::onePoleCoef (0.15f, (float) sampleRate);
            setAllpasses (0);
            reset();
        }

        void reset() override
        {
            history.reset();
            detector.reset();
            whisperGen.reset();
            for (auto& chain : allpass)
                for (auto& a : chain)
                    a.reset();
            swellLp[0].reset();
            swellLp[1].reset();
            events.fill ({});
            onsetTimes.fill (std::numeric_limits<int64_t>::min() / 4);
            writeTime = 0;
            duck = 0.0f;
            active = 0;
        }

        int latency (double sampleRate) const override { return latencyFor (sampleRate); }
        double tailSeconds (const float*) const override { return 0.2; }

        void control (const float* v, const host::BlockContext& ctx, int) override
        {
            if (ctx.callsign != callsign)
            {
                callsign = ctx.callsign;
                setAllpasses (callsign);
            }
            const float h = v[haunt];
            detector.setSensitivity (std::clamp (v[sensitivity] + 0.3f * h, 0.0f, 1.0f));
            echoGain = std::clamp (v[echoLevel] + 0.3f * h, 0.0f, 1.0f);
            const int sync = (int) std::lround (v[echoSync]);
            echoSeconds = sync <= 0 ? v[echoMs] * 0.001 : (double) dsp::timeBeats[std::min (sync, 10)] * 60.0 / std::max (1.0, ctx.transport->bpm);
            echoCountNow = std::clamp ((int) std::lround (v[echoCount]), 1, 6);
            echoDecay = v[echoFeedback];
            grainSamples = std::max (8, (int) (v[grain] * 0.001 * sr));
            swellGain = std::clamp (v[swellLevel] + 0.3f * h, 0.0f, 1.0f);
            swellSamples = std::clamp ((int) std::lround (v[swellLen] * 0.001 * sr), 8, (L - preRoll) / 2);
            swellLp[0].setHz (v[swellTone], (float) sr);
            swellLp[1].setHz (v[swellTone], (float) sr);
            whisperGain = std::clamp (v[whisperLevel] + 0.3f * h, 0.0f, 1.0f);
            whisperSamples = std::clamp ((int) std::lround (v[whisperLead] * 0.001 * sr), 8, (int) std::lround (budgetSeconds * sr));
            phantomGain = std::clamp (v[phantom] + 0.3f * h, 0.0f, 1.0f);
        }

        void process (float* l, float* r, const float*, const float*, int n, host::Guard& guard) override
        {
            const int budget = (int) std::lround (budgetSeconds * sr);
            for (int i = 0; i < n; ++i)
            {
                history.push (l[i], r[i]);
                const int64_t w = writeTime++;   // the input time of the sample just pushed
                if (detector.process (l[i], r[i]))
                    schedule (w, budget);

                const int64_t u = w - L;         // the output time
                float outL = history.readInt (0, L), outR = history.readInt (1, L);
                float evL = 0.0f, evR = 0.0f, swL = 0.0f, swR = 0.0f, whisperSum = 0.0f;
                active = 0;
                for (auto& e : events)
                {
                    if (e.type == none || u < e.start)
                        continue;
                    const int64_t k = u - e.start;
                    if (k >= e.length)
                    {
                        e.type = none;
                        continue;
                    }
                    ++active;
                    const float x = (float) k / (float) e.length;
                    if (e.type == echo)
                    {
                        // Forward from just before the hit: a 2 ms fade-in, then a cos^2 decay over the grain.
                        const int64_t src = e.src + k;
                        const int d = (int) (w - src);
                        const float win = k < preRoll ? (float) k / (float) preRoll
                                                      : dsp::sin2pi (0.25f + 0.25f * (float) (k - preRoll) / (float) std::max<int64_t> (1, e.length - preRoll));
                        const float g = e.gain * win * win;
                        evL += history.readInt (0, d) * g;
                        evR += history.readInt (1, d) * g;
                    }
                    else if (e.type == swell)
                    {
                        // Backwards from the end of the window to the hit, rising into it.
                        const int64_t src = e.src - k;
                        const int d = (int) (w - src);
                        const float g = e.gain * x * x;
                        swL += history.readInt (0, d) * g;
                        swR += history.readInt (1, d) * g;
                    }
                    else
                        whisperSum += e.gain * x * x;
                }
                if (whisperSum > 0.0f)
                {
                    const float wv = whisperGen.next() * whisperSum;
                    evL += wv;
                    evR += wv;
                }
                else
                    whisperGen.next();

                // PHANTOM: decorrelated doubles, ducked from 10 ms before every known hit until 50 ms after it, then
                // released over 150 ms.
                bool hitSoon = false;
                for (const auto t : onsetTimes)
                    if (t - u <= (int64_t) (0.01 * sr) && u - t <= (int64_t) (0.05 * sr))
                        hitSoon = true;
                duck += ((hitSoon ? 1.0f : 0.0f) - duck) * (hitSoon ? duckAttack : duckRelease);
                float phL = outL, phR = outR;
                for (auto& a : allpass[0]) phL = a.process (phL);
                for (auto& a : allpass[1]) phR = a.process (phR);
                const float pg = phantomGain * (1.0f - duck) * 0.7f;

                l[i] = outL + evL + swellLp[0].lowpass (swL) + phL * pg;
                r[i] = outR + evR + swellLp[1].lowpass (swR) + phR * pg;
            }
            guard (0, l, r, n);
        }

        void meter (host::Meters& m) const override
        {
            m.custom[0].store ((float) active);
            m.custom[1].store (duck);
            m.custom[2].store ((float) onsetCount);
        }

    private:
        enum Type { none = 0, echo, swell, whisper };
        struct Event { int type = none; int64_t start = 0, src = 0; int length = 0; float gain = 0.0f; };

        static int latencyFor (double sampleRate) { return (int) std::lround (lookaheadSeconds * sampleRate); }

        void setAllpasses (uint32_t salt)
        {
            for (int ch = 0; ch < 2; ++ch)
                for (int k = 0; k < 4; ++k)
                {
                    const float ms = 5.0f + 18.0f * dsp::hash01 (ch, k, 0xA11Fu, salt);
                    allpass[(size_t) ch][(size_t) k].set ((int) (ms * 0.001 * sr), 0.6f);
                }
        }

        void add (const Event& e)
        {
            // Oldest-ending slot if all are busy.
            Event* slot = &events[0];
            for (auto& s : events)
            {
                if (s.type == none) { slot = &s; break; }
                if (s.start + s.length < slot->start + slot->length) slot = &s;
            }
            *slot = e;
        }

        // An onset at input time t0: everything it causes, placed before it in output time, within the budget.
        void schedule (int64_t t0, int budget)
        {
            ++onsetCount;
            onsetTimes[(size_t) (onsetCount % (int64_t) onsetTimes.size())] = t0;
            const float level = std::clamp (detector.peak() * 4.0f, 0.0f, 1.0f);
            if (echoGain > 1.0e-4f)
            {
                const int64_t spacing = std::max<int64_t> (1, std::llround (echoSeconds * sr));   // 0.06 x 48000 is 2879.99...
                for (int k = 1; k <= echoCountNow; ++k)
                {
                    if (k * spacing + preRoll > budget)
                        break;   // past the look-ahead: dropped
                    add ({ echo, t0 - k * spacing - preRoll, t0 - preRoll, preRoll + grainSamples, echoGain * std::pow (echoDecay, (float) (k - 1)) });
                }
            }
            if (swellGain > 1.0e-4f)
                add ({ swell, t0 - swellSamples, t0 + swellSamples - 1, swellSamples, swellGain });
            if (whisperGain > 1.0e-4f)
                add ({ whisper, t0 - whisperSamples, 0, whisperSamples, whisperGain * level * 0.6f });
        }

        double sr = 48000.0, echoSeconds = 0.06;
        int L = 12000, preRoll = 96, grainSamples = 1920, swellSamples = 4320, whisperSamples = 5760, echoCountNow = 3, active = 0;
        float echoGain = 0.5f, echoDecay = 0.6f, swellGain = 0.4f, whisperGain = 0.2f, phantomGain = 0.3f;
        float duck = 0.0f, duckAttack = 0.005f, duckRelease = 0.0001f;
        int64_t writeTime = 0, onsetCount = 0;
        uint32_t callsign = 0;
        dsp::StereoDelayLine history;
        dsp::OnsetDetector detector;
        dsp::Whisper whisperGen;
        std::array<std::array<dsp::Allpass, 4>, 2> allpass;
        std::array<dsp::OnePole, 2> swellLp;
        std::array<Event, 64> events {};
        std::array<int64_t, 8> onsetTimes {};
    };

    class ApparitionProcessor final : public host::EffectProcessor
    {
    public:
        ApparitionProcessor() : EffectProcessor (effectInfo(), std::make_unique<ApparitionChain>()) {}
        juce::AudioProcessorEditor* createEditor() override;
    };

    class ApparitionEditor final : public ui::EffectEditor
    {
    public:
        explicit ApparitionEditor (ApparitionProcessor& p) : EffectEditor (p)
        {
            needle = own (std::make_unique<ui::Needle> ("APPARITIONS", 0.75f));
            addRow ({ panel ("FORECAST // ПРЕДВИДЕНИЕ", { { needle, 1.8f }, { knob ("sensitivity", "SENSE"), 1.0f },
                                                         { knob ("haunt", "HAUNT", true), 1.0f } }, true),
                      panel ("PRE-ECHO // ПРЕДЭХО", { { knob ("echo_level", "LEVEL"), 1.0f }, { choices ({ { "echo_sync", "TIME" } }), 1.2f },
                                                     { knob ("echo_ms", "MS"), 1.0f }, { knob ("echo_count", "COUNT"), 1.0f },
                                                     { knob ("echo_feedback", "DECAY"), 1.0f }, { knob ("grain", "GRAIN"), 1.0f } }) });
            addRow ({ panel ("SWELL // НАРАСТАНИЕ", { { knob ("swell_level", "SWELL"), 1.0f }, { knob ("swell_len", "LENGTH"), 1.0f },
                                                     { knob ("swell_tone", "TONE"), 1.0f } }),
                      panel ("WHISPER // ШЁПОТ", { { knob ("whisper_level", "WHISPER"), 1.0f }, { knob ("whisper_lead", "LEAD"), 1.0f } }),
                      panel ("PHANTOM // ФАНТОМ", { { knob ("phantom", "PHANTOM"), 1.0f } }) });
            addRow ({ outputPanel() });
            finish();
        }

    private:
        void refresh (float seconds) override
        {
            const auto& m = processor.meters;
            const float events = m.custom[0].load();
            needle->setReadout (juce::String (juce::roundToInt (events)) + " ACTIVE");
            needle->advance (std::min (1.0f, events / 12.0f), seconds, 0.3f * m.custom[1].load());
        }

        ui::Needle* needle = nullptr;
    };

    juce::AudioProcessorEditor* ApparitionProcessor::createEditor() { return new ApparitionEditor (*this); }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ApparitionProcessor();
}
