// CLEAR SKY // ЧИСТОЕ НЕБО — meteorology as a slow modulation system: fronts, precipitation, wind, lightning and the
// day. The Zone's weather and the Clear Sky faction's name. WEATHER Auto walks a Markov chain of fronts every FRONT RATE
// bars; its rolls are hashes of the front's index alone, so every CLEAR SKY in a project sees the same sky, and a render
// repeats it. FOG darkens, RAIN falls as drops and hiss, WIND pans and swirls, LIGHTNING flashes the highs and its RUMBLE
// arrives distance / 343 m/s later, NIGHT dims the top octaves, and an EMISSION SKY hums at 50 Hz under a distant siren.

#include "core/host/EffectProcessor.h"
#include "core/dsp/Ensemble.h"
#include "core/dsp/Filters.h"
#include "core/dsp/Generators.h"
#include "ui/EffectEditor.h"
#include "ui/Views.h"

namespace
{
    using namespace lab;
    using host::Kind;
    using host::Smooth;

    enum Param : int { weather = 0, frontRate, dayLength, timeOfDay, fog, rain, wind, lightning, rumble, distance, front };
    enum Sky : int { clear = 0, overcast, rainy, storm, emissionSky, numSkies };
    constexpr int frontBars[8] = { 1, 2, 4, 8, 16, 32, 64, 128 };
    // Per sky: fog, rain, wind, storm, emission (0..1, scaled by the user's maxima).
    constexpr float skyTargets[numSkies][5] = {
        { 0.0f, 0.0f, 0.2f, 0.0f, 0.0f }, { 0.6f, 0.0f, 0.4f, 0.0f, 0.0f }, { 0.5f, 0.8f, 0.5f, 0.0f, 0.0f },
        { 0.7f, 1.0f, 0.9f, 1.0f, 0.0f }, { 0.3f, 0.0f, 0.6f, 0.3f, 1.0f } };
    constexpr float transitions[numSkies][numSkies] = {
        { 0.50f, 0.35f, 0.10f, 0.00f, 0.05f }, { 0.30f, 0.30f, 0.30f, 0.05f, 0.05f }, { 0.10f, 0.30f, 0.30f, 0.25f, 0.05f },
        { 0.05f, 0.20f, 0.45f, 0.25f, 0.05f }, { 0.30f, 0.40f, 0.10f, 0.10f, 0.10f } };
    constexpr const char* skyNames[numSkies] = { "CLEAR", "OVERCAST", "RAIN", "STORM", "EMISSION SKY" };

    const host::EffectInfo& effectInfo()
    {
        static const host::EffectInfo i = []
        {
            host::EffectInfo e;
            e.product = LABX16_PRODUCT;
            e.subtitle = "ЧИСТОЕ НЕБО";
            e.slug = LABX16_SLUG;
            e.version = LABX16_VERSION;
            e.code = LABX16_CODE;
            e.faction = host::Faction::etherealSignal;
            e.params = {
                { "weather",     "Weather",        Kind::choice, 0.0f, 5.0f,  0.0f,  0.0f,  Smooth::none,   "",   "Auto|Clear|Overcast|Rain|Storm|Emission Sky" },
                { "front_rate",  "Front Rate",     Kind::choice, 0.0f, 7.0f,  4.0f,  0.0f,  Smooth::none,   "",   "1 bar|2 bars|4 bars|8 bars|16 bars|32 bars|64 bars|128 bars" },
                { "day_length",  "Day Length",     Kind::real,   1.0f, 60.0f, 24.0f, 15.0f, Smooth::none,   "min", "" },
                { "time_of_day", "Time of Day",    Kind::real,   0.0f, 24.0f, 12.0f, 0.0f,  Smooth::none,   "h",   "" },
                { "fog",         "Fog",            Kind::real,   0.0f, 1.0f,  0.5f,  0.0f,  Smooth::linear, "%",  "" },
                { "rain",        "Rain",           Kind::real,   0.0f, 1.0f,  0.3f,  0.0f,  Smooth::linear, "%",  "" },
                { "wind",        "Wind",           Kind::real,   0.0f, 1.0f,  0.3f,  0.0f,  Smooth::linear, "%",  "" },
                { "lightning",   "Lightning",      Kind::real,   0.0f, 1.0f,  0.5f,  0.0f,  Smooth::linear, "%",  "" },
                { "rumble",      "Rumble",         Kind::real,   0.0f, 1.0f,  0.5f,  0.0f,  Smooth::linear, "%",  "" },
                { "distance",    "Storm Distance", Kind::real,   0.5f, 10.0f, 3.0f,  3.0f,  Smooth::none,   "km",  "" },
                { "front",       "Front",          Kind::real,   0.0f, 1.0f,  0.0f,  0.0f,  Smooth::linear, "%",  "" },
            };
            e.stages = { "sky" };
            e.presets = {
                { "Init", {} },
                { "Morning Clear", { { "weather", 1.0f }, { "time_of_day", 7.0f } } },
                { "Overcast Swamp", { { "weather", 2.0f }, { "fog", 0.7f } } },
                { "Rain at Yantar", { { "weather", 3.0f }, { "rain", 0.7f } } },
                { "Storm Front", { { "weather", 4.0f }, { "lightning", 0.8f }, { "rain", 0.8f }, { "distance", 1.5f } } },
                { "Night Watch", { { "weather", 2.0f }, { "time_of_day", 0.0f }, { "day_length", 60.0f } } },
                { "Emission Sky", { { "weather", 5.0f }, { "wind", 0.6f } } },
            };
            return e;
        }();
        return i;
    }

