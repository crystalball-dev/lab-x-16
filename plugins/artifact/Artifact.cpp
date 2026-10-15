// ARTIFACT // АРТЕФАКТ — crystallography (Bragg peaks at lattice spacings) and reaction-diffusion (Gray-Scott Turing
// patterns). Artifacts forming in anomalies after emissions, each with a property. Twenty-four resonators sit at the
// frequency ratios of a crystal lattice; their gains are the inhibitor of a Gray-Scott system on a ring of 24 cells,
// fed by each resonator's share of the input, so spots and stripes of resonance drift and crystallise over FORMATION.
// An emission reseeds the lattice: ARTIFACT reads EMISSION's schedule from the Zone Bus and reseeds when the song
// crosses a fire point, so the reseed lands on the same bar in every render. The field saves with the project.

#include "core/host/EffectProcessor.h"
#include "core/dsp/Filters.h"
#include "core/dsp/Generators.h"
#include "ui/EffectEditor.h"
#include "ui/Views.h"

namespace
{
    using namespace lab;
    using host::Kind;
    using host::Smooth;

    enum Param : int { lattice = 0, f0, ratio, q, regime, formation, feed, depth, wetLevel, property, emissionReseed, reseed };
    enum Property : int { none = 0, moonlight, battery, fireball, bubble, stoneFlower, compass };
    constexpr int cells = 24;
    // F, k per REGIME. The textbook 2D constants (D_u 0.16, D_v 0.08) let every seed die on a ring of 24 cells; these
    // were found by a parameter search (docs/DESIGN.md) with D_u 0.4, D_v 0.05: patterns persist with contrast and,
    // with the substrate fed by the input, gather where the music's energy is.
    constexpr float regimes[4][2] = { { 0.04f, 0.062f }, { 0.03f, 0.055f }, { 0.025f, 0.05f }, { 0.05f, 0.055f } };
    constexpr float diffusionU = 0.4f, diffusionV = 0.05f;

    const host::EffectInfo& effectInfo()
    {
        static const host::EffectInfo i = []
        {
            host::EffectInfo e;
            e.product = LABX16_PRODUCT;
            e.subtitle = "АРТЕФАКТ";
            e.slug = LABX16_SLUG;
            e.version = LABX16_VERSION;
            e.code = LABX16_CODE;
            e.faction = host::Faction::etherealSignal;
            e.busClient = true;
            e.params = {
                { "lattice",         "Lattice",         Kind::choice, 0.0f,  2.0f,    0.0f,   0.0f,   Smooth::none,   "",   "Cubic|Hexagonal|Tetragonal" },
                { "f0",              "Base",            Kind::real,   40.0f, 1000.0f, 110.0f, 200.0f, Smooth::log,    "Hz", "" },
                { "ratio",           "Axis Ratio",      Kind::real,   0.5f,  2.0f,    1.2f,   1.0f,   Smooth::none,   "x",  "" },
                { "q",               "Q",               Kind::real,   20.0f, 200.0f,  60.0f,  60.0f,  Smooth::log,    "",   "" },
                { "regime",          "Regime",          Kind::choice, 0.0f,  3.0f,    0.0f,   0.0f,   Smooth::none,   "",   "Spots|Stripes|Mitosis|Coral" },
                { "formation",       "Formation",       Kind::real,   0.1f,  30.0f,   6.0f,   4.0f,   Smooth::log,    "s",  "" },
                { "feed",            "Feed",            Kind::real,   0.0f,  1.0f,    0.5f,   0.0f,   Smooth::linear, "%",  "" },
                { "depth",           "Depth",           Kind::real,   0.0f,  1.0f,    0.7f,   0.0f,   Smooth::linear, "%",  "" },
                { "wet_level",       "Wet Level",       Kind::real,   0.0f,  1.0f,    0.6f,   0.0f,   Smooth::linear, "%",  "" },
                { "property",        "Property",        Kind::choice, 0.0f,  6.0f,    0.0f,   0.0f,   Smooth::none,   "",   "None|Moonlight|Battery|Fireball|Bubble|Stone Flower|Compass" },
                { "emission_reseed", "Emission Reseed", Kind::choice, 0.0f,  1.0f,    1.0f,   0.0f,   Smooth::none,   "",   "Off|On" },
                { "reseed",          "Reseed",          Kind::choice, 0.0f,  1.0f,    0.0f,   0.0f,   Smooth::none,   "",   "Off|On" },
            };
            e.stages = { "lattice" };
            e.presets = {
                { "Init", {} },
                { "Raw Lattice", { { "property", 0.0f }, { "depth", 1.0f }, { "q", 90.0f }, { "output", -0.4f } } },
                { "Moonlight", { { "property", 1.0f }, { "lattice", 1.0f }, { "f0", 220.0f }, { "output", 0.0f } } },
                { "Battery", { { "property", 2.0f }, { "regime", 1.0f }, { "output", -0.3f } } },
                { "Fireball", { { "property", 3.0f }, { "f0", 80.0f }, { "regime", 3.0f }, { "output", -1.5f } } },
                { "Bubble", { { "property", 4.0f }, { "lattice", 2.0f }, { "output", -0.4f } } },
                { "Stone Flower", { { "property", 5.0f }, { "regime", 2.0f }, { "f0", 330.0f }, { "output", 0.0f } } },
                { "Compass", { { "property", 6.0f }, { "formation", 3.0f }, { "output", -0.3f } } },
            };
            return e;
        }();
        return i;
    }

