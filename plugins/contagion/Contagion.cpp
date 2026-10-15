// CONTAGION // ЗАРАЖЕНИЕ — epidemiology, the SIR compartment model. Zombified stalkers spreading out of Yantar:
// "Everyone who is affected by it loses his mind and turns into a zombie" (Sakharov); the psi-helmet as immunity.
// Every instance is a stalker: Susceptible, Infected or Recovered. Infection enters by ROLE Patient Zero, by the field
// source (the track itself by default) loud enough over a whole bar, or from infected peers on the Zone Bus; an infected
// instance degrades its audio through incubation, fever and zombification, then recovers, stays immune for WANE bars and
// can catch it again.
//
// Determinism: decisions happen only at bar boundaries, from the loads every peer published for the previous bar
// (one load per bar parity on the bus), with hash01 rolls on the bar and the callsign. Every instance starts healthy
// when the transport starts, so an FL render of the song repeats the outbreak exactly. The course counts the bars
// played, so a looping pattern carries it on instead of starting it over.

#include "core/host/EffectProcessor.h"
#include "core/dsp/Crush.h"
#include "core/dsp/DelayLine.h"
#include "core/dsp/Dynamics.h"
#include "core/dsp/Filters.h"
#include "core/dsp/Follower.h"
#include "core/dsp/Generators.h"
#include "ui/EffectEditor.h"
#include "ui/Views.h"

namespace
{
    using namespace lab;
    using host::Kind;
    using host::Smooth;

    enum Param : int { role = 0, r0, immunity, incubation, recovery, wane, symptoms, stageCurve, fieldSource, threshold, cure };
    enum Role : int { stalker = 0, patientZero, carrier };
    enum Sir : int { susceptible = 0, infected, recovered, carrying };
    const uint32_t contagionCode = host::bus::packCode ("L6Co");

    const host::EffectInfo& effectInfo()
    {
        static const host::EffectInfo i = []
        {
            host::EffectInfo e;
            e.product = LABX16_PRODUCT;
            e.subtitle = "ЗАРАЖЕНИЕ";
            e.slug = LABX16_SLUG;
            e.version = LABX16_VERSION;
            e.code = LABX16_CODE;
            e.faction = host::Faction::deepZone;
            e.sidechain = true;
            e.busClient = true;
            e.params = {
                { "role",         "Role",            Kind::choice,  0.0f,   2.0f,  0.0f,   0.0f, Smooth::none,   "",   "Stalker|Patient Zero|Carrier" },
                { "r0",           "R0",              Kind::real,    0.0f,   8.0f,  2.0f,   0.0f, Smooth::none,   "",   "" },
                { "immunity",     "Immunity",        Kind::real,    0.0f,   1.0f,  0.0f,   0.0f, Smooth::none,   "%",  "" },
                { "incubation",   "Incubation",      Kind::integer, 0.0f,   8.0f,  1.0f,   0.0f, Smooth::none,   "",   "" },
                { "recovery",     "Recovery",        Kind::integer, 1.0f,   32.0f, 8.0f,   0.0f, Smooth::none,   "",   "" },
                { "wane",         "Wane",            Kind::integer, 0.0f,   64.0f, 8.0f,   0.0f, Smooth::none,   "",   "" },
                { "symptoms",     "Symptoms",        Kind::real,    0.0f,   1.0f,  1.0f,   0.0f, Smooth::linear, "%",  "" },
                { "stage_curve",  "Course",          Kind::choice,  0.0f,   2.0f,  1.0f,   0.0f, Smooth::none,   "",   "Linear|Fever|Collapse" },
                { "field_source", "Field Source",    Kind::choice,  0.0f,   2.0f,  1.0f,   0.0f, Smooth::none,   "",   "None|Input|Sidechain" },
                { "threshold",    "Field Threshold", Kind::real,    -60.0f, 0.0f,  -30.0f, 0.0f, Smooth::none,   "dB", "" },
                { "cure",         "Cure",            Kind::choice,  0.0f,   1.0f,  0.0f,   0.0f, Smooth::none,   "",   "Off|On" },
            };
            e.stages = { "drift", "choke", "grit" };
            e.presets = {
                { "Init", {} },
                { "Patient Zero", { { "role", 1.0f } } },
                { "Yantar Outbreak", { { "r0", 4.0f }, { "incubation", 0.0f }, { "recovery", 6.0f } } },
                { "Slow Zombification", { { "incubation", 1.0f }, { "recovery", 16.0f }, { "stage_curve", 2.0f } } },
                { "Psi-helmet Issued", { { "immunity", 0.15f }, { "symptoms", 0.45f }, { "recovery", 4.0f }, { "incubation", 0.0f } } },
                { "Fever Dream", { { "incubation", 0.0f }, { "recovery", 4.0f }, { "wane", 2.0f }, { "stage_curve", 0.0f } } },
                { "Pandemic R8", { { "r0", 8.0f }, { "incubation", 0.0f }, { "wane", 0.0f } } },
            };
            return e;
        }();
        return i;
    }

