// ZEALOTS // ФАНАТИКИ — flocking simulation (Reynolds' boids: cohesion, separation, alignment). Monolith fighters
// chanting as one before the stone, scattering under fire. Up to sixteen agents read the input from one shared delay
// line: an agent's delay is its position, and the speed its read head moves is its velocity, heard as detune. ZEALOTRY
// trades separation and restlessness for cohesion: at 1 the flock converges into a chanting unison, at 0 it is a
// scattered crowd. A transient (the input's or the sidechain's) is the predator that scatters it.

#include "core/host/EffectProcessor.h"
#include "core/dsp/DelayLine.h"
#include "core/dsp/FormantBank.h"
#include "core/dsp/Generators.h"
#include "core/dsp/Onset.h"
#include "ui/EffectEditor.h"
#include "ui/Views.h"

namespace
{
    using namespace lab;
    using host::Kind;
    using host::Smooth;

    enum Param : int { agents = 0, zealotry, maxDetune, leaderDelay, spread, fear, predatorSource, chant };
    constexpr int maxAgents = 16;
    constexpr float minDelayMs = 2.0f, maxDelayMs = 80.0f, separationMs = 4.0f;

    const host::EffectInfo& effectInfo()
    {
        static const host::EffectInfo i = []
        {
            host::EffectInfo e;
            e.product = LABX16_PRODUCT;
            e.subtitle = "ФАНАТИКИ";
            e.slug = LABX16_SLUG;
            e.version = LABX16_VERSION;
            e.code = LABX16_CODE;
            e.faction = host::Faction::deepZone;
            e.sidechain = true;
            e.params = {
                { "agents",          "Agents",     Kind::integer, 2.0f, 16.0f,  12.0f, 0.0f,  Smooth::none,   "",   "" },
                { "zealotry",        "Zealotry",   Kind::real,    0.0f, 1.0f,   0.35f, 0.0f,  Smooth::linear, "%",  "" },
                { "max_detune",      "Max Detune", Kind::real,    0.0f, 100.0f, 40.0f, 30.0f, Smooth::linear, "ct", "" },
                { "leader_delay",    "Leader",     Kind::real,    5.0f, 40.0f,  20.0f, 0.0f,  Smooth::linear, "ms", "" },
                { "spread",          "Spread",     Kind::real,    0.0f, 1.0f,   0.8f,  0.0f,  Smooth::linear, "%",  "" },
                { "fear",            "Fear",       Kind::real,    0.0f, 1.0f,   0.5f,  0.0f,  Smooth::linear, "%",  "" },
                { "predator_source", "Predator",   Kind::choice,  0.0f, 2.0f,   1.0f,  0.0f,  Smooth::none,   "",   "Off|Input|Sidechain" },
                { "chant",           "Chant",      Kind::real,    0.0f, 1.0f,   0.25f, 0.0f,  Smooth::linear, "%",  "" },
            };
            e.stages = { "flock" };
            e.presets = {
                { "Init", {} },
                { "Chant", { { "zealotry", 0.85f }, { "chant", 0.65f }, { "agents", 12.0f }, { "output", -2.1f } } },
                { "Scattered Crowd", { { "zealotry", 0.0f }, { "max_detune", 55.0f }, { "spread", 1.0f }, { "output", -2.9f } } },
                { "Procession", { { "zealotry", 0.6f }, { "leader_delay", 30.0f }, { "max_detune", 25.0f }, { "output", -2.5f } } },
                { "Under Fire", { { "fear", 1.0f }, { "zealotry", 0.45f }, { "output", -2.7f } } },
                { "Monolith Wall", { { "agents", 16.0f }, { "zealotry", 1.0f }, { "chant", 0.5f }, { "spread", 1.0f }, { "output", -5.5f } } },
                { "Whisper Congregation", { { "agents", 8.0f }, { "zealotry", 0.3f }, { "chant", 0.85f }, { "max_detune", 20.0f }, { "output", -0.4f } } },
            };
            return e;
        }();
        return i;
    }