    // The first 24 distinct plane spacings of a lattice, as frequency ratios over the lowest (Bragg peaks).
    std::array<float, cells> latticeRatios (int kind, float c)
    {
        std::vector<double> found;
        for (int h = 0; h <= 6; ++h)
            for (int k = 0; k <= 6; ++k)
                for (int l = 0; l <= 6; ++l)
                {
                    if (h == 0 && k == 0 && l == 0)
                        continue;
                    const double lc = (double) l / (double) std::max (0.1f, c);
                    double s;
                    if (kind == 0)      s = (double) (h * h + k * k + l * l);
                    else if (kind == 1) s = 4.0 / 3.0 * (double) (h * h + h * k + k * k) + lc * lc;
                    else                s = (double) (h * h + k * k) + lc * lc;
                    found.push_back (std::sqrt (s));
                }
        std::sort (found.begin(), found.end());
        std::array<float, cells> out {};
        int n = 0;
        double last = -1.0;
        for (const double r : found)
        {
            if (n >= cells)
                break;
            if (last < 0.0 || r - last > 1.0e-4 * r)
            {
                out[(size_t) n++] = (float) (r / found.front());
                last = r;
            }
        }
        for (; n < cells; ++n)
            out[(size_t) n] = out[(size_t) n - 1] * 1.05f;
        return out;
    }

    class ArtifactChain final : public host::Chain
    {
    public:
        std::array<std::atomic<float>, cells> uiField {};

        ArtifactChain() { seedField (0); }

        void prepare (double sampleRate, int) override
        {
            sr = (float) sampleRate;
            envCoef = dsp::onePoleCoef (0.05f, sr);
            gainCoef = dsp::onePoleCoef (0.05f, sr);
            follower = dsp::onePoleCoef (0.02f, sr);
            whisperGen.prepare (sr, 0xA27Fu);
            whisperGen.setFormant (150.0f);
            crackleBand.setHz (3500.0f, 1.0f, sr);
            rng.seed (0xBA77u);
            ratiosKey = -1.0f;
            reset();
        }

        void reset() override
        {
            for (auto& b : bands) b.reset();
            env.fill (0.0f);
            gains.fill (0.0f);
            for (auto& s : shelf) s.reset();
            wetEnv = 0.0f;
            crackle = 0.0f;
            stepDebt = 0.0f;
        }

        double tailSeconds (const float* v) const override { return 0.2 + (double) v[q] * 0.01; }