    class ContagionChain final : public host::Chain
    {
    public:
        void prepare (double sampleRate, int) override
        {
            sr = (float) sampleRate;
            wobble.prepare ((int) std::ceil (0.03f * sr) + 4);
            field.set (0.005f, 0.12f, sr);
            inputEnv.set (0.005f, 0.12f, sr);
            grit.prepare (sr);
            symptomCoef = dsp::onePoleCoef (0.05f, sr);
            reset();
            infectNow = false;
        }

        void reset() override
        {
            wobble.reset();
            for (auto& f : choke) f.reset();
            field.reset();
            inputEnv.reset();
            grit.reset();
            crush.reset();
            whisperGen.reset();
            amplitude = 0.0f;
        }

        double tailSeconds (const float*) const override { return 0.1; }

        void control (const float* v, const host::BlockContext& ctx, int n) override
        {
            if (ctx.callsign != callsign)
            {
                callsign = ctx.callsign;
                walk.seed (dsp::hash32 (0, 0, 0xC0DEu, callsign) | 1u);
                walk.reset (sr);
                whisperGen.prepare (sr, dsp::hash32 (0, 1, 0xC0DEu, callsign));
                whisperGen.setFormant (150.0f);
            }
            p = v;
            sideActive = ctx.sidechainActive;
            const auto& t = *ctx.transport;
            const int roleNow = (int) std::lround (v[role]);
            const bool cureNow = v[cure] > 0.5f;

            // The transport starting begins a fresh outbreak: everyone healthy. (A loop's jump back does not: the course
            // goes on through it.)
            if (t.playing && ! wasPlaying)
            {
                state = roleNow == carrier ? carrying : susceptible;
                lastBar = std::numeric_limits<int64_t>::min();
                barsPlayed = 0;
                eventBar = 0;
                fieldSum = 0.0;
                fieldCount = 0;
                infectNow = roleNow == patientZero;
            }
            if (roleNow == patientZero && lastRole != patientZero)
                infectNow = true;   // the edge
            if (roleNow == carrier)
                state = carrying;
            else if (state == carrying)
                state = susceptible;
            if (cureNow && ! lastCure && state != carrying)
            {
                state = susceptible;
                infectNow = false;
            }
            lastRole = roleNow;
            lastCure = cureNow;
            wasPlaying = t.playing;
            lastQ = t.ppq;

            if (t.playing)
            {
                const int64_t bar = t.bar();
                if (bar != lastBar)
                {
                    if (lastBar != std::numeric_limits<int64_t>::min())
                        ++barsPlayed;
                    boundary (bar, v, ctx);
                    lastBar = bar;
                }
                const double intoBar = t.ppq / t.quartersPerBar();
                barsSince = (double) (barsPlayed - eventBar) + (intoBar - std::floor (intoBar));
            }

            // Symptoms follow the course continuously through the bar.
            target = symptomLevel() * v[symptoms];
            juce::ignoreUnused (n);
        }

