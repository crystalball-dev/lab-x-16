// OASIS // ОАЗИС — the Oasis anomaly that heals. Restoration that undoes the Deep Zone: DECLIP redraws flat-topped runs
// (a 64-sample look-ahead sees a whole run before it plays), DEHISS learns each band's noise floor and expands below
// it, REGENERATE rebuilds the top octave from the band below, and SUB RETURN adds an octave under the bass.

#include "core/host/EffectProcessor.h"
#include "core/dsp/Crossovers.h"
#include "core/dsp/Filters.h"
#include "ui/EffectEditor.h"

namespace
{
    using namespace lab;
    using host::Kind;
    using host::Smooth;

    enum Param : int { heal = 0, declip, clipThr, dehiss, regen, regenFrom, subRegen };

    constexpr int lookahead = 64, ringSize = 256, maxRun = 48, numBands = 8;
    constexpr float splits[numBands - 1] = { 120.0f, 250.0f, 500.0f, 1000.0f, 2000.0f, 4000.0f, 8000.0f };

    const host::EffectInfo& effectInfo()
    {
        static const host::EffectInfo i = []
        {
            host::EffectInfo e;
            e.product = LABX16_PRODUCT;
            e.subtitle = "ОАЗИС";
            e.slug = LABX16_SLUG;
            e.version = LABX16_VERSION;
            e.code = LABX16_CODE;
            e.faction = host::Faction::etherealSignal;
            e.params = {
                { "heal",       "Heal",           Kind::real, 0.0f,    1.0f,     0.0f,    0.0f,    Smooth::linear, "%",  "" },
                { "declip",     "Declip",         Kind::real, 0.0f,    1.0f,     0.5f,    0.0f,    Smooth::linear, "%",  "" },
                { "clip_thr",   "Clip Threshold", Kind::real, 0.5f,    1.0f,     0.98f,   0.0f,    Smooth::linear, "x",  "" },
                { "dehiss",     "Dehiss",         Kind::real, 0.0f,    1.0f,     0.3f,    0.0f,    Smooth::linear, "%",  "" },
                { "regen",      "Regenerate",     Kind::real, 0.0f,    1.0f,     0.2f,    0.0f,    Smooth::linear, "%",  "" },
                { "regen_from", "Regen From",     Kind::real, 4000.0f, 12000.0f, 8000.0f, 7000.0f, Smooth::log,    "Hz", "" },
                { "sub_regen",  "Sub Return",     Kind::real, 0.0f,    1.0f,     0.0f,    0.0f,    Smooth::linear, "%",  "" },
            };
            e.stages = { "declip", "dehiss" };
            e.presets = {
                { "Init", {} },
                { "Heal", { { "heal", 1.0f }, { "output", 0.8f } } },
                { "Declip Only", { { "declip", 1.0f }, { "dehiss", 0.0f }, { "regen", 0.0f }, { "output", 0.0f } } },
                { "Dehiss Only", { { "declip", 0.0f }, { "dehiss", 0.9f }, { "regen", 0.0f }, { "output", 0.8f } } },
                { "Regenerate Air", { { "regen", 0.8f }, { "regen_from", 6000.0f }, { "dehiss", 0.2f }, { "output", 0.3f } } },
                { "Sub Return", { { "sub_regen", 0.7f }, { "dehiss", 0.0f }, { "regen", 0.0f }, { "output", -1.2f } } },
                { "Full Restoration", { { "heal", 0.7f }, { "sub_regen", 0.3f }, { "regen", 0.4f }, { "output", 0.5f } } },
            };
            return e;
        }();
        return i;
    }

    class OasisChain final : public host::Chain
    {
    public:
        void prepare (double sampleRate, int) override
        {
            sr = (float) sampleRate;
            peakDecay = std::exp (-1.0f / sr);   // the running peak (the clip level) falls 1/e per second
            for (int ch = 0; ch < 2; ++ch)
            {
                auto& c = chan[(size_t) ch];
                for (int k = 0; k < numBands - 1; ++k)
                {
                    c.xo[(size_t) k].set (splits[k], sr);
                    for (int j = 0; j < numBands - 1; ++j)
                        c.ap[(size_t) k][(size_t) j].set (dsp::SVF::gFor (splits[j], sr), 1.41421356f);
                }
                c.regenBand.setHz (3200.0f, 0.8f, sr);
                c.regenLp.setHz (16000.0f, 0.7071f, sr);
                c.subBand.setHz (100.0f, 0.9f, sr);
                c.subLp.setHz (120.0f, false, sr);
            }
            envCoef = dsp::onePoleCoef (0.01f, sr);
            floorRise = std::pow (10.0f, 1.0f / (20.0f * sr));     // 1 dB per second up, as amplitude
            floorFall = std::pow (10.0f, -40.0f / (20.0f * sr));   // 40 dB per second down
            gainOpen = dsp::onePoleCoef (0.002f, sr);
            gainClose = dsp::onePoleCoef (0.08f, sr);
            regenEnvCoef = dsp::onePoleCoef (0.005f, sr);
            subEnvCoef = dsp::onePoleCoef (0.02f, sr);
            treeStep = 1.0f / (0.02f * sr);
            reset();
        }