    class ClearSkyChain final : public host::Chain
    {
    public:
        void prepare (double sampleRate, int) override
        {
            sr = (float) sampleRate;
            ensemble.prepare (sr);
            glide = dsp::onePoleCoef (4.0f, sr);
            dropDecay = std::exp (-1.0f / (0.0006f * sr));
            for (auto& d : drops) d.band.setHz (3000.0f, 3.0f, sr);
            hissHp.setHz (2000.0f, 0.7071f, sr);
            whooshHp.setHz (300.0f, 0.7071f, sr);
            rumbleLp.setHz (120.0f, false, sr);
            crackHp.setHz (2000.0f, 0.7071f, sr);
            sirenGen.prepare (sr, 0xC1E5u);
            rng.seed (0x5C1Eu);
            reset();
        }

        void reset() override
        {
            ensemble.reset();
            for (auto& f : fogLp) f.reset();
            for (auto& s : nightShelf) s.reset();
            for (auto& s : flashShelf) s.reset();
            for (auto& d : drops) { d.band.reset(); d.env = 0.0f; }
            hissHp.reset();
            whooshHp.reset();
            rumbleLp.reset();
            crackHp.reset();
            sirenGen.reset();
            panPhase = humPhase = 0.0f;
            flashLeft = crackLeft = 0;
            rumblePending.fill (-1);
            rumbleVoices.fill (-1);
            nextRumble = std::numeric_limits<int64_t>::max();
        }

        double tailSeconds (const float*) const override { return 3.5; }