        void process (float* l, float* r, const float* sideL, const float* sideR, int n, host::Guard& guard) override
        {
            if (p == nullptr)
                return;
            const int source = (int) std::lround (p[fieldSource]);
            const float coef = 1.0f - std::pow (1.0f - symptomCoef, (float) n);
            const float sStart = sNow;
            sNow += (target - sNow) * coef;
            const float s = sNow;

            // The field source's level through the bar (the lowest it fell), for infection by exposure.
            for (int i = 0; i < n; ++i)
            {
                float fe = 0.0f;
                if (source == 1)
                    fe = field.processStereo (l[i], r[i]);
                else if (source == 2 && sideActive)
                    fe = field.processStereo (sideL[i], sideR[i]);
                fieldSum += (double) fe * (double) fe;
                ++fieldCount;
            }

            // DRIFT: a wandering delay (wow); at no symptoms the delay is exactly 0 and the sound untouched.
            const float wob = walk.advance (n, 0.6f);
            const float targetAmp = s * 0.012f * sr;
            const float ampStep = (targetAmp - amplitude) / (float) n;
            float lastDelay = delayNow;
            for (int i = 0; i < n; ++i)
            {
                amplitude += ampStep;
                const float frac = (float) (i + 1) / (float) n;
                const float u = wobPrev + (wob - wobPrev) * frac;
                delayNow = amplitude * (1.0f + u);
                wobble.push (l[i], r[i]);
                l[i] = wobble.readLinear (0, delayNow);
                r[i] = wobble.readLinear (1, delayNow);
            }
            wobPrev = wob;
            centsNow = 1731.0f * (delayNow - lastDelay) / (float) n;
            maxCents = std::max (maxCents, std::abs (centsNow));
            guard (0, l, r, n);

            // CHOKE: a low-pass from 20 kHz down to 1.2 kHz, blended in by sqrt(s) so no symptoms is no filter.
            const float chokeHz = 20000.0f * std::pow (1200.0f / 20000.0f, std::sqrt (s));
            choke[0].setHz (chokeHz, 0.7071f, sr);
            choke[1].setHz (chokeHz, 0.7071f, sr);
            const float blend = std::sqrt (std::max (s, sStart));
            for (int i = 0; i < n; ++i)
            {
                l[i] += (choke[0].lowpass (l[i]) - l[i]) * blend;
                r[i] += (choke[1].lowpass (r[i]) - r[i]) * blend;
            }
            guard (1, l, r, n);

            // GRIT (parallel tube above 300 Hz), CRUSH, WHISPER and the level drop.
            grit.process (l, r, n, dsp::ParallelGrit::tube, s, 24.0f * s, 300.0f);
            crush.process (l, r, n, 16.0f - 10.0f * s, 1.0f + 7.0f * s);
            bitsNow = 16.0f - 10.0f * s;
            const float level = dsp::dbToGain (-3.0f * s);
            const float whisperGain = s * 0.3f;
            for (int i = 0; i < n; ++i)
            {
                const float env = inputEnv.processStereo (l[i], r[i]);
                const float w = whisperGain > 1.0e-4f ? whisperGen.next() * whisperGain * env : 0.0f;
                l[i] = l[i] * level + w;
                r[i] = r[i] * level + w;
            }
            guard (2, l, r, n);
        }

        void meter (host::Meters& m) const override
        {
            m.custom[0].store ((float) state);
            m.custom[1].store (sNow);
            m.custom[2].store (loadNow);
            m.custom[3].store ((float) infections);
            m.custom[4].store ((float) (state == infected ? eventBar : -1));
            m.custom[5].store (bitsNow);
            m.custom[6].store (maxCents);
            m.custom[7].store ((float) barsSince);
        }

