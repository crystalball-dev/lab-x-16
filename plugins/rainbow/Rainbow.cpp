// RAINBOW // РАДУГА — Rainbow, the Brain Scorcher's codename: five antennas, one leaning and dead.
// Five tempo-synced taps with their own level, pan and tone, Ensemble motion on the tap times, a darkening
// soft-limited feedback path, and a DAMAGED fifth tap that detunes, narrows, crushes and stutters.

#include "core/host/EffectProcessor.h"
#include "core/dsp/Crush.h"
#include "core/dsp/Filters.h"
#include "core/dsp/MicroShift.h"
#include "core/dsp/Modulation.h"
#include "ui/EffectEditor.h"

namespace
{
    using namespace lab;
    using host::Kind;
    using host::Smooth;

    constexpr int taps = 5;
    enum Param : int { pattern = 0, tap1Sync, tap1Ms = tap1Sync + taps, tap1Level = tap1Ms + taps, tap1Pan = tap1Level + taps,
                       tone = tap1Pan + taps, feedback, fbSource, motion, motionRate, damaged, pingpong, transmit };

    // Tap times in quarter notes, per pattern.
    constexpr float patterns[5][taps] = {
        { 1.0f, 2.0f, 3.0f, 4.0f, 5.0f },                                   // Even
        { 0.5f, 0.75f, 1.25f, 2.0f, 4.0f },                                 // Antenna
        { 1.0f, 1.618f, 2.618f, 4.236f, 6.854f },                           // Golden
        { 0.75f, 1.5f, 2.25f, 3.0f, 3.75f },                                // Dotted
        { 2.0f / 3.0f, 4.0f / 3.0f, 2.0f, 8.0f / 3.0f, 10.0f / 3.0f },      // Triplet
    };
    constexpr const char* syncChoices = "Pattern|Free|1/4|1/8|1/8 D|1/8 T|1/16|1/4 D|1/2|3/16|1/32|1/1";
    constexpr float maxSeconds = 4.0f;

    // One tap's time in seconds: its pattern slot, its own ms (Free), or a TIME division.
    float tapSeconds (const float* v, int k, float bpm) noexcept
    {
        const float beat = 60.0f / std::max (1.0f, bpm);
        const int sync = (int) std::lround (v[tap1Sync + k]);
        float seconds;
        if (sync <= 0)
            seconds = patterns[std::clamp ((int) std::lround (v[pattern]), 0, 4)][k] * beat;
        else if (sync == 1)
            seconds = v[tap1Ms + k] * 0.001f;
        else
            seconds = dsp::timeBeats[std::min (sync - 1, 10)] * beat;
        return std::clamp (seconds, 0.001f, maxSeconds);
    }

