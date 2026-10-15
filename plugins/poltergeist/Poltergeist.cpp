// POLTERGEIST // ПОЛТЕРГЕЙСТ — rigid-body physics: gravity, restitution and Doppler. Poltergeists hurling crates across
// the X-labs; the burer's telekinesis. An onset throws an object carrying 80 ms of itself into a small room (x across
// the stereo field, y height, z depth). It falls, bounces off the floor and the walls, and every floor impact replays
// its sound: louder for a harder impact, panned where it lands, darker and quieter further away, Doppler-shifted by its
// speed toward or away from the listener (an exaggerated speed of sound), brighter higher up. RAGE adds telekinetic
// lifts. Throws and lifts are hash01 decisions, so a render repeats exactly.

#include "core/host/EffectProcessor.h"
#include "core/dsp/Filters.h"
#include "core/dsp/Onset.h"
#include "ui/EffectEditor.h"
#include "ui/Views.h"

namespace
{
    using namespace lab;
    using host::Kind;
    using host::Smooth;

    enum Param : int { throwSpeed = 0, gravity, bounce, life, objects, doppler, depthFilter, rage, sensitivity };
    constexpr int maxObjects = 12, maxReplays = 32;
    constexpr double captureSeconds = 0.08;
    constexpr float roomHeight = 3.0f, startHeight = 1.0f, soundSpeed = 40.0f;
    constexpr float earPosition[3] = { 0.0f, 1.5f, -1.0f };   // x, y, z: in front of the room, at ear height

    const host::EffectInfo& effectInfo()
    {
        static const host::EffectInfo i = []
        {
            host::EffectInfo e;
            e.product = LABX16_PRODUCT;
            e.subtitle = "ПОЛТЕРГЕЙСТ";
            e.slug = LABX16_SLUG;
            e.version = LABX16_VERSION;
            e.code = LABX16_CODE;
            e.faction = host::Faction::deepZone;
            e.params = {
                { "throw",        "Throw",       Kind::real,    0.0f, 1.0f,  0.8f, 0.0f, Smooth::none,   "%",   "" },
                { "gravity",      "Gravity",     Kind::real,    1.0f, 30.0f, 9.8f, 9.8f, Smooth::none,   "m/s2", "" },
                { "bounce",       "Bounce",      Kind::real,    0.0f, 0.95f, 0.7f, 0.0f, Smooth::none,   "%",   "" },
                { "life",         "Life",        Kind::real,    0.5f, 8.0f,  4.0f, 3.0f, Smooth::none,   "s",   "" },
                { "objects",      "Objects",     Kind::integer, 1.0f, 12.0f, 8.0f, 0.0f, Smooth::none,   "",    "" },
                { "doppler",      "Doppler",     Kind::real,    0.0f, 1.0f,  0.8f, 0.0f, Smooth::none,   "%",   "" },
                { "depth_filter", "Depth",       Kind::real,    0.0f, 1.0f,  0.6f, 0.0f, Smooth::none,   "%",   "" },
                { "rage",         "Rage",        Kind::real,    0.0f, 1.0f,  0.0f, 0.0f, Smooth::linear, "%",   "" },
                { "sensitivity",  "Sensitivity", Kind::real,    0.0f, 1.0f,  0.5f, 0.0f, Smooth::linear, "%",   "" },
            };
            e.stages = { "objects" };
            e.presets = {
                { "Init", {} },
                { "Lab X-18 Lobby", { { "objects", 10.0f }, { "bounce", 0.65f }, { "depth_filter", 0.8f }, { "output", -3.1f } } },
                { "Thrown Crates", { { "throw", 1.0f }, { "bounce", 0.5f }, { "life", 3.0f }, { "output", -2.7f } } },
                { "Burer Lift", { { "rage", 0.8f }, { "throw", 0.6f }, { "output", -3.3f } } },
                { "Low Gravity", { { "gravity", 2.0f }, { "life", 7.0f }, { "throw", 0.5f }, { "output", -0.7f } } },
                { "Rubber Room", { { "bounce", 0.95f }, { "life", 6.0f }, { "output", -4.4f } } },
                { "Rage", { { "rage", 1.0f }, { "objects", 12.0f }, { "throw", 1.0f }, { "output", -2.8f } } },
            };
            return e;
        }();
        return i;
    }

    class PoltergeistChain final : public host::Chain
    {
    public:
        struct ObjectView { std::atomic<float> x { 0.0f }, y { 0.0f }, z { 0.0f }; std::atomic<bool> alive { false }; };
        std::array<ObjectView, maxObjects> uiObjects;