    private:
        // At a bar boundary: recover, wane, catch it from the bar before, publish this bar's load.
        void boundary (int64_t bar, const float* v, const host::BlockContext& ctx)
        {
            const int inc = (int) std::lround (v[incubation]), rec = std::max (1, (int) std::lround (v[recovery]));
            const int waneBars = (int) std::lround (v[wane]);
            if (state == infected && barsPlayed - eventBar >= inc + rec)
            {
                state = recovered;
                eventBar = barsPlayed;
            }
            else if (state == recovered && waneBars > 0 && barsPlayed - eventBar >= waneBars)
                state = susceptible;

            if (state == susceptible)
            {
                float exposure = 0.0f;
                if (ctx.bus != nullptr)
                {
                    std::array<host::bus::PeerView, host::bus::maxPeers> peers {};
                    const int count = std::min (ctx.bus->peers (peers.data(), (int) peers.size()), (int) peers.size());
                    for (int k = 0; k < count; ++k)
                    {
                        const auto& pv = peers[(size_t) k];
                        if (pv.code != contagionCode)
                            continue;
                        const int64_t peerBar = pv.state >> 3;
                        if (peerBar < (bar - 1) % (int64_t) (1 << 27))
                            continue;   // not playing along
                        exposure += ((bar - 1) & 1) == 0 ? pv.load : pv.loadOdd;
                    }
                }
                const float pInfect = (1.0f - v[immunity]) * (1.0f - std::exp (-(v[r0] / (float) rec) * exposure));
                // The field: its mean level over the bar just played above THRESHOLD; IMMUNITY resists it too.
                const double fieldDb = fieldCount > 0 ? 10.0 * std::log10 (fieldSum / (double) fieldCount + 1.0e-12) : -120.0;
                const bool fromField = (int) std::lround (v[fieldSource]) != 0 && lastBar != std::numeric_limits<int64_t>::min()
                                       && fieldDb > (double) v[threshold]
                                       && dsp::hash01 (bar, 1, 0x1F3C7u, ctx.callsign) >= v[immunity];
                if (infectNow || fromField || (pInfect > 0.0f && dsp::hash01 (bar, 0, 0x1F3C7u, ctx.callsign) < pInfect))
                {
                    state = infected;
                    eventBar = barsPlayed;
                    ++infections;
                }
            }
            infectNow = false;
            fieldSum = 0.0;
            fieldCount = 0;
            incubationBars = inc;
            recoveryBars = rec;
            curve = (int) std::lround (v[stageCurve]);

            // This bar's load.
            loadNow = 0.0f;
            if (state == carrying)
                loadNow = 1.0f;
            else if (state == infected)
            {
                const int64_t t = barsPlayed - eventBar;
                if (t < inc)
                    loadNow = 0.5f * (float) t / (float) std::max (1, inc);
                else if ((double) t < inc + 0.6 * rec)
                    loadNow = 1.0f;
                else
                    loadNow = 0.7f;
            }
            if (ctx.bus != nullptr)
                ctx.bus->publishParity ((int32_t) std::lround (v[role]), (int32_t) ((bar % (1 << 27)) << 3) | state, bar, loadNow);
        }

        // 0..1 along the course, from the bars since infection (fractional, so it moves smoothly through each bar).
        float symptomLevel() const noexcept
        {
            if (state == infected)
            {
                const double t = barsSince;
                const double inc = incubationBars, fever = 0.6 * recoveryBars;
                if (t < inc)
                    return 0.0f;
                if (t < inc + fever)
                {
                    const float x = (float) ((t - inc) / fever);
                    switch (curve)
                    {
                        case 0:  return x;
                        case 2:  return x * x * x;                       // collapse: late and sudden
                        default: return x * x * (3.0f - 2.0f * x);       // fever: a rise and a plateau
                    }
                }
                return 1.0f;
            }
            if (state == recovered)
                return (float) std::clamp (1.0 - barsSince, 0.0, 1.0);   // a bar to come back
            return 0.0f;
        }

        dsp::StereoDelayLine wobble;
        std::array<dsp::SVF, 2> choke;
        dsp::Follower field, inputEnv;
        dsp::ParallelGrit grit;
        dsp::Crush crush;
        dsp::Whisper whisperGen;
        dsp::Programmer walk;
        const float* p = nullptr;
        float sr = 48000.0f, sNow = 0.0f, target = 0.0f, symptomCoef = 0.001f, amplitude = 0.0f, delayNow = 0.0f, wobPrev = 0.0f;
        float loadNow = 0.0f, bitsNow = 16.0f, centsNow = 0.0f, maxCents = 0.0f;
        double fieldSum = 0.0;
        int64_t fieldCount = 0, barsPlayed = 0;
        int state = susceptible, lastRole = stalker, incubationBars = 1, recoveryBars = 8, curve = 1;
        int64_t lastBar = std::numeric_limits<int64_t>::min(), eventBar = 0, infections = 0;
        double barsSince = 0.0, lastQ = 0.0;
        bool wasPlaying = false, infectNow = false, lastCure = false, sideActive = false;
        uint32_t callsign = 0;
    };