        void control (const float* v, const host::BlockContext& ctx, int n) override
        {
            clockNow = ctx.sampleClock;
            const auto& t = *ctx.transport;
            const double quarters = t.playing ? t.ppq : (double) ctx.sampleClock * t.ppqPerSample;
            const double secPerQ = 60.0 / std::max (1.0, t.bpm);

            // The sky: chosen, or the Markov chain's state for this front.
            const int choice = (int) std::lround (v[weather]);
            if (choice == 0)
            {
                const int bars = frontBars[std::clamp ((int) std::lround (v[frontRate]), 0, 7)];
                const auto k = (int64_t) std::floor (quarters / t.quartersPerBar() / (double) bars + 1.0e-9);
                skyNow = skyAt (std::max<int64_t> (0, k), v[front]);
            }
            else
                skyNow = choice - 1;
            const float coef = 1.0f - std::pow (1.0f - glide, (float) n);
            for (int c = 0; c < 5; ++c)
                current[(size_t) c] += (skyTargets[skyNow][c] - current[(size_t) c]) * coef;
            fogNow = current[0] * v[fog];
            rainNow = current[1] * v[rain];
            windNow = current[2] * v[wind];
            stormNow = std::clamp (current[3] + 0.5f * v[front] * current[1], 0.0f, 1.0f) * v[lightning];
            emissionNow = current[4];
            rumbleGain = v[rumble];

            // The day: TIME OF DAY plus the song's time over DAY LENGTH minutes while it plays.
            hours = v[timeOfDay] + (t.playing ? (float) (quarters * secPerQ / (v[dayLength] * 60.0) * 24.0) : 0.0f);
            hours = std::fmod (hours, 24.0f);
            night = 0.5f * (1.0f + std::cos (dsp::twoPi * hours / 24.0f));

            // Filters for this block.
            const float clearness = 1.0f - fogNow;
            fogBlend = std::sqrt (fogNow);
            for (auto& f : fogLp)
                f.setHz (std::min (20000.0f * (0.1f + 0.9f * clearness), 0.45f * sr), 0.7071f, sr);
            const float topDb = -3.0f * night + 3.0f * emissionNow - 2.0f * fogNow;
            for (auto& s : nightShelf)
                s.set (dsp::Biquad::highShelf, 4000.0f, 0.7071f, topDb, sr);
            nightActive = std::abs (topDb) > 0.01f;

            // LIGHTNING: a hash01 roll per block at storm x 0.2 flashes a second; its RUMBLE follows distance / 343 m/s.
            const float dt = (float) n / sr;
            if (stormNow > 0.0f && dsp::hash01 (ctx.sampleClock / 16, 0, 0xF1A5u, 0) < stormNow * 0.2f * dt)
            {
                flashLeft = (int) (0.08f * sr);
                crackLeft = (int) (0.03f * sr);
                lastFlash = ctx.sampleClock;
                if (firstFlash < 0)
                    firstFlash = ctx.sampleClock;
                // Every flash keeps its own thunder (a queue: flashes come faster than a far storm's rumble arrives).
                const int64_t arrival = ctx.sampleClock + (int64_t) std::llround (v[distance] * 1000.0f / 343.0f * sr);
                const auto slot = std::find (rumblePending.begin(), rumblePending.end(), (int64_t) -1);
                if (slot != rumblePending.end())
                {
                    *slot = arrival;
                    nextRumble = std::min (nextRumble, arrival);
                }
                ++flashes;
            }
            for (auto& s : flashShelf)
                s.set (dsp::Biquad::highShelf, 6000.0f, 0.7071f, 6.0f, sr);
        }

        void process (float* l, float* r, const float*, const float*, int n, host::Guard& guard) override
        {
            const float dropChance = rainNow * 300.0f / sr;
            const float panInc = (0.05f + 0.25f * windNow) / sr;
            for (int i = 0; i < n; ++i)
            {
                float a = l[i], b = r[i];
                // FOG
                if (fogBlend > 1.0e-4f)
                {
                    a += (fogLp[0].lowpass (a) - a) * fogBlend;
                    b += (fogLp[1].lowpass (b) - b) * fogBlend;
                }
                // NIGHT and the emission sky's tilt
                if (nightActive)
                {
                    a = nightShelf[0].process (a);
                    b = nightShelf[1].process (b);
                }
                // A flash brightens the highs for 80 ms.
                if (flashLeft > 0)
                {
                    --flashLeft;
                    a = flashShelf[0].process (a);
                    b = flashShelf[1].process (b);
                }
                // WIND pans the program around.
                if (windNow > 1.0e-4f)
                {
                    panPhase = dsp::wrap01 (panPhase + panInc);
                    float pl, pr;
                    dsp::panGains (dsp::sin2pi (panPhase) * windNow * 0.6f, pl, pr);
                    a *= pl;
                    b *= pr;
                }
                // The beds: drops and hiss, the whoosh, the crack, the rumble, the hum and the siren.
                float bed = 0.0f;
                if (rng.uni() < dropChance)
                {
                    auto& d = drops[(size_t) (nextDrop++ % drops.size())];
                    d.band.setHz (2000.0f + 4000.0f * rng.uni(), 3.0f, sr);
                    d.env = 0.5f + 0.5f * rng.uni();
                    ++dropCount;
                }
                for (auto& d : drops)
                    if (d.env > 1.0e-4f)
                    {
                        bed += d.band.bandpass (rng.bi() * d.env) * 0.15f;
                        d.env *= dropDecay;
                    }
                const float white = rng.bi();
                bed += hissHp.highpass (white) * rainNow * 0.01f;
                if (windNow > 1.0e-4f)
                {
                    gust += (rng.bi() - gust) * 0.0002f;
                    bed += whooshHp.highpass (rng.bi()) * windNow * 0.02f * (0.5f + std::abs (gust) * 20.0f);
                }
                if (crackLeft > 0)
                {
                    --crackLeft;
                    bed += crackHp.highpass (white) * 0.2f * stormNow;
                }
                if (clockNow + i >= nextRumble)
                    startRumbles (clockNow + i);
                if (rumbling)
                {
                    // Up to four rumbles overlap; each swells over 200 ms and dies away over 3 s.
                    float env = 0.0f;
                    rumbling = false;
                    for (auto& age : rumbleVoices)
                        if (age >= 0)
                        {
                            const float t = (float) age++ / sr;
                            env += std::min (1.0f, t / 0.2f) * std::exp (-t / 0.8f);
                            if (t > 3.0f)
                                age = -1;
                            else
                                rumbling = true;
                        }
                    bed += rumbleLp.process (rng.bi()) * std::min (env, 1.2f) * rumbleGain * 0.6f;
                }
                if (emissionNow > 1.0e-3f)
                {
                    humPhase = dsp::wrap01 (humPhase + 50.0f / sr);
                    bed += (dsp::sin2pi (humPhase) + 0.4f * dsp::sin2pi (2.0f * humPhase) + 0.2f * dsp::sin2pi (3.0f * humPhase)) * emissionNow * 0.02f;
                    bed += sirenGen.next (0.5f) * emissionNow * 0.03f;
                }
                l[i] = a + bed;
                r[i] = b + bed;
            }
            if (windNow > 1.0e-4f)
                ensemble.process (l, r, n, windNow * 0.4f, windNow, 0.3f);
            guard (0, l, r, n);
        }