    class ZealotsChain final : public host::Chain
    {
    public:
        struct AgentView { std::atomic<float> pan { 0.0f }, delay { 0.0f }; };
        std::array<AgentView, maxAgents> uiAgents;
        std::atomic<int> uiCount { 12 };

        void prepare (double sampleRate, int) override
        {
            sr = (float) sampleRate;
            line.prepare ((int) std::ceil ((maxDelayMs + 4.0f) * 0.001f * sr));
            detector.prepare (sr);
            formants.prepare (sr);
            formants.set (0.25f, 0.0f);   // the vowel O
            agcCoef = dsp::onePoleCoef (0.3f, sr);
            scatter (callsign);
            reset();
        }

        void reset() override
        {
            line.reset();
            detector.reset();
            formants.reset();
            inPower = wetPower = 1.0e-6f;
            agcGain = 1.0f;
            scatter (callsign);
        }

        double tailSeconds (const float*) const override { return 0.1; }

        void control (const float* v, const host::BlockContext& ctx, int n) override
        {
            if (ctx.callsign != callsign)
            {
                callsign = ctx.callsign;
                scatter (callsign);
            }
            count = std::clamp ((int) std::lround (v[agents]), 2, maxAgents);
            const float z = v[zealotry];
            // At ZEALOTRY 0 only a faint pull keeps the crowd inside the line; at 1 the pull and the alignment are
            // strong enough to make the flock one voice. Cohesion grows with ZEALOTRY cubed (alignment squared): grown
            // linearly it pulled the crowd into unison by a third of the knob, leaving the rest of it in tune.
            const float kc = 0.05f + 5.45f * z * z * z, ks = 0.2f + 2.0f * (1.0f - z), align = 0.5f * z * z;
            const float limit = v[maxDetune];
            const float leader = v[leaderDelay];
            const float dt = (float) n / sr;
            const float relax = std::min (1.0f, dt / 0.3f);
            spreadNow = v[spread];
            chantNow = v[chant];
            predator = (int) std::lround (v[predatorSource]);
            sideActive = ctx.sidechainActive;

            float meanV = 0.0f;
            for (int i = 0; i < count; ++i)
                meanV += agentsState[(size_t) i].velocity;
            meanV /= (float) count;

            for (int i = 0; i < count; ++i)
            {
                auto& a = agentsState[(size_t) i];
                const float dMs = a.delay * 1000.0f / sr;
                // Cohesion toward the leader (a positive detune shortens the delay), alignment with the flock, a
                // restlessness of its own that ZEALOTRY quiets, and separation from any agent closer than 1.5 ms.
                float target = kc * (dMs - leader) * 2.0f + align * (meanV - a.velocity)
                             + (1.0f - z) * limit * a.walk.advance (n, 0.4f + 0.6f * (1.0f - z));
                for (int j = 0; j < count; ++j)
                {
                    if (j == i)
                        continue;
                    const float gap = dMs - agentsState[(size_t) j].delay * 1000.0f / sr;
                    if (std::abs (gap) < separationMs)
                        target -= ks * (gap >= 0.0f ? 1.0f : -1.0f) * (1.0f - std::abs (gap) / separationMs) * 30.0f;
                }
                a.velocity += (target - a.velocity) * relax;
                a.velocity = std::clamp (a.velocity, -limit, limit);
                a.panVelocity += (-kc * a.pan * 0.5f - a.panVelocity) * relax;
            }

            // The predator: a transient kicks every agent in a hashed direction.
            if (kickPending)
            {
                kickPending = false;
                ++kicks;
                for (int i = 0; i < count; ++i)
                {
                    auto& a = agentsState[(size_t) i];
                    a.velocity = std::clamp (a.velocity + v[fear] * (dsp::hash01 (kicks, i, 0xFEA2u, callsign) * 2.0f - 1.0f) * std::max (limit, 1.0f),
                                             -std::max (limit, 1.0f), std::max (limit, 1.0f));
                    a.panVelocity += v[fear] * (dsp::hash01 (kicks, 32 + i, 0xFEA2u, callsign) * 2.0f - 1.0f) * 2.0f;
                }
            }

            float lo = 1.0e9f, hi = -1.0e9f;
            for (int i = 0; i < count; ++i)
            {
                auto& a = agentsState[(size_t) i];
                a.rate = 1.0f - dsp::semisToRatio (a.velocity * 0.01f);   // delay change per sample
                a.pan = std::clamp (a.pan + a.panVelocity * dt, -1.0f, 1.0f);
                if (std::abs (a.pan) >= 1.0f)
                    a.panVelocity = -a.panVelocity * 0.5f;
                lo = std::min (lo, a.velocity);
                hi = std::max (hi, a.velocity);
                uiAgents[(size_t) i].pan.store (a.pan * spreadNow);
                uiAgents[(size_t) i].delay.store ((a.delay * 1000.0f / sr - minDelayMs) / (maxDelayMs - minDelayMs));
            }
            detuneSpread = hi - lo;
            uiCount.store (count);
        }