        void control (const float* v, const host::BlockContext& ctx, int n) override
        {
            // The lattice's frequencies (recomputed only when LATTICE, AXIS RATIO or BASE change).
            const float key = v[lattice] * 1000.0f + v[ratio] * 10.0f + v[f0] * 1.0e-3f + v[q] * 1.0e-6f;
            if (key != ratiosKey)
            {
                ratiosKey = key;
                const auto r = latticeRatios ((int) std::lround (v[lattice]), v[ratio]);
                for (int i = 0; i < cells; ++i)
                {
                    const float hz = v[f0] * r[(size_t) i];
                    active[(size_t) i] = hz < 0.45f * sr;
                    hzOf[(size_t) i] = hz;
                    for (int ch = 0; ch < 2; ++ch)
                        bands[(size_t) (ch * cells + i)].setHz (std::min (hz, 0.45f * sr), v[q], sr);
                }
            }

            // RESEED: the switch, or an emission on the Zone Bus (a fire point of EMISSION's schedule).
            const bool reseedNow = v[reseed] > 0.5f;
            if (reseedNow && ! lastReseed)
                seedField (++reseeds);
            lastReseed = reseedNow;
            const auto& t = *ctx.transport;
            if (v[emissionReseed] > 0.5f && ctx.bus != nullptr && t.playing)
            {
                host::bus::EmissionSchedule s;
                if (ctx.bus->readSchedule (s) && s.seq != 0 && s.fireBar >= 0)
                {
                    const int64_t bar = t.bar();
                    const int64_t since = bar - s.fireBar;
                    const bool fireBar = since == 0 || (s.repeatBars > 0 && since > 0 && since % s.repeatBars == 0);
                    if (fireBar && bar != lastFireBar)
                    {
                        lastFireBar = bar;
                        seedField (++reseeds);
                    }
                }
            }

            // Gray-Scott on the ring, 2000 / FORMATION steps a second.
            const float F = regimes[std::clamp ((int) std::lround (v[regime]), 0, 3)][0];
            const float k = regimes[std::clamp ((int) std::lround (v[regime]), 0, 3)][1];
            std::array<float, cells> energy {};
            for (int i = 0; i < cells; ++i)
                energy[(size_t) i] = active[(size_t) i] ? std::clamp ((dsp::gainToDb (std::sqrt (env[(size_t) i]) + 1.0e-9f) + 60.0f) / 60.0f, 0.0f, 1.0f) : 0.0f;
            stepDebt += (float) n / sr * 2000.0f / std::max (0.1f, v[formation]);
            int steps = 0;
            while (stepDebt >= 1.0f && steps < 64)
            {
                step (F, k, v[feed], energy);
                stepDebt -= 1.0f;
                ++steps;
            }
            float top = 1.0e-4f;
            for (const auto x : field.v)
                top = std::max (top, x);
            for (int i = 0; i < cells; ++i)
            {
                target[(size_t) i] = active[(size_t) i] ? v[depth] * field.v[(size_t) i] / top : 0.0f;
                uiField[(size_t) i].store (field.v[(size_t) i] / top);
            }
            wet = v[wetLevel] * 3.0f;
            propertyNow = (int) std::lround (v[property]);
            playingNow = ctx.transport->playing;

            // COMPASS: the wet leans toward the pattern's centroid around the ring.
            float centroid = 0.0f, mass = 1.0e-6f;
            for (int i = 0; i < cells; ++i)
            {
                centroid += field.v[(size_t) i] * (2.0f * (float) i / (float) (cells - 1) - 1.0f);
                mass += field.v[(size_t) i];
            }
            compassPan = propertyNow == compass ? std::clamp (centroid / mass * 2.0f, -1.0f, 1.0f) : 0.0f;
            peakV = top;
            const float shelfDb = propertyNow == stoneFlower ? -12.0f * std::clamp ((dsp::gainToDb (wetEnv + 1.0e-9f) + 40.0f) / 40.0f, 0.0f, 1.0f) : 0.0f;
            for (auto& s : shelf)
                s.set (dsp::Biquad::highShelf, 6000.0f, 0.7071f, shelfDb, sr);
        }