        void prepare (double sampleRate, int) override
        {
            sr = (float) sampleRate;
            detector.prepare (sr);
            capture = (int) std::ceil (captureSeconds * sampleRate);
            for (auto& o : bodies)
            {
                o.l.assign ((size_t) capture + 4, 0.0f);
                o.r.assign ((size_t) capture + 4, 0.0f);
            }
            reset();
        }

        void reset() override
        {
            detector.reset();
            for (auto& o : bodies)
                o.alive = false;
            for (auto& rp : replays)
                rp.active = false;
            throws = lifts = impacts = 0;
            dopplerUp = dopplerDown = dopplerWrong = 0;
        }

        double tailSeconds (const float* v) const override { return (double) v[life] + 0.2; }

        void control (const float* v, const host::BlockContext& ctx, int n) override
        {
            callsign = ctx.callsign;
            clockNow = ctx.sampleClock;
            detector.setSensitivity (v[sensitivity]);
            throwNow = v[throwSpeed];
            maxCount = std::clamp ((int) std::lround (v[objects]), 1, maxObjects);
            lifeSamples = (int64_t) (v[life] * sr);
            dopplerNow = v[doppler];
            depthNow = v[depthFilter];
            const float g = v[gravity], restitution = v[bounce];
            const float dt = (float) n / sr;

            // RAGE: telekinetic lifts, about rage x 2 a second, a hash01 roll per block.
            const bool lift = v[rage] > 0.0f && dsp::hash01 (blockCount, 0, 0x1F7u, callsign) < v[rage] * 2.0f * dt;
            if (lift)
                ++lifts;

            for (int k = 0; k < maxObjects; ++k)
            {
                auto& o = bodies[(size_t) k];
                if (! o.alive)
                {
                    uiObjects[(size_t) k].alive.store (false);
                    continue;
                }
                if (lift)
                {
                    o.v[1] += 5.0f;
                    o.v[0] += (dsp::hash01 (lifts, k, 0x1F7Au, callsign) * 2.0f - 1.0f) * 2.0f;
                    o.v[2] += (dsp::hash01 (lifts, 16 + k, 0x1F7Au, callsign) * 2.0f - 1.0f) * 0.5f;
                }
                o.v[1] -= g * dt;
                for (int a = 0; a < 3; ++a)
                    o.p[a] += o.v[a] * dt;
                // Walls: x in -1..1, z in 0..1, the ceiling at 3 m.
                if (o.p[0] < -1.0f || o.p[0] > 1.0f) { o.p[0] = std::clamp (o.p[0], -1.0f, 1.0f); o.v[0] = -o.v[0] * restitution; }
                if (o.p[2] < 0.0f || o.p[2] > 1.0f)  { o.p[2] = std::clamp (o.p[2], 0.0f, 1.0f);  o.v[2] = -o.v[2] * restitution; }
                if (o.p[1] > roomHeight)             { o.p[1] = roomHeight; o.v[1] = -std::abs (o.v[1]) * restitution; }
                // The floor: an impact replays the object's sound.
                if (o.p[1] <= 0.0f)
                {
                    const float speed = -o.v[1];
                    o.p[1] = 0.0f;
                    o.v[1] = speed * restitution;
                    o.v[0] *= 0.9f;
                    o.v[2] *= 0.9f;
                    if (speed > 0.3f)
                        replay (k, speed);
                    if (o.v[1] < 0.2f && std::hypot (o.v[0], o.v[2]) < 0.2f)
                        o.alive = false;   // come to rest
                }
                o.age += n;
                if (o.age > lifeSamples)
                    o.alive = false;
                uiObjects[(size_t) k].x.store (o.p[0]);
                uiObjects[(size_t) k].y.store (o.p[1] / roomHeight);
                uiObjects[(size_t) k].z.store (o.p[2]);
                uiObjects[(size_t) k].alive.store (o.alive);
            }
            ++blockCount;
        }