        void process (float* l, float* r, const float* sideL, const float* sideR, int n, host::Guard& guard) override
        {
            const float dMin = minDelayMs * 0.001f * sr, dMax = maxDelayMs * 0.001f * sr;
            std::array<float, maxAgents> gl {}, gr {};
            for (int k = 0; k < count; ++k)
                dsp::panGains (agentsState[(size_t) k].pan * spreadNow, gl[(size_t) k], gr[(size_t) k]);
            for (int i = 0; i < n; ++i)
            {
                const bool hit = predator == 1 ? detector.process (l[i], r[i])
                                               : (predator == 2 && sideActive ? detector.process (sideL[i], sideR[i]) : false);
                if (hit)
                    kickPending = true;
                line.push (0.5f * (l[i] + r[i]));
                float wl = 0.0f, wr = 0.0f;
                for (int k = 0; k < count; ++k)
                {
                    auto& a = agentsState[(size_t) k];
                    a.delay += a.rate;
                    if (a.delay < dMin) { a.delay = dMin; a.velocity = -std::abs (a.velocity); a.rate = -std::abs (a.rate); }
                    if (a.delay > dMax) { a.delay = dMax; a.velocity = std::abs (a.velocity); a.rate = std::abs (a.rate); }
                    const float x = line.readHermite (0, (double) a.delay);
                    wl += x * gl[(size_t) k];
                    wr += x * gr[(size_t) k];
                }
                // The flock's level follows the input's (a converged flock is coherent and would be far louder).
                inPower += (0.5f * (l[i] * l[i] + r[i] * r[i]) - inPower) * agcCoef;
                wetPower += (0.5f * (wl * wl + wr * wr) - wetPower) * agcCoef;
                const float wanted = std::clamp (std::sqrt ((inPower + 1.0e-9f) / (wetPower + 1.0e-9f)), 0.25f, 4.0f);
                agcGain += (wanted - agcGain) * agcCoef;
                wl *= agcGain * 0.9f;
                wr *= agcGain * 0.9f;
                if (chantNow > 1.0e-4f)
                {
                    wl += (formants.process (0, wl) - wl) * chantNow;
                    wr += (formants.process (1, wr) - wr) * chantNow;
                }
                l[i] += wl;
                r[i] += wr;
            }
            guard (0, l, r, n);
        }

        void meter (host::Meters& m) const override
        {
            m.custom[0].store (detuneSpread);
            m.custom[1].store ((float) kicks);
            m.custom[2].store (agcGain);
        }

    private:
        struct Agent
        {
            float delay = 480.0f, velocity = 0.0f, rate = 0.0f, pan = 0.0f, panVelocity = 0.0f;
            dsp::Programmer walk;
        };