    const host::EffectInfo& effectInfo()
    {
        static const host::EffectInfo i = []
        {
            static const char* syncIds[taps] = { "tap1_sync", "tap2_sync", "tap3_sync", "tap4_sync", "tap5_sync" };
            static const char* syncNames[taps] = { "Tap 1 Time", "Tap 2 Time", "Tap 3 Time", "Tap 4 Time", "Tap 5 Time" };
            static const char* msIds[taps] = { "tap1_ms", "tap2_ms", "tap3_ms", "tap4_ms", "tap5_ms" };
            static const char* msNames[taps] = { "Tap 1 ms", "Tap 2 ms", "Tap 3 ms", "Tap 4 ms", "Tap 5 ms" };
            static const char* levelIds[taps] = { "tap1_level", "tap2_level", "tap3_level", "tap4_level", "tap5_level" };
            static const char* levelNames[taps] = { "Tap 1 Level", "Tap 2 Level", "Tap 3 Level", "Tap 4 Level", "Tap 5 Level" };
            static const char* panIds[taps] = { "tap1_pan", "tap2_pan", "tap3_pan", "tap4_pan", "tap5_pan" };
            static const char* panNames[taps] = { "Tap 1 Pan", "Tap 2 Pan", "Tap 3 Pan", "Tap 4 Pan", "Tap 5 Pan" };
            constexpr float levels[taps] = { 0.8f, 0.7f, 0.6f, 0.5f, 0.4f };
            constexpr float pans[taps] = { -0.6f, 0.6f, -0.3f, 0.3f, 0.0f };

            host::EffectInfo e;
            e.product = LABX16_PRODUCT;
            e.subtitle = "РАДУГА";
            e.slug = LABX16_SLUG;
            e.version = LABX16_VERSION;
            e.code = LABX16_CODE;
            e.faction = host::Faction::etherealSignal;
            e.mixLaw = host::MixLaw::equalPower;
            e.params.push_back ({ "pattern", "Pattern", Kind::choice, 0.0f, 4.0f, 1.0f, 0.0f, Smooth::none, "", "Even|Antenna|Golden|Dotted|Triplet" });
            for (int k = 0; k < taps; ++k)
                e.params.push_back ({ syncIds[k], syncNames[k], Kind::choice, 0.0f, 11.0f, 0.0f, 0.0f, Smooth::none, "", syncChoices });
            for (int k = 0; k < taps; ++k)
                e.params.push_back ({ msIds[k], msNames[k], Kind::real, 10.0f, 4000.0f, 250.0f * (float) (k + 1), 400.0f, Smooth::log, "ms", "" });
            for (int k = 0; k < taps; ++k)
                e.params.push_back ({ levelIds[k], levelNames[k], Kind::real, 0.0f, 1.0f, levels[k], 0.0f, Smooth::linear, "%", "" });
            for (int k = 0; k < taps; ++k)
                e.params.push_back ({ panIds[k], panNames[k], Kind::real, -1.0f, 1.0f, pans[k], 0.0f, Smooth::linear, "%", "" });
            e.params.push_back ({ "tone",        "Tone",          Kind::real,   500.0f, 16000.0f, 6000.0f, 3000.0f, Smooth::log,    "Hz", "" });
            e.params.push_back ({ "feedback",    "Feedback",      Kind::real,   0.0f,   0.95f,    0.35f,   0.0f,    Smooth::linear, "%",  "" });
            e.params.push_back ({ "fb_source",   "Feedback From", Kind::choice, 0.0f,   1.0f,     0.0f,    0.0f,    Smooth::none,   "",   "Tap 5|Sum" });
            e.params.push_back ({ "motion",      "Motion",        Kind::real,   0.0f,   1.0f,     0.2f,    0.0f,    Smooth::linear, "%",  "" });
            e.params.push_back ({ "motion_rate", "Motion Rate",   Kind::real,   0.05f,  2.0f,     0.3f,    0.3f,    Smooth::log,    "Hz", "" });
            e.params.push_back ({ "damaged",     "Damaged Fifth", Kind::choice, 0.0f,   1.0f,     1.0f,    0.0f,    Smooth::none,   "",   "Off|On" });
            e.params.push_back ({ "pingpong",    "Ping-Pong",     Kind::choice, 0.0f,   1.0f,     0.0f,    0.0f,    Smooth::none,   "",   "Off|On" });
            e.params.push_back ({ "transmit",    "Transmit",      Kind::real,   0.0f,   1.0f,     0.0f,    0.0f,    Smooth::linear, "%",  "" });
            e.stages = { "taps" };
            e.presets = {
                { "Init", {} },
                { "Five Antennas", { { "mix", 0.3f }, { "pattern", 1.0f }, { "feedback", 0.4f }, { "motion", 0.25f }, { "output", -0.7f } } },
                { "Damaged Fifth", { { "mix", 0.3f }, { "pattern", 1.0f }, { "tap1_level", 0.3f }, { "tap2_level", 0.3f }, { "tap3_level", 0.3f },
                                     { "tap4_level", 0.3f }, { "tap5_level", 1.0f }, { "feedback", 0.55f }, { "tone", 4000.0f }, { "output", 0.6f } } },
                { "Steel Yard", { { "mix", 0.3f }, { "pattern", 2.0f }, { "feedback", 0.3f }, { "tone", 5000.0f }, { "motion", 0.15f }, { "output", -0.5f } } },
                { "Over the Horizon", { { "mix", 0.3f }, { "pattern", 0.0f }, { "tap1_sync", 8.0f }, { "tap2_sync", 11.0f }, { "feedback", 0.6f },
                                        { "fb_source", 1.0f }, { "pingpong", 1.0f }, { "tone", 3500.0f }, { "motion", 0.35f }, { "output", -0.6f } } },
                { "Woodpecker Echo", { { "mix", 0.3f }, { "tap1_sync", 6.0f }, { "tap2_sync", 3.0f }, { "tap3_sync", 9.0f }, { "tap4_sync", 2.0f },
                                       { "tap5_sync", 7.0f }, { "tap1_level", 0.8f }, { "tap2_level", 0.65f }, { "tap3_level", 0.5f },
                                       { "tap4_level", 0.4f }, { "tap5_level", 0.3f }, { "feedback", 0.3f }, { "motion", 0.0f },
                                       { "damaged", 0.0f }, { "tone", 7000.0f }, { "output", -0.9f } } },
                { "Clean Carrier", { { "mix", 0.3f }, { "damaged", 0.0f }, { "motion", 0.0f }, { "feedback", 0.3f }, { "tone", 9000.0f }, { "output", -1.8f } } },
            };
            return e;
        }();
        return i;
    }