        void reset() override
        {
            for (auto& c : chan)
            {
                c.ring.fill (0.0f);
                c.runStart = -1;
                c.runLength = 0;
                c.pendingEnd = -1;
                c.longPlateau = false;
                c.peak = 0.0f;
                for (auto& x : c.xo) x.reset();
                for (auto& row : c.ap) for (auto& a : row) a.reset();
                c.env.fill (0.0f);
                c.floor.fill (1.0f);
                c.gainDb.fill (0.0f);
                c.regenBand.reset();
                c.regenLp.reset();
                c.regenHp.reset();
                c.regenEnv = 0.0f;
                c.subBand.reset();
                c.subLp.reset();
                c.subEnv = 0.0f;
                c.subSign = 1.0f;
                c.subPrev = 0.0f;
            }
            write = 0;
            treeMix = 0.0f;
            corrected = 0;
        }

        int latency (double) const override { return lookahead; }
        double tailSeconds (const float*) const override { return 0.05; }

        void control (const float* v, const host::BlockContext&, int) override
        {
            const float h = v[heal];
            declipNow = std::clamp (v[declip] + h, 0.0f, 1.0f);
            thrNow = v[clipThr];
            dehissNow = std::clamp (v[dehiss] + 0.8f * h, 0.0f, 1.0f);
            regenNow = std::clamp (v[regen] + 0.4f * h, 0.0f, 1.0f);
            subNow = v[subRegen];
            if (std::abs (v[regenFrom] - regenHz) > 1.0f)
            {
                regenHz = v[regenFrom];
                for (auto& c : chan)
                    c.regenHp.setHz (regenHz, true, sr);
            }
        }

        void process (float* l, float* r, const float*, const float*, int n, host::Guard& guard) override
        {
            float* io[2] = { l, r };
            // DECLIP: write at the head, correct runs in the not-yet-played past, read 64 samples back.
            for (int i = 0; i < n; ++i)
            {
                for (int ch = 0; ch < 2; ++ch)
                {
                    auto& c = chan[(size_t) ch];
                    const float x = io[ch][i];
                    c.ring[(size_t) (write & (ringSize - 1))] = x;
                    c.peak = std::max (std::abs (x), c.peak * peakDecay);
                    if (declipNow > 1.0e-4f)
                        track (c, x);
                    io[ch][i] = c.ring[(size_t) ((write - lookahead) & (ringSize - 1))];
                }
                ++write;
            }
            guard (0, l, r, n);

            // DEHISS (crossover tree), REGENERATE and SUB RETURN; each skipped entirely while it is off.
            const float treeTarget = dehissNow > 1.0e-3f ? 1.0f : 0.0f;
            for (int i = 0; i < n; ++i)
            {
                treeMix = treeTarget > treeMix ? std::min (1.0f, treeMix + treeStep) : std::max (0.0f, treeMix - treeStep);
                for (int ch = 0; ch < 2; ++ch)
                {
                    auto& c = chan[(size_t) ch];
                    const float x = io[ch][i];
                    float y = x;
                    if (treeMix > 0.0f)
                        y = x + (dehissBands (c, x) - x) * treeMix;
                    if (regenNow > 1.0e-4f)
                    {
                        const float b = c.regenBand.bandpass (x);
                        c.regenEnv += (b * b - c.regenEnv) * regenEnvCoef;
                        const float odd = b * b * b / (c.regenEnv * 3.0f + 1.0e-7f);   // harmonics at a level of their own
                        y += c.regenLp.lowpass (c.regenHp.process (std::clamp (odd, -2.0f, 2.0f))) * regenNow * 0.5f;
                    }
                    if (subNow > 1.0e-4f)
                    {
                        const float b = c.subBand.bandpass (x);
                        if (c.subPrev <= 0.0f && b > 0.0f)
                            c.subSign = -c.subSign;   // a flip-flop: half the frequency
                        c.subPrev = b;
                        c.subEnv += (std::abs (b) - c.subEnv) * subEnvCoef;
                        y += c.subLp.process (c.subSign * c.subEnv * 1.4f) * subNow;
                    }
                    io[ch][i] = y;
                }
            }
            guard (1, l, r, n);
        }