        void meter (host::Meters& m) const override
        {
            m.custom[0].store ((float) skyNow);
            m.custom[1].store (hours);
            m.custom[2].store (night);
            m.custom[3].store ((float) dropCount);
            m.custom[4].store ((float) firstFlash);    // the first flash and the first rumble (samples), for the
            m.custom[5].store ((float) firstRumble);   // distance test; -1 = none yet
            m.custom[6].store ((float) flashes);
        }

    private:
        struct Drop { dsp::SVF band; float env = 0.0f; };

        // The front's state: the chain walked from CLEAR at front 0, one hash01 roll per front (cached as it advances;
        // a jump back walks it again). FRONT leans every transition toward rain and storm.
        int skyAt (int64_t k, float lean) noexcept
        {
            if (k < walkedTo || std::abs (lean - walkedLean) > 1.0e-3f)
            {
                walkedTo = 0;
                walkedSky = clear;
                walkedLean = lean;
            }
            while (walkedTo < k)
            {
                ++walkedTo;
                float row[numSkies], sum = 0.0f;
                for (int s = 0; s < numSkies; ++s)
                {
                    row[s] = transitions[walkedSky][s] * ((s == rainy || s == storm) ? 1.0f + 3.0f * lean : 1.0f);
                    sum += row[s];
                }
                float roll = dsp::hash01 (walkedTo, 0, 0xC10Du, 0) * sum;
                int next = numSkies - 1;
                for (int s = 0; s < numSkies; ++s)
                {
                    if (roll < row[s]) { next = s; break; }
                    roll -= row[s];
                }
                walkedSky = next;
            }
            return walkedSky;
        }

        dsp::Ensemble ensemble;
        std::array<dsp::SVF, 2> fogLp;
        std::array<dsp::Biquad, 2> nightShelf, flashShelf;
        std::array<Drop, 6> drops {};
        dsp::SVF hissHp, whooshHp, crackHp;
        dsp::Butter4 rumbleLp;
        dsp::Siren sirenGen;
        dsp::Rng rng;
        std::array<float, 5> current {};
        float sr = 48000.0f, glide = 0.0001f, dropDecay = 0.9f, fogNow = 0.0f, rainNow = 0.0f, windNow = 0.0f, stormNow = 0.0f, emissionNow = 0.0f;
        float rumbleGain = 0.5f, hours = 12.0f, night = 0.0f, fogBlend = 0.0f, panPhase = 0.0f, humPhase = 0.0f, gust = 0.0f, walkedLean = 0.0f;
        bool nightActive = false, rumbling = false;
        int skyNow = clear, walkedSky = clear, flashLeft = 0, crackLeft = 0;
        size_t nextDrop = 0;
        int64_t clockNow = 0, lastFlash = -1, lastRumble = -1, firstFlash = -1, firstRumble = -1, flashes = 0, dropCount = 0, walkedTo = 0;
        std::array<int64_t, 16> rumblePending = idle<16>();   // each flash's thunder arrival (sample clock), -1 = free
        std::array<int64_t, 4> rumbleVoices = idle<4>();      // the age of each sounding rumble (samples), -1 = silent