    class RainbowChain final : public host::Chain
    {
    public:
        void prepare (double sampleRate, int) override
        {
            sr = (float) sampleRate;
            line.prepare ((int) std::ceil ((maxSeconds + 0.02f) * sr) + 8);
            for (auto& s : shift)
            {
                s.prepare (sr, 0.06f);
                s.setWindow (0.05f);
            }
            for (auto& b : bandHp) b.setHz (300.0f, true, sr);
            for (auto& b : bandLp) b.setHz (3000.0f, false, sr);
            glide = 1.0f - std::exp (-1.0f / (0.05f * sr));       // tap times glide like a tape head, ~50 ms
            fbHpCoef = dsp::onePoleCoefHz (120.0f, sr);
            detuneCoef = dsp::onePoleCoef (0.15f, sr);
            erraticCoef = dsp::onePoleCoef (0.02f, sr);
            loopDecay = std::exp (-1.0f / (0.3f * sr));
            reset();
        }

        void reset() override
        {
            line.reset();
            for (auto& s : shift) s.reset();
            for (auto& b : bandHp) b.reset();
            for (auto& b : bandLp) b.reset();
            tapLpL.fill (0.0f);
            tapLpR.fill (0.0f);
            current.fill (-1.0f);
            fbLpL = fbLpR = fbHpL = fbHpR = 0.0f;
            motionPhase = fastPhase = 0.0f;
            detune = detuneTarget = 0.0f;
            erratic = erraticTarget = 1.0f;
            lastStep = std::numeric_limits<int64_t>::min();
            loopPeak = 0.0f;
        }

        double tailSeconds (const float* v) const override
        {
            const float bpm = bpmNow.load();
            float longest = 0.0f;
            for (int k = 0; k < taps; ++k)
                longest = std::max (longest, tapSeconds (v, k, bpm));
            const float fbEff = std::clamp (v[feedback] + 0.3f * v[transmit], 0.0f, 0.95f);
            const float loop = v[fbSource] > 0.5f ? longest : tapSeconds (v, taps - 1, bpm);
            const float repeats = fbEff > 0.001f ? 60.0f / -dsp::gainToDb (fbEff) : 0.0f;
            return std::min (60.0, (double) (longest + repeats * loop + 0.3f));
        }

        void control (const float* v, const host::BlockContext& ctx, int) override
        {
            const auto& t = *ctx.transport;
            bpmNow.store ((float) t.bpm);
            for (int k = 0; k < taps; ++k)
            {
                target[(size_t) k] = tapSeconds (v, k, (float) t.bpm) * sr;
                level[(size_t) k] = v[tap1Level + k];
                dsp::panGains (v[tap1Pan + k], panL[(size_t) k], panR[(size_t) k]);
            }
            const float tx = v[transmit];
            const float toneHz = std::clamp (v[tone] * std::exp2 (1.5f * tx), 500.0f, 16000.0f);
            toneCoef = dsp::onePoleCoefHz (std::min (toneHz, 0.45f * sr), sr);
            fb = std::clamp (v[feedback] + 0.3f * tx, 0.0f, 0.95f);
            fromSum = v[fbSource] > 0.5f;
            motionDepth = std::clamp (v[motion] + 0.5f * tx, 0.0f, 1.0f);
            motionInc = v[motionRate] / sr;
            damagedOn = v[damaged] > 0.5f;
            pingPongOn = v[pingpong] > 0.5f;

            // The dead antenna's detune walk and erratic level: one decision per sixteenth note (the host's when it
            // plays, the sample clock's when it is stopped), hashed with the callsign so a render repeats exactly.
            const int64_t sixteenth = t.playing ? (int64_t) std::floor (t.ppq * 4.0)
                                                : ctx.sampleClock / std::max<int64_t> (1, (int64_t) (0.125f * sr));
            if (sixteenth != lastStep)
            {
                lastStep = sixteenth;
                const int64_t bar = sixteenth >= 0 ? sixteenth / 16 : (sixteenth - 15) / 16;
                const int step = (int) (sixteenth - bar * 16);
                detuneTarget = (dsp::hash01 (bar, step, 0xDE7Eu, ctx.callsign) * 2.0f - 1.0f) * 12.0f;
                const float h = dsp::hash01 (bar, step, 0xE77Au, ctx.callsign);
                erraticTarget = h < 0.15f ? 0.08f : 0.35f + 0.65f * h;   // now and then it nearly drops out
            }
        }