        void meter (host::Meters& m) const override
        {
            m.custom[0].store ((float) corrected);              // runs redrawn since prepare
            float worst = 0.0f;
            for (const auto& g : chan[0].gainDb)
                worst = std::min (worst, g);
            m.custom[1].store (worst);                          // deepest band reduction (dB)
        }

    private:
        struct Channel
        {
            std::array<float, ringSize> ring {};
            int64_t runStart = -1, pendingEnd = -1;
            int runLength = 0;
            bool longPlateau = false;
            float peak = 0.0f;
            std::array<dsp::LR4, numBands - 1> xo;
            std::array<std::array<dsp::SVF, numBands - 1>, numBands - 1> ap {};   // ap[band][split]: phase compensation
            std::array<float, numBands> env {}, floor {}, gainDb {};
            dsp::SVF regenBand, regenLp;
            dsp::Butter4 regenHp, subLp;
            dsp::SVF subBand;
            float regenEnv = 0.0f, subEnv = 0.0f, subSign = 1.0f, subPrev = 0.0f;
        };

        float at (const Channel& c, int64_t index) const noexcept { return c.ring[(size_t) (index & (ringSize - 1))]; }

        // A run: at least three samples at or above CLIP THRESHOLD x the running peak, flat (each within a hair of the
        // last) and of one sign. When it ends, one more sample is waited for, then the run is redrawn with a cubic
        // Hermite through the samples on either side and their slopes, so the missing peak comes back.
        void track (Channel& c, float x) noexcept
        {
            const int64_t w = write;
            if (c.pendingEnd >= 0 && w == c.pendingEnd + 1)
            {
                redraw (c, c.runStart, c.pendingEnd);
                c.pendingEnd = -1;
                c.runStart = -1;
                c.runLength = 0;
            }
            const float level = std::abs (x);
            const bool high = level >= thrNow * c.peak && level > 1.0e-3f;
            // A plateau longer than a run can be (a square wave, a brickwalled master) is not clipping to redraw: wait
            // until it ends before looking for runs again.
            if (c.longPlateau)
            {
                c.longPlateau = high;
                return;
            }
            bool continues = false;
            if (c.runStart >= 0 && c.pendingEnd < 0)
            {
                const float prev = at (c, w - 1);
                continues = high && (prev > 0.0f) == (x > 0.0f) && std::abs (x - prev) <= 1.0e-4f + 1.0e-3f * level;
            }
            if (c.pendingEnd >= 0)
                return;
            if (continues)
            {
                if (++c.runLength > maxRun)
                {
                    c.runStart = -1;   // too long to redraw in time: leave it
                    c.runLength = 0;
                    c.longPlateau = true;
                }
            }
            else
            {
                if (c.runStart >= 0 && c.runLength >= 3)
                    c.pendingEnd = w;   // w is the first sample after the run
                else
                {
                    c.runStart = high ? w : -1;
                    c.runLength = high ? 1 : 0;
                }
            }
        }

        void redraw (Channel& c, int64_t s, int64_t e) noexcept
        {
            // Samples s .. e-1 are the run; s-1 and e are the anchors, s-2 and e+1 give the slopes.
            if (e - s >= lookahead - 4 || s - 2 < write - ringSize + 8)
                return;
            const float p0 = at (c, s - 1), p1 = at (c, e);
            const float d = (float) (e - (s - 1));
            const float m0 = (p0 - at (c, s - 2)) * d, m1 = (at (c, e + 1) - p1) * d;
            for (int64_t j = s; j < e; ++j)
            {
                const float t = (float) (j - (s - 1)) / d, t2 = t * t, t3 = t2 * t;
                const float h = (2.0f * t3 - 3.0f * t2 + 1.0f) * p0 + (t3 - 2.0f * t2 + t) * m0 + (-2.0f * t3 + 3.0f * t2) * p1 + (t3 - t2) * m1;
                auto& cell = c.ring[(size_t) (j & (ringSize - 1))];
                cell += (std::clamp (h, -4.0f, 4.0f) - cell) * declipNow;
            }
            ++corrected;
        }

