// EMISSION // ВЫБРОС — seismology and civil-defence timing: the sirens give a lead, the P-wave arrives, then the S-wave,
// then the aftermath. Emissions, sirens and radio warnings, shelters, and the Anabiotics that let a stalker sleep through
// one. One instance with ROLE Siren schedules an emission on FL's bar grid and posts it on the Zone Bus; every instance
// (the siren included) plays the same phases from its own song position, scaled by SEVERITY and by 1 - SHELTER. The
// phases are functions of the bar position, so a host loop or a render replays them exactly.

#include "core/host/EffectProcessor.h"
#include "core/dsp/Crush.h"
#include "core/dsp/Dynamics.h"
#include "core/dsp/Fdn.h"
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

    enum Param : int { role = 0, arm, countdown, lead, repeat, severity, pressureTime, aftermathTime, shelter, sirenLevel, anabiotics };
    enum Role : int { listener = 0, siren };
    enum Arm : int { armOff = 0, armCountdown, armAtBar };
    enum Phase : int { idle = 0, warning, pWave, sWave, aftermath };
    constexpr int repeatBars[6] = { 0, 8, 16, 32, 64, 128 };
    constexpr double pWaveSeconds = 0.12;

    const host::EffectInfo& effectInfo()
    {
        static const host::EffectInfo i = []
        {
            host::EffectInfo e;
            e.product = LABX16_PRODUCT;
            e.subtitle = "ВЫБРОС";
            e.slug = LABX16_SLUG;
            e.version = LABX16_VERSION;
            e.code = LABX16_CODE;
            e.faction = host::Faction::deepZone;
            e.busClient = true;
            e.params = {
                { "role",           "Role",        Kind::choice,  0.0f, 1.0f,   0.0f, 0.0f, Smooth::none,   "",   "Listener|Siren" },
                { "arm",            "Arm",         Kind::choice,  0.0f, 2.0f,   0.0f, 0.0f, Smooth::none,   "",   "Off|Countdown|At Bar" },
                { "countdown",      "Countdown",   Kind::integer, 1.0f, 64.0f,  8.0f, 0.0f, Smooth::none,   "",   "" },
                { "lead",           "Siren Lead",  Kind::integer, 1.0f, 8.0f,   2.0f, 0.0f, Smooth::none,   "",   "" },
                { "repeat",         "Repeat",      Kind::choice,  0.0f, 5.0f,   0.0f, 0.0f, Smooth::none,   "",   "Off|8|16|32|64|128" },
                { "severity",       "Severity",    Kind::real,    0.0f, 1.0f,   0.7f, 0.0f, Smooth::linear, "%",  "" },
                { "pressure_time",  "Pressure",    Kind::real,    0.5f, 8.0f,   2.5f, 2.5f, Smooth::none,   "s",  "" },
                { "aftermath_time", "Aftermath",   Kind::real,    0.5f, 16.0f,  4.0f, 4.0f, Smooth::none,   "s",  "" },
                { "shelter",        "Shelter",     Kind::real,    0.0f, 1.0f,   0.0f, 0.0f, Smooth::linear, "%",  "" },
                { "siren_level",    "Siren Level", Kind::real,    0.0f, 1.0f,   0.5f, 0.0f, Smooth::linear, "%",  "" },
                { "anabiotics",     "Anabiotics",  Kind::choice,  0.0f, 1.0f,   0.0f, 0.0f, Smooth::none,   "",   "Off|On" },
            };
            e.stages = { "program" };
            e.presets = {
                { "Init", {} },
                { "Standard Blowout", { { "severity", 0.7f } } },
                { "Shelter in Bunker", { { "shelter", 0.9f } } },
                { "Open Field", { { "shelter", 0.0f }, { "severity", 1.0f } } },
                { "Periodic Sixteen", { { "repeat", 2.0f }, { "severity", 0.6f } } },
                { "Anabiotics", { { "anabiotics", 1.0f } } },
                { "Severe Front", { { "pressure_time", 8.0f }, { "severity", 0.9f }, { "aftermath_time", 10.0f } } },
            };
            return e;
        }();
        return i;
    }

    using Schedule = host::bus::EmissionSchedule;

    // Where a position falls relative to the schedule: the phase and the seconds from the fire point.
    struct Position { Phase phase = idle; double rel = 0.0; double warn = 0.0; int64_t fireBar = -1; };

    Position locate (const Schedule& s, double q, const dsp::Transport& t) noexcept
    {
        Position out;
        if (s.seq == 0 || s.fireBar < 0)
            return out;
        const double qpb = t.quartersPerBar(), secPerQ = 60.0 / std::max (1.0, t.bpm);
        const double post = (double) s.pressureSeconds + (double) s.aftermathSeconds;
        auto relTo = [&] (int64_t fire) { return (q - (double) fire * qpb) * secPerQ; };
        int64_t fire = s.fireBar;
        if (s.repeatBars > 0 && q >= (double) s.fireBar * qpb)
        {
            // The latest fire point at or before now, or the next one if its warning has begun.
            const auto k = (int64_t) std::floor ((q / qpb - (double) s.fireBar) / (double) s.repeatBars);
            fire = s.fireBar + k * s.repeatBars;
            const int64_t next = fire + s.repeatBars;
            if (relTo (fire) > post && relTo (next) >= -(double) s.leadBars * qpb * secPerQ)
                fire = next;
        }
        const double rel = relTo (fire), leadSeconds = (double) s.leadBars * qpb * secPerQ;
        out.fireBar = fire;
        out.rel = rel;
        if (rel < 0.0 && rel >= -leadSeconds)
        {
            out.phase = warning;
            out.warn = 1.0 + rel / leadSeconds;   // 0 .. 1 toward the fire point
        }
        else if (rel >= 0.0 && rel < (double) s.pressureSeconds)
            out.phase = rel < pWaveSeconds ? pWave : sWave;
        else if (rel >= (double) s.pressureSeconds && rel < post)
            out.phase = aftermath;
        return out;
    }

    class EmissionChain final : public host::Chain
    {
    public:
        void prepare (double sampleRate, int) override
        {
            sr = (float) sampleRate;
            for (auto& h : warnHp) h.setHz (120.0f, 0.7071f, sr);
            follower.set (0.005f, 0.2f, sr);
            compressor.prepare (sr);
            sirenGen.prepare (sr, 0x51E1u);
            bloom.prepare (sr, 1.5f, 0.01f);
            bloomSettings.scale = 1.2f;
            bloomSettings.rt60 = 3.0f;
            bloomSettings.dampHz = 5000.0f;
            downStep = 1.0f / (0.02f * sr);
            upStep = 1.0f / (0.2f * sr);
            rng.seed (0xB0057u);
            reset();
        }

        void reset() override
        {
            for (auto& h : warnHp) h.reset();
            for (auto& f : lp) f.reset();
            follower.reset();
            compressor.reset();
            crush.reset();
            sirenGen.reset();
            bloom.reset();
            sleep = 1.0f;
        }

        double tailSeconds (const float*) const override { return 4.0; }

        void control (const float* v, const host::BlockContext& ctx, int) override
        {
            const auto& t = *ctx.transport;
            transport = t;
            depth = v[severity] * (1.0f - v[shelter]);
            sirenOn = (int) std::lround (v[role]) == siren;
            sirenGain = v[sirenLevel] * (1.0f - v[shelter]);
            anabioticsOn = v[anabiotics] > 0.5f;
            const int armMode = (int) std::lround (v[arm]);

            if (sirenOn)
            {
                // The siren's own schedule: At Bar fires at bar COUNTDOWN of the song (bar 1 = the first); Countdown fires
                // COUNTDOWN bars after the bar in which ARM switched on (an automation clip makes that repeatable).
                Schedule s = own;
                s.leadBars = (int32_t) std::lround (v[lead]);
                s.severity = v[severity];
                s.pressureSeconds = v[pressureTime];
                s.aftermathSeconds = v[aftermathTime];
                s.repeatBars = repeatBars[std::clamp ((int) std::lround (v[repeat]), 0, 5)];
                s.sirenId = ctx.callsign;
                if (armMode == armAtBar)
                    s.fireBar = (int64_t) std::lround (v[countdown]) - 1;
                else if (armMode == armCountdown)
                {
                    if (lastArm != armCountdown && t.playing)
                        s.fireBar = (int64_t) std::ceil (t.ppq / t.quartersPerBar() - 1.0e-6) + (int64_t) std::lround (v[countdown]);
                    else if (lastArm != armCountdown)
                        s.fireBar = -1;   // armed while stopped: waits for the transport
                    else if (s.fireBar < 0 && t.playing)
                        s.fireBar = (int64_t) std::ceil (t.ppq / t.quartersPerBar() - 1.0e-6) + (int64_t) std::lround (v[countdown]);
                }
                else
                    s.fireBar = -1;
                if (std::memcmp (&s, &own, sizeof (Schedule)) != 0 || own.seq == 0)
                {
                    s.seq = own.seq + 1;
                    own = s;
                    if (ctx.bus != nullptr)
                        ctx.bus->postSchedule (own);
                }
                else if (ctx.bus != nullptr && ++repost >= 1024)
                {
                    repost = 0;   // now and then, for instances that joined late
                    ctx.bus->postSchedule (own);
                }
                active = own;
            }
            else if (ctx.bus != nullptr)
            {
                Schedule s;
                if (ctx.bus->readSchedule (s))
                    active = s;
            }
            else
                active = {};
            lastArm = sirenOn ? armMode : armOff;

            // The siren tells the bus whenever a fire point is crossed (ARTIFACT reseeds from the schedule itself).
            const auto here = locate (active, t.ppq, t);
            if (sirenOn && t.playing && here.fireBar >= 0 && here.rel >= 0.0 && here.fireBar != lastFired && ctx.bus != nullptr)
            {
                lastFired = here.fireBar;
                ctx.bus->bumpReseed();
            }
            phaseNow = here.phase;
            relNow = here.rel;
            fireBarNow = here.fireBar;
            if (ctx.bus != nullptr)
                ctx.bus->publish (sirenOn ? siren : listener, (int32_t) phaseNow, 0.0f);
        }

        void process (float* l, float* r, const float*, const float*, int n, host::Guard& guard) override
        {
            const auto& t = transport;
            const double pressure = active.pressureSeconds, after = active.aftermathSeconds;
            const double quarterSeconds = 60.0 / std::max (1.0, t.bpm);
            std::array<float, host::controlBlock> bloomIn {};
            for (int i = 0; i < n; ++i)
            {
                const double q = t.ppq + (double) i * t.ppqPerSample;
                Position pos;
                if (t.playing)
                    pos = locate (active, q, t);
                const float env = follower.processStereo (l[i], r[i]);
                float a = l[i], b = r[i], extra = 0.0f;
                const float d = depth;
                if (pos.phase == warning)
                {
                    const float w = (float) pos.warn;
                    const float blend = w * d;   // the low end gives way as the siren rises
                    a += (warnHp[0].highpass (a) - a) * blend;
                    b += (warnHp[1].highpass (b) - b) * blend;
                    if (sirenOn)
                        extra += sirenGen.next (w) * sirenGain * w * 0.25f;
                }
                else
                {
                    warnHp[0].highpass (a);
                    warnHp[1].highpass (b);
                }
                if (pos.phase == pWave || pos.phase == sWave)
                {
                    const double rel = pos.rel;
                    if (rel < pWaveSeconds)
                    {
                        // The P-wave: +12 dB decaying over 60 ms, a white burst at the track's own level, 6-bit crush.
                        const float k = (float) (rel / pWaveSeconds);
                        const float g = dsp::dbToGain (12.0f * d * std::exp (-(float) rel / 0.06f));
                        a *= g;
                        b *= g;
                        const float burst = rng.bi() * 0.25f * d * std::exp (-(float) rel / 0.03f) * std::min (1.0f, env * 4.0f);
                        a += burst;
                        b += burst;
                        const float c = d * (1.0f - k);
                        a += (dsp::Crush::quantise (a, 6.0f) - a) * c;
                        b += (dsp::Crush::quantise (b, 6.0f) - b) * c;
                    }
                    // The S-wave: pressure. Low-pass from 20 kHz down to 400 Hz over the first 30 %, back over the rest,
                    // compression and a collapse to mono following it.
                    const double x = rel / std::max (0.01, pressure);
                    const float s = (float) (x < 0.3 ? smooth (x / 0.3) : smooth (1.0 - (x - 0.3) / 0.7)) * d;
                    lp[0].setHz (20000.0f * std::pow (0.02f, s), 0.7071f, sr);
                    lp[1].setHz (20000.0f * std::pow (0.02f, s), 0.7071f, sr);
                    const float blend = std::sqrt (s);
                    a += (lp[0].lowpass (a) - a) * blend;
                    b += (lp[1].lowpass (b) - b) * blend;
                    compressor.process (a, b, s);
                    const float mid = 0.5f * (a + b), side = 0.5f * (a - b) * (1.0f - s);
                    a = mid + side;
                    b = mid - side;
                }
                else
                {
                    lp[0].lowpass (a);
                    lp[1].lowpass (b);
                    float ca = a, cb = b;
                    compressor.process (ca, cb, 0.0f);
                }
                if (pos.phase == aftermath)
                {
                    // The aftermath: -18 dB recovering, and a bloom that rings out.
                    const float k = (float) ((pos.rel - pressure) / std::max (0.01, after));
                    const float fade = std::exp (-3.0f * k);
                    const float g = dsp::dbToGain (-18.0f * d * fade);
                    bloomIn[(size_t) i] = 0.5f * (a + b) * 0.5f * d * fade;
                    a *= g;
                    b *= g;
                }
                // ANABIOTICS: asleep from a quarter beat before the fire point to the end of the aftermath.
                const bool asleep = anabioticsOn && t.playing && pos.fireBar >= 0
                                    && pos.rel >= -0.25 * quarterSeconds && pos.rel < pressure + after;
                sleep = asleep ? std::max (0.0f, sleep - downStep) : std::min (1.0f, sleep + upStep);
                l[i] = (a + extra) * sleep;
                r[i] = (b + extra) * sleep;
            }
            std::array<float, host::controlBlock> bl {}, br {};
            bloomSettings.extraIn = bloomIn.data();
            bloom.process (bl.data(), br.data(), n, bloomSettings);
            for (int i = 0; i < n; ++i)
            {
                l[i] += bl[(size_t) i] * sleep;
                r[i] += br[(size_t) i] * sleep;
            }
            guard (0, l, r, n);
        }

        void meter (host::Meters& m) const override
        {
            m.custom[0].store ((float) phaseNow);
            m.custom[1].store ((float) relNow);
            m.custom[2].store ((float) fireBarNow);
            m.custom[3].store ((float) active.seq);
            m.custom[4].store (sleep);
            const double qpb = transport.quartersPerBar();
            m.custom[5].store (fireBarNow >= 0 ? (float) ((double) fireBarNow - transport.ppq / qpb) : -1.0f);
        }

    private:
        static double smooth (double x) noexcept { x = std::clamp (x, 0.0, 1.0); return x * x * (3.0 - 2.0 * x); }

        dsp::Transport transport;
        Schedule own {}, active {};
        std::array<dsp::SVF, 2> warnHp, lp;
        dsp::Follower follower;
        dsp::Compressor compressor;
        dsp::Crush crush;
        dsp::Siren sirenGen;
        dsp::Fdn bloom;
        dsp::Fdn::Settings bloomSettings;
        dsp::Rng rng;
        float sr = 48000.0f, depth = 0.7f, sirenGain = 0.5f, sleep = 1.0f, downStep = 0.001f, upStep = 0.0001f;
        bool sirenOn = false, anabioticsOn = false;
        int lastArm = armOff, repost = 0;
        int64_t lastFired = -1, fireBarNow = -1;
        Phase phaseNow = idle;
        double relNow = 0.0;
    };

    class EmissionProcessor final : public host::EffectProcessor
    {
    public:
        EmissionProcessor() : EffectProcessor (effectInfo(), std::make_unique<EmissionChain>()) {}
        juce::AudioProcessorEditor* createEditor() override;
    };

    class EmissionEditor final : public ui::EffectEditor
    {
    public:
        explicit EmissionEditor (EmissionProcessor& p) : EffectEditor (p)
        {
            countdownView = own (std::make_unique<ui::BigReadout>());
            needle = own (std::make_unique<ui::Needle> ("PRESSURE", 0.7f));
            addRow ({ panel ("COUNTDOWN // ОТСЧЁТ", { { countdownView, 2.6f }, { needle, 1.7f } }, true),
                      panel ("SIREN // СИРЕНА", { { choices ({ { "role", "ROLE" }, { "arm", "ARM" } }), 1.3f },
                                                 { knob ("countdown", "BARS"), 1.0f }, { knob ("lead", "LEAD"), 1.0f },
                                                 { choices ({ { "repeat", "REPEAT" } }), 1.1f } }) });
            addRow ({ panel ("WAVE // ВОЛНА", { { knob ("severity", "SEVERITY", true), 1.0f }, { knob ("pressure_time", "PRESSURE"), 1.0f },
                                               { knob ("aftermath_time", "AFTERMATH"), 1.0f }, { knob ("siren_level", "SIREN"), 1.0f } }),
                      panel ("SHELTER // УКРЫТИЕ", { { knob ("shelter", "SHELTER"), 1.0f }, { choices ({ { "anabiotics", "ANABIOTICS" } }), 1.2f } }) });
            addRow ({ outputPanel() });
            addSide (panel ("PEERS // СВЯЗЬ", { { peersView(), 1.0f } }));
            finish();
        }

    private:
        void refresh (float seconds) override
        {
            const auto& m = processor.meters;
            static const char* names[5] = { "QUIET", "SIREN", "P-WAVE", "S-WAVE", "AFTERMATH" };
            const int phase = std::clamp (juce::roundToInt (m.custom[0].load()), 0, 4);
            const float barsLeft = m.custom[5].load();
            const bool scheduled = m.custom[2].load() >= 0.0f && m.custom[3].load() > 0.0f;
            juce::String value;
            if (! scheduled)
                value = "NO EMISSION";
            else if (phase == 0 && barsLeft > 0.0f)
                value = juce::String (barsLeft, 1) + " BARS";
            else
                value = names[phase];
            countdownView->set (scheduled ? "EMISSION AT BAR " + juce::String (juce::roundToInt (m.custom[2].load()) + 1) : juce::String ("COUNTDOWN"),
                                value, phase >= 2);
            const float pressure = phase == 3 ? 1.0f : (phase == 2 ? 1.1f : (phase == 1 ? 0.4f : (phase == 4 ? 0.25f : 0.0f)));
            needle->setReadout (m.custom[4].load() < 0.5f ? juce::String ("ASLEEP") : juce::String (names[phase]));
            needle->advance (pressure, seconds, phase >= 2 ? 1.0f : 0.0f);
        }

        ui::BigReadout* countdownView = nullptr;
        ui::Needle* needle = nullptr;
    };

    juce::AudioProcessorEditor* EmissionProcessor::createEditor() { return new EmissionEditor (*this); }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new EmissionProcessor();
}