        void process (float* l, float* r, const float*, const float*, int n, host::Guard& guard) override
        {
            for (int i = 0; i < n; ++i)
            {
                if (detector.process (l[i], r[i]))
                    spawn();
                for (auto& o : bodies)
                    if (o.alive && o.captured < capture)
                    {
                        o.l[(size_t) o.captured] = l[i];
                        o.r[(size_t) o.captured] = r[i];
                        ++o.captured;
                    }

                float wl = 0.0f, wr = 0.0f;
                for (auto& rp : replays)
                {
                    if (! rp.active)
                        continue;
                    const auto& o = bodies[(size_t) rp.object];
                    const int k = (int) rp.pos;
                    if (k + 1 >= o.captured || rp.serial != o.serial)
                    {
                        rp.active = false;
                        continue;
                    }
                    const float f = (float) (rp.pos - (double) k);
                    float a = (o.l[(size_t) k] + f * (o.l[(size_t) k + 1] - o.l[(size_t) k])) * rp.gain;
                    float b = (o.r[(size_t) k] + f * (o.r[(size_t) k + 1] - o.r[(size_t) k])) * rp.gain;
                    const float fadeIn = std::min (1.0f, (float) rp.age / 24.0f);
                    const float fadeOut = std::min (1.0f, (float) (o.captured - 1 - k) / 64.0f);
                    a *= fadeIn * fadeOut;
                    b *= fadeIn * fadeOut;
                    rp.lpL += (a - rp.lpL) * rp.lpCoef;
                    rp.lpR += (b - rp.lpR) * rp.lpCoef;
                    const float hl = rp.shelfL.process (rp.lpL), hr = rp.shelfR.process (rp.lpR);
                    wl += hl * rp.panL;
                    wr += hr * rp.panR;
                    rp.pos += rp.ratio;
                    ++rp.age;
                }
                l[i] += wl;
                r[i] += wr;
            }
            guard (0, l, r, n);
        }

        void meter (host::Meters& m) const override
        {
            m.custom[0].store ((float) throws);
            m.custom[1].store ((float) impacts);
            m.custom[2].store ((float) dopplerUp);
            m.custom[3].store ((float) dopplerDown);
            m.custom[4].store ((float) dopplerWrong);
            m.custom[5].store ((float) lifts);
            m.custom[6].store (firstImpact);
            m.custom[7].store (secondImpact);
        }

    private:
        struct Body
        {
            std::vector<float> l, r;
            float p[3] { 0.0f, startHeight, 0.3f }, v[3] { 0.0f, 0.0f, 0.0f };
            int captured = 0;
            int64_t age = 0, serial = 0;
            bool alive = false;
        };

        struct Replay
        {
            int object = 0, age = 0;
            int64_t serial = 0;
            double pos = 0.0, ratio = 1.0;
            float gain = 0.0f, panL = 1.0f, panR = 1.0f, lpL = 0.0f, lpR = 0.0f, lpCoef = 1.0f;
            dsp::Biquad shelfL, shelfR;
            bool active = false;
        };

        // An onset throws a new object (the oldest gives way): from 1 m up, in a hashed direction at THROW x 6 m/s.
        void spawn() noexcept
        {
            int slot = -1, oldest = 0;
            int alive = 0;
            for (int k = 0; k < maxObjects; ++k)
            {
                if (bodies[(size_t) k].alive)
                {
                    ++alive;
                    if (bodies[(size_t) k].age > bodies[(size_t) oldest].age)
                        oldest = k;
                }
                else if (slot < 0)
                    slot = k;
            }
            if (slot < 0 || alive >= maxCount)
                slot = oldest;
            auto& o = bodies[(size_t) slot];
            ++throws;
            const float az = dsp::hash01 (throws, 0, 0x7A20u, callsign) * dsp::twoPi;
            const float up = dsp::hash01 (throws, 1, 0x7A20u, callsign);
            const float speed = throwNow * 6.0f;
            o.p[0] = 0.0f; o.p[1] = startHeight; o.p[2] = 0.3f;
            o.v[0] = speed * std::cos (az) * (1.0f - 0.5f * up);
            o.v[1] = speed * up;
            o.v[2] = speed * std::sin (az) * (1.0f - 0.5f * up) * 0.3f;
            o.age = 0;
            o.captured = 0;
            o.serial = throws;
            o.alive = true;
            firstImpactAt = secondImpactAt = -1;
            spawnClock = clockNow;
        }