        // Eight bands from a Linkwitz-Riley tree, each made all-pass-equal to the others by the splits above it. Each
        // band's floor is learned (40 dB/s down, 1 dB/s up; digital silence teaches it nothing, or a song that starts
        // silent would leave it far below the hiss), and below floor + 6..12 dB the band is expanded 1:4, down to
        // 30 dB x DEHISS. The gain opens in 2 ms, so attacks pass, and closes over 80 ms.
        float dehissBands (Channel& c, float x) noexcept
        {
            float bands[numBands];
            float rest = x;
            for (int k = 0; k < numBands - 1; ++k)
            {
                float low, high;
                c.xo[(size_t) k].process (rest, low, high);
                for (int j = k + 1; j < numBands - 1; ++j)
                {
                    float lp, bp, hp;
                    c.ap[(size_t) k][(size_t) j].process (low, lp, bp, hp);
                    low = lp - 1.41421356f * bp + hp;
                }
                bands[k] = low;
                rest = high;
            }
            bands[numBands - 1] = rest;

            float y = 0.0f;
            const float offsetDb = 6.0f + 6.0f * dehissNow, rangeDb = 30.0f * dehissNow;
            for (int k = 0; k < numBands; ++k)
            {
                auto& env = c.env[(size_t) k];
                env += (bands[k] * bands[k] - env) * envCoef;
                const float level = std::sqrt (env);
                auto& fl = c.floor[(size_t) k];
                if (level > silentLevel)
                    fl = level < fl ? std::max (level, fl * floorFall) : fl * floorRise;
                const float overDb = dsp::gainToDb (level + 1.0e-9f) - (dsp::gainToDb (fl + 1.0e-9f) + offsetDb);
                const float target = overDb < 0.0f ? std::max (-rangeDb, 3.0f * overDb) : 0.0f;
                auto& g = c.gainDb[(size_t) k];
                g += (target - g) * (target > g ? gainOpen : gainClose);
                y += bands[k] * dsp::dbToGain (g);
            }
            return y;
        }

        std::array<Channel, 2> chan;
        float sr = 48000.0f, peakDecay = 0.9999f, declipNow = 0.5f, thrNow = 0.98f, dehissNow = 0.3f, regenNow = 0.2f, subNow = 0.0f;
        float regenHz = -1.0f, envCoef = 0.002f, floorRise = 1.0f, floorFall = 1.0f, gainOpen = 0.01f, gainClose = 0.0003f;
        static constexpr float silentLevel = 3.0e-6f;   // -110 dBFS
        float regenEnvCoef = 0.004f, subEnvCoef = 0.001f, treeMix = 0.0f, treeStep = 0.001f;
        int64_t write = 0, corrected = 0;
    };

    class OasisProcessor final : public host::EffectProcessor
    {
    public:
        OasisProcessor() : EffectProcessor (effectInfo(), std::make_unique<OasisChain>()) {}
        juce::AudioProcessorEditor* createEditor() override;
    };

    class OasisEditor final : public ui::EffectEditor
    {
    public:
        explicit OasisEditor (OasisProcessor& p) : EffectEditor (p)
        {
            needle = own (std::make_unique<ui::Needle> ("REDUCTION", 0.8f));
            addRow ({ panel ("HEAL // ИСЦЕЛЕНИЕ", { { needle, 1.8f }, { knob ("heal", "HEAL", true), 1.0f } }, true),
                      panel ("DECLIP // СРЕЗ", { { knob ("declip", "DECLIP"), 1.0f }, { knob ("clip_thr", "THRESHOLD"), 1.0f } }),
                      panel ("DEHISS // ШИПЕНИЕ", { { knob ("dehiss", "DEHISS"), 1.0f } }) });
            addRow ({ panel ("REGENERATE // ВОССТАНОВЛЕНИЕ", { { knob ("regen", "REGEN"), 1.0f }, { knob ("regen_from", "FROM"), 1.0f } }),
                      panel ("SUB RETURN // ВОЗВРАТ", { { knob ("sub_regen", "SUB"), 1.0f } }),
                      outputPanel() });
            finish();
        }

    private:
        void refresh (float seconds) override
        {
            const float db = processor.meters.custom[1].load();
            needle->setReadout (juce::String (db, 1) + " dB  " + juce::String (juce::roundToInt (processor.meters.custom[0].load())) + " CLIPS");
            needle->advance (std::clamp (-db / 30.0f, 0.0f, 1.0f), seconds);
        }

        ui::Needle* needle = nullptr;
    };

    juce::AudioProcessorEditor* OasisProcessor::createEditor() { return new OasisEditor (*this); }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new OasisProcessor();
}