        void process (float* l, float* r, const float*, const float*, int n, host::Guard& guard) override
        {
            const float sweep = 0.006f * sr * motionDepth;
            const float wobble = 0.00012f * sr * motionDepth;
            const float fastInc = 6.3f / sr;
            float levelSum = 0.0f;
            for (const float g : level)
                levelSum += g;
            const float sumNorm = 1.0f / std::max (1.0f, levelSum);

            for (int i = 0; i < n; ++i)
            {
                motionPhase = dsp::wrap01 (motionPhase + motionInc);
                fastPhase = dsp::wrap01 (fastPhase + fastInc);
                detune += (detuneTarget - detune) * detuneCoef;
                erratic += (erraticTarget - erratic) * erraticCoef;

                float outL = 0.0f, outR = 0.0f, fbL = 0.0f, fbR = 0.0f;
                for (int k = 0; k < taps; ++k)
                {
                    auto& cur = current[(size_t) k];
                    if (cur < 0.0f)
                        cur = target[(size_t) k];
                    cur += (target[(size_t) k] - cur) * glide;
                    const float off = (float) k / (float) taps;
                    const float fast = dsp::sin2pi (fastPhase + off) * wobble;
                    const float dl = cur + sweep * (0.5f + 0.5f * dsp::sin2pi (motionPhase + off)) + fast;
                    const float dr = cur + sweep * (0.5f + 0.5f * dsp::sin2pi (motionPhase + off + 0.5f)) - fast;
                    float tl = line.readHermite (0, (double) std::max (1.0f, dl - 1.0f));
                    float tr = line.readHermite (1, (double) std::max (1.0f, dr - 1.0f));
                    if (k == taps - 1 && damagedOn)
                        damage (tl, tr);

                    if (fromSum)
                    {
                        fbL += tl * level[(size_t) k];
                        fbR += tr * level[(size_t) k];
                    }
                    else if (k == taps - 1)
                    {
                        fbL = tl;   // tap 5 before its level: LEVEL shapes what is heard, FEEDBACK what returns
                        fbR = tr;
                    }

                    tapLpL[(size_t) k] += (tl - tapLpL[(size_t) k]) * toneCoef;
                    tapLpR[(size_t) k] += (tr - tapLpR[(size_t) k]) * toneCoef;
                    outL += tapLpL[(size_t) k] * level[(size_t) k] * panL[(size_t) k];
                    outR += tapLpR[(size_t) k] * level[(size_t) k] * panR[(size_t) k];
                }
                if (fromSum)
                {
                    fbL *= sumNorm;   // the summed taps never return more than one tap would
                    fbR *= sumNorm;
                }

                // The feedback path: TONE low-pass, 120 Hz high-pass, soft limit.
                fbLpL += (fbL - fbLpL) * toneCoef;
                fbLpR += (fbR - fbLpR) * toneCoef;
                fbHpL += (fbLpL - fbHpL) * fbHpCoef;
                fbHpR += (fbLpR - fbHpR) * fbHpCoef;
                const float fl = dsp::fastTanh ((fbLpL - fbHpL) * fb * 1.1f) / 1.1f;
                const float fr = dsp::fastTanh ((fbLpR - fbHpR) * fb * 1.1f) / 1.1f;
                loopPeak = std::max (loopPeak * loopDecay, std::max (std::abs (fl), std::abs (fr)));

                if (pingPongOn)
                    line.push (0.5f * (l[i] + r[i]) + fr, fl);   // the repeats cross sides
                else
                    line.push (l[i] + fl, r[i] + fr);
                l[i] = outL;
                r[i] = outR;
            }
            guard (0, l, r, n);
        }

        void meter (host::Meters& m) const override { m.custom[0].store (dsp::gainToDb (std::max (loopPeak, 1.0e-4f))); }   // LOOP needle

    private:
        // DAMAGED: detune walk (+-12 ct), 300-3000 Hz band, light crush, -6 dB, erratic level.
        void damage (float& tl, float& tr) noexcept
        {
            const float ratio = dsp::MicroShift::centsToRatio (detune);
            tl = bandLp[0].process (bandHp[0].process (shift[0].process (tl, ratio)));
            tr = bandLp[1].process (bandHp[1].process (shift[1].process (tr, ratio)));
            const float g = 0.5f * erratic;
            tl = dsp::Crush::quantise (tl, 9.0f) * g;
            tr = dsp::Crush::quantise (tr, 9.0f) * g;
        }