        void replay (int k, float speed) noexcept
        {
            auto& o = bodies[(size_t) k];
            ++impacts;
            const float t = (float) (clockNow - spawnClock) / sr;   // seconds since the last throw (test readout)
            if (firstImpactAt < 0) { firstImpactAt = 1; firstImpact = t; }
            else if (secondImpactAt < 0) { secondImpactAt = 1; secondImpact = t; }

            Replay* slot = &replays[0];
            for (auto& rp : replays)
            {
                if (! rp.active) { slot = &rp; break; }
                if (rp.age > slot->age) slot = &rp;
            }
            // Doppler: the radial velocity relative to the listener (positive = receding).
            float d[3], len = 0.0f;
            for (int a = 0; a < 3; ++a) { d[a] = o.p[a] - earPosition[a]; len += d[a] * d[a]; }
            len = std::sqrt (std::max (len, 1.0e-6f));
            const float radial = (o.v[0] * d[0] + o.v[1] * d[1] + o.v[2] * d[2]) / len;
            const float ratio = std::clamp (1.0f - dopplerNow * radial / soundSpeed, 0.5f, 2.0f);
            if (radial < -0.01f && dopplerNow > 0.0f) (ratio > 1.0f ? dopplerUp : dopplerWrong)++;
            if (radial > 0.01f && dopplerNow > 0.0f) (ratio < 1.0f ? dopplerDown : dopplerWrong)++;

            slot->object = k;
            slot->serial = o.serial;
            slot->pos = 0.0;
            slot->ratio = ratio;
            slot->age = 0;
            const float z = o.p[2];
            slot->gain = std::min (1.2f, speed / 3.5f) / (1.0f + 1.5f * z * depthNow);   // near the hit's own level
            dsp::panGains (std::clamp (o.p[0], -1.0f, 1.0f), slot->panL, slot->panR);
            slot->lpCoef = dsp::onePoleCoefHz (std::min (20000.0f * std::pow (0.1f, z * depthNow), 0.45f * sr), sr);
            const float shelf = 3.0f * std::clamp (o.p[1] / roomHeight, 0.0f, 1.0f);
            slot->shelfL.set (dsp::Biquad::highShelf, 6000.0f, 0.7071f, shelf, sr);
            slot->shelfR.set (dsp::Biquad::highShelf, 6000.0f, 0.7071f, shelf, sr);
            slot->shelfL.reset();
            slot->shelfR.reset();
            slot->lpL = slot->lpR = 0.0f;
            slot->active = true;
        }

        std::array<Body, maxObjects> bodies;
        std::array<Replay, maxReplays> replays {};
        dsp::OnsetDetector detector;
        float sr = 48000.0f, throwNow = 0.6f, dopplerNow = 0.5f, depthNow = 0.6f, firstImpact = -1.0f, secondImpact = -1.0f;
        int capture = 3840, maxCount = 6, firstImpactAt = -1, secondImpactAt = -1;
        int64_t lifeSamples = 144000, throws = 0, lifts = 0, impacts = 0, blockCount = 0, spawnClock = 0, clockNow = 0;
        int64_t dopplerUp = 0, dopplerDown = 0, dopplerWrong = 0;
        uint32_t callsign = 0;
    };

    class PoltergeistProcessor final : public host::EffectProcessor
    {
    public:
        PoltergeistProcessor() : EffectProcessor (effectInfo(), std::make_unique<PoltergeistChain>()) {}
        PoltergeistChain& room() { return static_cast<PoltergeistChain&> (getChain()); }
        juce::AudioProcessorEditor* createEditor() override;
    };

    class PoltergeistEditor final : public ui::EffectEditor
    {
    public:
        explicit PoltergeistEditor (PoltergeistProcessor& p) : EffectEditor (p), fx (p)
        {
            roomView = own (std::make_unique<ui::DotsView>());
            roomView->setFloorLine (true);
            roomView->setAxes ("X", "HEIGHT");
            addRow ({ panel ("ROOM // КОМНАТА", { { roomView, 4.6f } }, true),
                      panel ("TELEKINESIS // ТЕЛЕКИНЕЗ", { { knob ("throw", "THROW"), 1.0f }, { knob ("rage", "RAGE", true), 1.0f },
                                                          { knob ("sensitivity", "SENSE"), 1.0f } }) });
            addRow ({ panel ("PHYSICS // ФИЗИКА", { { knob ("gravity", "GRAVITY"), 1.0f }, { knob ("bounce", "BOUNCE"), 1.0f },
                                                   { knob ("life", "LIFE"), 1.0f }, { knob ("objects", "OBJECTS"), 1.0f } }),
                      panel ("ACOUSTICS // АКУСТИКА", { { knob ("doppler", "DOPPLER"), 1.0f }, { knob ("depth_filter", "DEPTH"), 1.0f } }) });
            addRow ({ outputPanel() });
            finish();
        }

    private:
        void refresh (float) override
        {
            auto& room = fx.room();
            std::vector<ui::DotsView::Dot> dots;
            for (int k = 0; k < maxObjects; ++k)
                if (room.uiObjects[(size_t) k].alive.load())
                    dots.push_back ({ room.uiObjects[(size_t) k].x.load(), room.uiObjects[(size_t) k].y.load() * 2.0f - 1.0f,
                                      1.0f - 0.7f * room.uiObjects[(size_t) k].z.load(), k % 3 == 0 ? 2 : 1, {} });
            roomView->setDots (std::move (dots));
        }

        PoltergeistProcessor& fx;
        ui::DotsView* roomView = nullptr;
    };

    juce::AudioProcessorEditor* PoltergeistProcessor::createEditor() { return new PoltergeistEditor (*this); }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PoltergeistProcessor();
}