        void process (float* l, float* r, const float*, const float*, int n, host::Guard& guard) override
        {
            float pl = 1.0f, pr = 1.0f;
            dsp::panGains (compassPan, pl, pr);
            const float crackleChance = propertyNow == battery && playingNow ? peakV * 30.0f / sr : 0.0f;   // no sparks on a stopped track
            for (int i = 0; i < n; ++i)
            {
                float rl = 0.0f, rr = 0.0f;
                for (int c = 0; c < cells; ++c)
                {
                    auto& g = gains[(size_t) c];
                    g += (target[(size_t) c] - g) * gainCoef;
                    const float bl = bands[(size_t) c].bandpass (l[i]);
                    const float br = bands[(size_t) (cells + c)].bandpass (r[i]);
                    env[(size_t) c] += (0.5f * (bl * bl + br * br) - env[(size_t) c]) * envCoef;
                    rl += bl * g;
                    rr += br * g;
                }
                rl *= wet;
                rr *= wet;
                const float mono = 0.5f * (std::abs (rl) + std::abs (rr));
                wetEnv += (mono - wetEnv) * follower;
                switch (propertyNow)
                {
                    case moonlight:
                    {
                        const float w = whisperGen.next() * wetEnv * 0.5f;
                        rl += w;
                        rr += w;
                        break;
                    }
                    case battery:
                    {
                        if (rng.uni() < crackleChance)
                            crackle += rng.bi() * 0.6f;
                        crackle *= 0.995f;
                        const float c = crackleBand.bandpass (crackle) * (0.2f + wetEnv);
                        rl += c;
                        rr += c;
                        break;
                    }
                    case fireball:
                        rl = dsp::fastTanh (rl * 4.0f) * 0.5f;
                        rr = dsp::fastTanh (rr * 4.0f) * 0.5f;
                        break;
                    case stoneFlower:
                        rl = shelf[0].process (rl);
                        rr = shelf[1].process (rr);
                        break;
                    case compass:
                        rl *= pl;
                        rr *= pr;
                        break;
                    default:
                        break;
                }
                float yl = l[i] + rl, yr = r[i] + rr;
                if (propertyNow == bubble)
                {
                    // BUBBLE scrubs the floor: under -50 dBFS the whole output drops 12 dB.
                    const float level = std::max (std::abs (yl), std::abs (yr));
                    floorEnv += (level - floorEnv) * follower;
                    const float g = floorEnv < 0.00316f ? dsp::dbToGain (-12.0f * (1.0f - floorEnv / 0.00316f)) : 1.0f;
                    yl *= g;
                    yr *= g;
                }
                l[i] = yl;
                r[i] = yr;
            }
            guard (0, l, r, n);
        }

        void meter (host::Meters& m) const override
        {
            m.custom[0].store ((float) reseeds);
            float lo = 1.0f, hi = 0.0f;
            for (int i = 0; i < cells; ++i)
            {
                lo = std::min ({ lo, field.u[(size_t) i], field.v[(size_t) i] });
                hi = std::max ({ hi, field.u[(size_t) i], field.v[(size_t) i] });
            }
            m.custom[1].store (lo);
            m.custom[2].store (hi);
            float mean = 0.0f;
            for (const auto g : gains) mean += g;
            mean /= (float) cells;
            float var = 0.0f;
            for (const auto g : gains) var += (g - mean) * (g - mean);
            m.custom[3].store (var / (float) cells);
            m.custom[4].store (hzOf[0]);
            m.custom[5].store (hzOf[1]);
            m.custom[6].store (hzOf[2]);
            float checksum = 0.0f;
            for (int i = 0; i < cells; ++i)
                checksum += field.v[(size_t) i] * (float) (i + 1);
            m.custom[7].store (checksum);
        }

        int blobVersion() const override { return 1; }
        void saveBlob (juce::MemoryOutputStream& out) const override
        {
            for (const auto x : field.u) out.writeFloat (x);
            for (const auto x : field.v) out.writeFloat (x);
        }
        bool loadBlob (juce::MemoryInputStream& in, int version) override
        {
            if (version != 1 || in.getNumBytesRemaining() < (int64_t) (sizeof (float) * 2 * cells))
                return false;
            for (auto& x : field.u) x = std::clamp (in.readFloat(), 0.0f, 1.0f);
            for (auto& x : field.v) x = std::clamp (in.readFloat(), 0.0f, 1.0f);
            return true;
        }

    private:
        struct Field { std::array<float, cells> u {}, v {}; };

        // u = 1, v = 0, with three hashed seeds three cells wide (v = u = 0.5): the crystal starts from a few points.
        void seedField (int64_t count) noexcept
        {
            field.u.fill (1.0f);
            field.v.fill (0.0f);
            for (int k = 0; k < 3; ++k)
            {
                const int c = (int) (dsp::hash01 (count, k, 0xA27Fu, 0) * (float) cells) % cells;
                for (int w = 0; w < 3; ++w)
                {
                    field.v[(size_t) ((c + w) % cells)] = 0.5f;
                    field.u[(size_t) ((c + w) % cells)] = 0.5f;
                }
            }
        }