    class ContagionProcessor final : public host::EffectProcessor
    {
    public:
        ContagionProcessor() : EffectProcessor (effectInfo(), std::make_unique<ContagionChain>()) {}
        juce::AudioProcessorEditor* createEditor() override;
    };

    class ContagionEditor final : public ui::EffectEditor
    {
    public:
        explicit ContagionEditor (ContagionProcessor& p) : EffectEditor (p), fx (p)
        {
            map = own (std::make_unique<ui::DotsView>());
            stage = own (std::make_unique<ui::BigReadout>());
            addRow ({ panel ("OUTBREAK // ВСПЫШКА", { { map, 2.2f }, { stage, 2.4f } }, true),
                      panel ("SPREAD // РАСПРОСТРАНЕНИЕ", { { choices ({ { "role", "ROLE" }, { "cure", "CURE" } }), 1.4f },
                                                           { knob ("r0", "R0"), 1.0f }, { knob ("immunity", "IMMUNITY"), 1.0f } }) });
            addRow ({ panel ("COURSE // ТЕЧЕНИЕ", { { knob ("incubation", "INCUBATION"), 1.0f }, { knob ("recovery", "RECOVERY"), 1.0f },
                                                   { knob ("wane", "WANE"), 1.0f }, { choices ({ { "stage_curve", "COURSE" } }), 1.2f } }),
                      panel ("SYMPTOMS // СИМПТОМЫ", { { knob ("symptoms", "SYMPTOMS", true), 1.0f } }, true),
                      panel ("FIELD // ПОЛЕ", { { choices ({ { "field_source", "SOURCE" } }), 1.3f }, { knob ("threshold", "THRESHOLD"), 1.0f } }) });
            addRow ({ outputPanel() });
            addSide (panel ("PEERS // СВЯЗЬ", { { peersView(), 1.0f } }));
            finish();
        }

    private:
        void refresh (float) override
        {
            const auto& m = processor.meters;
            const int st = juce::roundToInt (m.custom[0].load());
            const float s = m.custom[1].load();
            static const char* names[4] = { "SUSCEPTIBLE", "INFECTED", "RECOVERED", "CARRIER" };
            juce::String detail = names[std::clamp (st, 0, 3)];
            if (st == 1)
                detail = s < 0.01f ? juce::String ("INCUBATING") : (s > 0.99f ? juce::String ("ZOMBIFIED") : "FEVER " + juce::String (juce::roundToInt (s * 100.0f)) + "%");
            stage->set ("STAGE  //  LOAD " + juce::String (m.custom[2].load(), 2), detail, st == 1 || st == 3);

            // The map: this instance in the centre, the bus's CONTAGION peers around it.
            std::array<host::bus::PeerView, 16> others {};
            const int count = std::min (fx.getBusClient().peers (others.data(), (int) others.size()), (int) others.size());
            std::vector<ui::DotsView::Dot> dots;
            auto kindFor = [] (int sir) { return sir == 1 ? 2 : (sir == 2 ? 3 : (sir == 3 ? 2 : 1)); };
            dots.push_back ({ 0.0f, 0.0f, 1.0f, kindFor (st), fx.getCallsign() });
            int k = 0;
            for (int j = 0; j < count; ++j)
            {
                if (others[(size_t) j].code != host::bus::packCode ("L6Co"))
                    continue;
                const float a = juce::MathConstants<float>::twoPi * (float) k++ / (float) std::max (1, count);
                dots.push_back ({ 0.7f * std::cos (a), 0.7f * std::sin (a), 0.5f, kindFor (others[(size_t) j].state & 7),
                                  host::bus::unpackCallsign (others[(size_t) j].callsign) });
            }
            map->setDots (std::move (dots));
        }

        ContagionProcessor& fx;
        ui::DotsView* map = nullptr;
        ui::BigReadout* stage = nullptr;
    };

    juce::AudioProcessorEditor* ContagionProcessor::createEditor() { return new ContagionEditor (*this); }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ContagionProcessor();
}