        template <size_t N> static std::array<int64_t, N> idle() noexcept { std::array<int64_t, N> a {}; a.fill (-1); return a; }
        int64_t nextRumble = std::numeric_limits<int64_t>::max();

        void startRumbles (int64_t now) noexcept
        {
            nextRumble = std::numeric_limits<int64_t>::max();
            for (auto& arrival : rumblePending)
            {
                if (arrival < 0)
                    continue;
                if (now >= arrival)
                {
                    arrival = -1;
                    auto voice = std::find (rumbleVoices.begin(), rumbleVoices.end(), (int64_t) -1);
                    if (voice == rumbleVoices.end())
                        voice = std::max_element (rumbleVoices.begin(), rumbleVoices.end());   // the oldest gives way
                    *voice = 0;
                    rumbling = true;
                    lastRumble = now;
                    if (firstRumble < 0)
                        firstRumble = now;
                }
                else
                    nextRumble = std::min (nextRumble, arrival);
            }
        }
    };

    class ClearSkyProcessor final : public host::EffectProcessor
    {
    public:
        ClearSkyProcessor() : EffectProcessor (effectInfo(), std::make_unique<ClearSkyChain>()) {}
        juce::AudioProcessorEditor* createEditor() override;
    };

    class ClearSkyEditor final : public ui::EffectEditor
    {
    public:
        explicit ClearSkyEditor (ClearSkyProcessor& p) : EffectEditor (p)
        {
            weatherView = own (std::make_unique<ui::BigReadout>());
            needle = own (std::make_unique<ui::Needle> ("SUN", 0.85f));
            addRow ({ panel ("FORECAST // ПРОГНОЗ", { { weatherView, 3.0f }, { needle, 1.8f } }, true),
                      panel ("FRONT // ФРОНТ", { { choices ({ { "weather", "WEATHER" }, { "front_rate", "FRONTS" } }), 1.5f },
                                                { knob ("front", "FRONT", true), 1.0f } }) });
            addRow ({ panel ("ELEMENTS // СТИХИИ", { { knob ("fog", "FOG"), 1.0f }, { knob ("rain", "RAIN"), 1.0f }, { knob ("wind", "WIND"), 1.0f } }),
                      panel ("STORM // ГРОЗА", { { knob ("lightning", "LIGHTNING"), 1.0f }, { knob ("rumble", "RUMBLE"), 1.0f },
                                                { knob ("distance", "DISTANCE"), 1.0f } }),
                      panel ("DAY // СУТКИ", { { knob ("day_length", "DAY MIN"), 1.0f }, { knob ("time_of_day", "HOUR"), 1.0f } }) });
            addRow ({ outputPanel() });
            finish();
        }

    private:
        void refresh (float seconds) override
        {
            const auto& m = processor.meters;
            const int sky = std::clamp (juce::roundToInt (m.custom[0].load()), 0, numSkies - 1);
            const float h = m.custom[1].load();
            const int hh = (int) h, mm = (int) ((h - (float) hh) * 60.0f);
            weatherView->set ("ZONE TIME " + juce::String (hh).paddedLeft ('0', 2) + ":" + juce::String (mm).paddedLeft ('0', 2),
                              skyNames[sky], sky >= storm);
            needle->setReadout (m.custom[2].load() > 0.5f ? juce::String ("NIGHT") : juce::String ("DAY"));
            needle->advance (1.0f - m.custom[2].load(), seconds);
        }

        ui::BigReadout* weatherView = nullptr;
        ui::Needle* needle = nullptr;
    };

    juce::AudioProcessorEditor* ClearSkyProcessor::createEditor() { return new ClearSkyEditor (*this); }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ClearSkyProcessor();
}