        // One Gray-Scott step on the ring (dt 1). FEED raises the substrate's feed rate where a resonator hears more of
        // the input, so the pattern drifts toward the music's energy; a field that has died re-nucleates at the most
        // energetic cell.
        void step (float F, float k, float feedAmount, const std::array<float, cells>& energy) noexcept
        {
            Field next;
            float top = 0.0f;
            for (int i = 0; i < cells; ++i)
            {
                const int a = (i + cells - 1) % cells, b = (i + 1) % cells;
                const float u = field.u[(size_t) i], v = field.v[(size_t) i];
                const float lapU = field.u[(size_t) a] + field.u[(size_t) b] - 2.0f * u;
                const float lapV = field.v[(size_t) a] + field.v[(size_t) b] - 2.0f * v;
                const float uvv = u * v * v;
                const float Fi = F * (1.0f + feedAmount * (energy[(size_t) i] - 0.5f));
                next.u[(size_t) i] = std::clamp (u + diffusionU * lapU - uvv + Fi * (1.0f - u), 0.0f, 1.0f);
                next.v[(size_t) i] = std::clamp (v + diffusionV * lapV + uvv - (F + k) * v, 0.0f, 1.0f);
                top = std::max (top, next.v[(size_t) i]);
            }
            if (top < 0.01f)
            {
                int c = 0;
                for (int i = 1; i < cells; ++i)
                    if (energy[(size_t) i] > energy[(size_t) c])
                        c = i;
                next.v[(size_t) c] = 0.5f;
                next.u[(size_t) c] = 0.5f;
            }
            field = next;
        }

        Field field;
        std::array<dsp::SVF, 2 * cells> bands {};
        std::array<float, cells> env {}, gains {}, target {}, hzOf {};
        std::array<bool, cells> active {};
        std::array<dsp::Biquad, 2> shelf;
        dsp::Whisper whisperGen;
        dsp::SVF crackleBand;
        dsp::Rng rng;
        float sr = 48000.0f, envCoef = 0.001f, gainCoef = 0.001f, follower = 0.001f, ratiosKey = -1.0f, stepDebt = 0.0f, wet = 1.8f;
        float wetEnv = 0.0f, crackle = 0.0f, compassPan = 0.0f, peakV = 0.0f, floorEnv = 0.0f;
        int propertyNow = none;
        bool playingNow = false;
        int64_t reseeds = 0, lastFireBar = std::numeric_limits<int64_t>::min();
        bool lastReseed = false;
    };

    class ArtifactProcessor final : public host::EffectProcessor
    {
    public:
        ArtifactProcessor() : EffectProcessor (effectInfo(), std::make_unique<ArtifactChain>()) {}
        ArtifactChain& artifact() { return static_cast<ArtifactChain&> (getChain()); }
        juce::AudioProcessorEditor* createEditor() override;
    };

    class ArtifactEditor final : public ui::EffectEditor
    {
    public:
        explicit ArtifactEditor (ArtifactProcessor& p) : EffectEditor (p), fx (p)
        {
            latticeView = own (std::make_unique<ui::BarsView>());
            latticeView->setCaption ("LATTICE  //  24 CELLS");
            addRow ({ panel ("CRYSTAL // КРИСТАЛЛ", { { latticeView, 5.0f } }, true),
                      panel ("PROPERTY // СВОЙСТВО", { { choices ({ { "property", "PROPERTY" }, { "regime", "REGIME" } }), 1.5f },
                                                      { choices ({ { "emission_reseed", "EMISSION" }, { "reseed", "RESEED" } }), 1.3f } }) });
            addRow ({ panel ("LATTICE // РЕШЁТКА", { { choices ({ { "lattice", "LATTICE" } }), 1.4f }, { knob ("f0", "BASE"), 1.0f },
                                                    { knob ("ratio", "C/A"), 1.0f }, { knob ("q", "Q"), 1.0f } }),
                      panel ("GROWTH // РОСТ", { { knob ("formation", "FORMATION"), 1.0f }, { knob ("feed", "FEED"), 1.0f },
                                                { knob ("depth", "DEPTH", true), 1.0f }, { knob ("wet_level", "WET"), 1.0f } }) });
            addRow ({ outputPanel() });
            addSide (panel ("PEERS // СВЯЗЬ", { { peersView(), 1.0f } }));
            finish();
        }

    private:
        void refresh (float) override
        {
            std::array<float, cells> values {};
            int brightest = 0;
            for (int i = 0; i < cells; ++i)
            {
                values[(size_t) i] = fx.artifact().uiField[(size_t) i].load();
                if (values[(size_t) i] > values[(size_t) brightest])
                    brightest = i;
            }
            latticeView->setValues (values.data(), cells);
            latticeView->setHighlight (brightest);
        }

        ArtifactProcessor& fx;
        ui::BarsView* latticeView = nullptr;
    };

    juce::AudioProcessorEditor* ArtifactProcessor::createEditor() { return new ArtifactEditor (*this); }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ArtifactProcessor();
}