        // The flock's starting places: around the leader, panned out (hash01 on the callsign).
        void scatter (uint32_t salt)
        {
            for (int i = 0; i < maxAgents; ++i)
            {
                auto& a = agentsState[(size_t) i];
                a.delay = (12.0f + 20.0f * (dsp::hash01 (i, 0, 0x2EA1u, salt) - 0.5f)) * 0.001f * sr;
                a.velocity = 0.0f;
                a.rate = 0.0f;
                a.pan = dsp::hash01 (i, 1, 0x2EA1u, salt) * 2.0f - 1.0f;
                a.panVelocity = 0.0f;
                a.walk.seed (dsp::hash32 (i, 2, 0x2EA1u, salt) | 1u);
                a.walk.reset (sr);
            }
        }

        std::array<Agent, maxAgents> agentsState;
        dsp::DelayLine line;
        dsp::OnsetDetector detector;
        dsp::FormantBank formants;
        float sr = 48000.0f, spreadNow = 0.8f, chantNow = 0.0f, agcCoef = 0.0001f, inPower = 1.0e-6f, wetPower = 1.0e-6f, agcGain = 1.0f;
        float detuneSpread = 0.0f;
        int count = 12, predator = 1;
        int64_t kicks = 0;
        bool kickPending = false, sideActive = false;
        uint32_t callsign = 0;
    };

    class ZealotsProcessor final : public host::EffectProcessor
    {
    public:
        ZealotsProcessor() : EffectProcessor (effectInfo(), std::make_unique<ZealotsChain>()) {}
        ZealotsChain& flock() { return static_cast<ZealotsChain&> (getChain()); }
        juce::AudioProcessorEditor* createEditor() override;
    };

    class ZealotsEditor final : public ui::EffectEditor
    {
    public:
        explicit ZealotsEditor (ZealotsProcessor& p) : EffectEditor (p), fx (p)
        {
            flockView = own (std::make_unique<ui::DotsView>());
            flockView->setAxes ("PAN", "DELAY");
            needle = own (std::make_unique<ui::Needle> ("SCATTER", 0.8f));
            addRow ({ panel ("FLOCK // СТАЯ", { { flockView, 4.0f }, { needle, 1.8f } }, true),
                      panel ("DOCTRINE // ДОКТРИНА", { { knob ("zealotry", "ZEALOTRY", true), 1.0f }, { knob ("agents", "AGENTS"), 1.0f } }) });
            addRow ({ panel ("MOTION // ДВИЖЕНИЕ", { { knob ("max_detune", "DETUNE"), 1.0f }, { knob ("leader_delay", "LEADER"), 1.0f },
                                                    { knob ("spread", "SPREAD"), 1.0f } }),
                      panel ("PREDATOR // ХИЩНИК", { { choices ({ { "predator_source", "SOURCE" } }), 1.3f }, { knob ("fear", "FEAR"), 1.0f } }),
                      panel ("CHANT // ПЕСНОПЕНИЕ", { { knob ("chant", "CHANT"), 1.0f } }) });
            addRow ({ outputPanel() });
            finish();
        }

    private:
        void refresh (float seconds) override
        {
            auto& f = fx.flock();
            std::vector<ui::DotsView::Dot> dots;
            const int count = f.uiCount.load();
            for (int i = 0; i < count; ++i)
                dots.push_back ({ f.uiAgents[(size_t) i].pan.load(), f.uiAgents[(size_t) i].delay.load() * 2.0f - 1.0f, 0.35f, i == 0 ? 2 : 1, {} });
            flockView->setDots (std::move (dots));
            const float spreadCents = processor.meters.custom[0].load();
            const float limit = std::max (1.0f, processor.effectRaw (Param::maxDetune));
            needle->setReadout (juce::String (juce::roundToInt (spreadCents)) + " ct");
            needle->advance (std::clamp (spreadCents / (2.0f * limit), 0.0f, 1.0f), seconds, 0.3f * spreadCents / limit);
        }

        ZealotsProcessor& fx;
        ui::DotsView* flockView = nullptr;
        ui::Needle* needle = nullptr;
    };

    juce::AudioProcessorEditor* ZealotsProcessor::createEditor() { return new ZealotsEditor (*this); }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ZealotsProcessor();
}