        dsp::StereoDelayLine line;
        std::array<dsp::MicroShift, 2> shift;
        std::array<dsp::Butter4, 2> bandHp, bandLp;
        std::array<float, taps> target {}, current {}, level {}, panL {}, panR {}, tapLpL {}, tapLpR {};
        std::atomic<float> bpmNow { 120.0f };
        float sr = 48000.0f, glide = 0.0004f, toneCoef = 0.5f, fb = 0.35f, fbHpCoef = 0.01f;
        float fbLpL = 0.0f, fbLpR = 0.0f, fbHpL = 0.0f, fbHpR = 0.0f;
        float motionDepth = 0.2f, motionInc = 0.0f, motionPhase = 0.0f, fastPhase = 0.0f;
        float detune = 0.0f, detuneTarget = 0.0f, detuneCoef = 0.001f, erratic = 1.0f, erraticTarget = 1.0f, erraticCoef = 0.001f;
        float loopPeak = 0.0f, loopDecay = 0.9999f;
        bool fromSum = false, damagedOn = true, pingPongOn = false;
        int64_t lastStep = 0;
    };

    class RainbowProcessor final : public host::EffectProcessor
    {
    public:
        RainbowProcessor() : EffectProcessor (effectInfo(), std::make_unique<RainbowChain>()) {}
        juce::AudioProcessorEditor* createEditor() override;
    };

    class RainbowEditor final : public ui::EffectEditor
    {
    public:
        explicit RainbowEditor (RainbowProcessor& p) : EffectEditor (p)
        {
            static const char* titles[taps] = { "ANTENNA 1 // АНТЕННА 1", "ANTENNA 2 // АНТЕННА 2", "ANTENNA 3 // АНТЕННА 3",
                                                "ANTENNA 4 // АНТЕННА 4", "ANTENNA 5 // АНТЕННА 5" };
            static const char* syncIds[taps] = { "tap1_sync", "tap2_sync", "tap3_sync", "tap4_sync", "tap5_sync" };
            static const char* msIds[taps] = { "tap1_ms", "tap2_ms", "tap3_ms", "tap4_ms", "tap5_ms" };
            static const char* levelIds[taps] = { "tap1_level", "tap2_level", "tap3_level", "tap4_level", "tap5_level" };
            static const char* panIds[taps] = { "tap1_pan", "tap2_pan", "tap3_pan", "tap4_pan", "tap5_pan" };
            static const char* numbers[taps] = { "1", "2", "3", "4", "5" };

            std::vector<Panel> antennas;
            for (int k = 0; k < taps; ++k)
                antennas.push_back (panel (titles[k], { { choices ({ { syncIds[k], "TIME" } }), 1.25f }, { knob (msIds[k], "MS"), 1.0f } },
                                           k == taps - 1));
            addRow (std::move (antennas));

            std::vector<Item> levelItems, panItems;
            for (int k = 0; k < taps; ++k)
            {
                levelItems.push_back ({ knob (levelIds[k], numbers[k]), 1.0f });
                panItems.push_back ({ knob (panIds[k], numbers[k]), 1.0f });
            }
            addRow ({ panel ("LEVELS // УРОВНИ", std::move (levelItems)), panel ("PAN // ПАНОРАМА", std::move (panItems)) });

            needle = own (std::make_unique<ui::Needle> ("LOOP", 0.85f));
            addRow ({ panel ("SIGNAL // СИГНАЛ", { { choices ({ { "pattern", "PATTERN" }, { "fb_source", "FB FROM" } }), 1.3f },
                                                  { choices ({ { "damaged", "DAMAGED" }, { "pingpong", "PING-PONG" } }), 1.3f } }),
                      panel ("FEEDBACK // ПЕТЛЯ", { { knob ("feedback", "FEEDBACK"), 1.0f }, { knob ("tone", "TONE"), 1.0f } }),
                      panel ("MOTION // ДВИЖЕНИЕ", { { knob ("motion", "MOTION"), 1.0f }, { knob ("motion_rate", "RATE"), 1.0f } }),
                      panel ("TRANSMIT // ПЕРЕДАЧА", { { needle, 1.6f }, { knob ("transmit", "TRANSMIT", true), 1.0f } }, true),
                      outputPanel() });
            finish();
        }

    private:
        void refresh (float seconds) override
        {
            const float db = processor.meters.custom[0].load();
            needle->setReadout (db > -59.0f ? juce::String (db, 1) + " dB" : juce::String ("--"));
            needle->advance ((db + 60.0f) / 60.0f, seconds);
        }

        ui::Needle* needle = nullptr;
    };

    juce::AudioProcessorEditor* RainbowProcessor::createEditor() { return new RainbowEditor (*this); }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new RainbowProcessor();
}
