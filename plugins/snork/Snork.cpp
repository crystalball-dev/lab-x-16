// SNORK // СНОРК — genetic algorithms: a population of genomes, fitness, tournament selection, crossover, mutation,
// elitism. The mutants bred in Lab X-18; the snork that was once a soldier. Up to eight genomes each define a small
// chain (high-pass, low-pass, tilt, drive, crush, chorus, delay, pitch). All of them run on the input; every GENERATION
// bars the population is judged against a GOAL (brighter, darker, louder, quieter, wider, or like the sidechain) and
// bred. What you hear is a voice that glides to the fittest genome (the alpha), blended with the best ever by INSTINCT.
// Every random choice is a hash of the generation and the STRAIN, so a render repeats the same evolution.

#include "core/host/EffectProcessor.h"
#include "core/dsp/Crush.h"
#include "core/dsp/DelayLine.h"
#include "core/dsp/Ensemble.h"
#include "core/dsp/Filters.h"
#include "core/dsp/MicroShift.h"
#include "ui/EffectEditor.h"
#include "ui/Views.h"

namespace
{
    using namespace lab;
    using host::Kind;
    using host::Smooth;

    enum Param : int { goal = 0, driveGoal, generation, population, mutation, crossover, evolve, seed, instinct };
    enum Goal : int { brighter = 0, darker, louder, quieter, wider, matchSide };
    constexpr int genes = 8, maxPopulation = 8, numBands = 7;
    using Genome = std::array<float, genes>;
    constexpr Genome neutral { 0.0f, 1.0f, 0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 0.5f };   // a member that passes the input through
    constexpr float bandHz[numBands] = { 125.0f, 250.0f, 500.0f, 1000.0f, 2000.0f, 4000.0f, 8000.0f };

    const host::EffectInfo& effectInfo()
    {
        static const host::EffectInfo i = []
        {
            host::EffectInfo e;
            e.product = LABX16_PRODUCT;
            e.subtitle = "СНОРК";
            e.slug = LABX16_SLUG;
            e.version = LABX16_VERSION;
            e.code = LABX16_CODE;
            e.faction = host::Faction::deepZone;
            e.sidechain = true;
            e.params = {
                { "goal",       "Goal",          Kind::choice,  0.0f, 5.0f,    0.0f, 0.0f, Smooth::none,   "",  "Brighter|Darker|Louder|Quieter|Wider|Match Sidechain" },
                { "drive_goal", "Goal Strength", Kind::real,    0.0f, 1.0f,    0.5f, 0.0f, Smooth::none,   "%", "" },
                { "generation", "Generation",    Kind::integer, 1.0f, 8.0f,    2.0f, 0.0f, Smooth::none,   "",  "" },
                { "population", "Population",    Kind::integer, 2.0f, 8.0f,    8.0f, 0.0f, Smooth::none,   "",  "" },
                { "mutation",   "Mutation",      Kind::real,    0.0f, 1.0f,    0.3f, 0.0f, Smooth::none,   "%", "" },
                { "crossover",  "Crossover",     Kind::real,    0.0f, 1.0f,    0.5f, 0.0f, Smooth::none,   "%", "" },
                { "evolve",     "Evolve",        Kind::choice,  0.0f, 1.0f,    1.0f, 0.0f, Smooth::none,   "",  "Off|On" },
                { "seed",       "Strain",        Kind::integer, 0.0f, 9999.0f, 1.0f, 0.0f, Smooth::none,   "",  "" },
                { "instinct",   "Instinct",      Kind::real,    0.0f, 1.0f,    0.0f, 0.0f, Smooth::linear, "%", "" },
            };
            e.stages = { "alpha" };
            e.presets = {
                { "Init", {} },
                { "Evolve Brighter", { { "goal", 0.0f }, { "drive_goal", 0.7f }, { "generation", 1.0f }, { "output", -8.5f } } },
                { "Evolve Darker", { { "goal", 1.0f }, { "drive_goal", 0.7f }, { "generation", 1.0f }, { "output", -2.8f } } },
                { "Match Sidechain", { { "goal", 5.0f }, { "generation", 2.0f }, { "output", 0.1f } } },
                { "Mutant Strain", { { "mutation", 1.0f }, { "seed", 342.0f }, { "generation", 1.0f }, { "output", -2.2f } } },
                { "Frozen Specimen", { { "evolve", 0.0f }, { "seed", 18.0f }, { "output", -1.3f } } },
                { "Wide Breed", { { "goal", 4.0f }, { "drive_goal", 0.8f }, { "output", 0.1f } } },
            };
            return e;
        }();
        return i;
    }

    // Features of a signal over a generation: band energies, level, peak and the stereo correlation.
    struct Analyser
    {
        std::array<dsp::SVF, numBands> bands {};
        std::array<double, numBands> energy {};
        double total = 0.0, lr = 0.0, ll = 0.0, rr = 0.0;
        float peak = 0.0f;
        int64_t count = 0;

        void prepare (float sr) { for (int b = 0; b < numBands; ++b) bands[(size_t) b].setHz (bandHz[b], 1.4f, sr); }
        void clear() noexcept { energy.fill (0.0); total = lr = ll = rr = 0.0; peak = 0.0f; count = 0; }
        void add (const float* l, const float* r, int n) noexcept
        {
            for (int i = 0; i < n; ++i)
            {
                const float m = 0.5f * (l[i] + r[i]);
                for (int b = 0; b < numBands; ++b)
                {
                    const float x = bands[(size_t) b].bandpass (m);
                    energy[(size_t) b] += (double) (x * x);
                }
                total += (double) (m * m);
                lr += (double) (l[i] * r[i]);
                ll += (double) (l[i] * l[i]);
                rr += (double) (r[i] * r[i]);
                peak = std::max (peak, std::max (std::abs (l[i]), std::abs (r[i])));
            }
            count += n;
        }

        struct Features { float centroid = 0.0f, rmsDb = -120.0f, crestDb = 0.0f, correlation = 1.0f; };
        Features features() const noexcept
        {
            Features f;
            double weighted = 0.0, sum = 0.0;
            for (int b = 0; b < numBands; ++b)
            {
                weighted += energy[(size_t) b] * std::log2 ((double) bandHz[b]);
                sum += energy[(size_t) b];
            }
            f.centroid = sum > 1.0e-12 ? (float) (weighted / sum) : std::log2 (1000.0f);
            const double ms = count > 0 ? total / (double) count : 0.0;
            f.rmsDb = dsp::gainToDb ((float) std::sqrt (ms) + 1.0e-9f);
            f.crestDb = dsp::gainToDb (peak + 1.0e-9f) - f.rmsDb;
            f.correlation = (ll > 1.0e-12 && rr > 1.0e-12) ? (float) (lr / std::sqrt (ll * rr)) : 1.0f;
            return f;
        }
    };

    // One genome's chain.
    struct Specimen
    {
        Genome genome = neutral;
        std::array<dsp::SVF, 2> hp {}, lp {};
        std::array<dsp::Tilt, 2> tilt {};
        dsp::Ensemble chorus;
        dsp::StereoDelayLine echo;
        std::array<dsp::MicroShift, 2> shift;
        Analyser analyser;
        float sr = 48000.0f, fitness = 0.0f, shiftMix = 0.0f;

        void prepare (float sampleRate)
        {
            sr = sampleRate;
            chorus.prepare (sr);
            echo.prepare ((int) std::ceil (0.26f * sr) + 4);
            for (auto& s : shift)
            {
                s.prepare (sr, 0.05f);
                s.setWindow (0.03f);
            }
            for (auto& t : tilt)
                t.prepare (900.0f, sr);
            analyser.prepare (sr);
            reset();
        }

        void reset() noexcept
        {
            for (auto& f : hp) f.reset();
            for (auto& f : lp) f.reset();
            for (auto& t : tilt) t.reset();
            chorus.reset();
            echo.reset();
            for (auto& s : shift) s.reset();
            shiftMix = 0.0f;
            analyser.clear();
        }

        void process (float* l, float* r, int n) noexcept
        {
            const auto& g = genome;
            const float hpHz = 20.0f * std::pow (100.0f, g[0]), lpHz = 500.0f * std::pow (40.0f, g[1]);
            const float tone = (g[2] - 0.5f) * 1.6f, drive = dsp::dbToGain (24.0f * g[3]), bits = 16.0f - 12.0f * g[4];
            const float comp = 1.0f / dsp::fastTanh (std::max (1.0f, drive));
            for (int ch = 0; ch < 2; ++ch)
            {
                hp[(size_t) ch].setHz (hpHz, 0.7071f, sr);
                lp[(size_t) ch].setHz (std::min (lpHz, 0.45f * sr), 0.7071f, sr);
            }
            float* io[2] = { l, r };
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < n; ++i)
                {
                    float x = lp[(size_t) ch].lowpass (hp[(size_t) ch].highpass (io[ch][i]));
                    x = tilt[(size_t) ch].process (x, tone);
                    if (g[3] > 0.01f)
                        x = dsp::fastTanh (x * drive) * comp;
                    if (bits < 15.9f)
                        x = dsp::Crush::quantise (x, bits);
                    io[ch][i] = x;
                }
            chorus.process (l, r, n, g[5] * 0.7f, g[5], 0.8f);
            const float delaySamples = std::max (1.0f, g[6] * 0.25f * sr), feedback = 0.5f * g[6], send = g[6] > 0.02f ? 0.4f : 0.0f;
            // The shifter always runs, so its buffer is never stale (an idle one replayed old audio when the pitch gene
            // came back), and fades over 20 ms around +-0.25 semitone, where it would only comb.
            const float semis = -12.0f + 24.0f * g[7];
            const float shiftTarget = std::abs (semis) > 0.25f ? 1.0f : 0.0f, shiftStep = 1.0f / (0.02f * sr);
            const float ratio = dsp::semisToRatio (semis);
            for (int i = 0; i < n; ++i)
            {
                if (send > 0.0f)
                {
                    const float el = echo.readLinear (0, delaySamples - 1.0f), er = echo.readLinear (1, delaySamples - 1.0f);
                    echo.push (l[i] + el * feedback, r[i] + er * feedback);
                    l[i] += el * send;
                    r[i] += er * send;
                }
                else
                    echo.push (0.0f, 0.0f);
                const float sl = shift[0].process (l[i], ratio), srr = shift[1].process (r[i], ratio);
                shiftMix = shiftTarget > shiftMix ? std::min (1.0f, shiftMix + shiftStep) : std::max (0.0f, shiftMix - shiftStep);
                l[i] += (sl - l[i]) * shiftMix;
                r[i] += (srr - r[i]) * shiftMix;
            }
            analyser.add (l, r, n);
        }
    };

    class SnorkChain final : public host::Chain
    {
    public:
        std::array<std::atomic<float>, maxPopulation> uiFitness {};
        std::array<std::atomic<float>, genes> uiGenome {};
        std::atomic<int> uiAlpha { 0 }, uiGeneration { 0 }, uiPopulation { 8 };

        void prepare (double sampleRate, int) override
        {
            sr = (float) sampleRate;
            for (auto& s : specimens)
                s.prepare (sr);
            voice.prepare (sr);
            inputAnalyser.prepare (sr);
            sideAnalyser.prepare (sr);
            glide = dsp::onePoleCoef (0.05f, sr);
            reset();
        }

        void reset() override
        {
            for (auto& s : specimens)
                s.reset();
            voice.reset();
            inputAnalyser.clear();
            sideAnalyser.clear();
        }

        double tailSeconds (const float*) const override { return 1.5; }

        void control (const float* v, const host::BlockContext& ctx, int n) override
        {
            const int strain = (int) std::lround (v[seed]);
            const int size = std::clamp ((int) std::lround (v[population]), 2, maxPopulation);
            if (strain != seedNow || ! seeded)
            {
                seedNow = strain;
                seedPopulation();
                seeded = true;
            }
            if (size > populationNow)
                for (int j = populationNow; j < size; ++j)
                    specimens[(size_t) j].genome = randomGenome (j + 100 * (int) (generations % 100000));
            populationNow = size;
            p = v;
            sideActive = ctx.sidechainActive;

            // A generation ends every GENERATION bars of the song (every 2 s x GENERATION while it is stopped).
            const auto& t = *ctx.transport;
            const int bars = std::clamp ((int) std::lround (v[generation]), 1, 8);
            const int64_t index = t.playing ? (int64_t) std::floor ((double) t.bar() / (double) bars)
                                            : ctx.sampleClock / std::max<int64_t> (1, (int64_t) (2.0f * sr * (float) bars));
            if (index != lastIndex)
            {
                if (lastIndex != std::numeric_limits<int64_t>::min())
                    endGeneration (v);
                lastIndex = index;
            }

            // The voice glides to the alpha (blended with the best ever by INSTINCT).
            const float k = 1.0f - std::pow (1.0f - glide, (float) n);
            for (int g = 0; g < genes; ++g)
            {
                const float target = specimens[(size_t) alpha].genome[(size_t) g] * (1.0f - v[instinct]) + bestEver[(size_t) g] * v[instinct];
                voice.genome[(size_t) g] += (target - voice.genome[(size_t) g]) * k;
                uiGenome[(size_t) g].store (voice.genome[(size_t) g]);
            }
        }

        void process (float* l, float* r, const float* sideL, const float* sideR, int n, host::Guard& guard) override
        {
            inputAnalyser.add (l, r, n);
            if (sideActive)
                sideAnalyser.add (sideL, sideR, n);
            std::array<float, host::controlBlock> bl {}, br {};
            for (int j = 0; j < populationNow; ++j)
            {
                std::copy (l, l + n, bl.begin());
                std::copy (r, r + n, br.begin());
                specimens[(size_t) j].process (bl.data(), br.data(), n);
            }
            voice.process (l, r, n);
            guard (0, l, r, n);
        }

        void meter (host::Meters& m) const override
        {
            m.custom[0].store ((float) generations);
            m.custom[1].store ((float) alpha);
            m.custom[2].store (alphaCentroidHz);
            m.custom[3].store (firstCentroidHz);
            m.custom[4].store (specimens[(size_t) alpha].fitness);
            float checksum = 0.0f;
            for (int g = 0; g < genes; ++g)
                checksum += specimens[(size_t) alpha].genome[(size_t) g] * (float) (g + 1);
            m.custom[5].store (checksum);
            m.custom[6].store (inputCentroidHz);
        }

    private:
        Genome randomGenome (int salt) const noexcept
        {
            Genome g {};
            for (int k = 0; k < genes; ++k)
                g[(size_t) k] = dsp::hash01 (salt, k, 0x5EEDu, (uint32_t) seedNow);
            return g;
        }

        void seedPopulation() noexcept
        {
            specimens[0].genome = neutral;
            for (int j = 1; j < maxPopulation; ++j)
                specimens[(size_t) j].genome = randomGenome (j);
            bestEver = neutral;
            bestEverFitness = -1.0e9f;
            alpha = 0;
            voice.genome = neutral;
            generations = 0;
            firstCentroidHz = 0.0f;
        }

        // Fitness: minus the weighted distance between a specimen's features and the goal's target features.
        float fitnessOf (const Analyser::Features& f, const Analyser::Features& in, const Analyser::Features& side, const float* v) const noexcept
        {
            if (f.rmsDb < -80.0f)
                return -100.0f;   // a silent mutant is not fit
            const int goalNow = (int) std::lround (v[goal]);
            const float strength = v[driveGoal];
            auto target = goalNow == matchSide && sideActive && side.rmsDb > -80.0f ? side : in;
            float wc = 0.3f, wr = 0.3f, wk = 0.15f, wp = 0.3f;
            switch (goalNow)
            {
                case brighter: target.centroid += 2.0f * (0.25f + 0.75f * strength); wc = 1.0f; break;
                case darker:   target.centroid -= 2.0f * (0.25f + 0.75f * strength); wc = 1.0f; break;
                case louder:   target.rmsDb += 6.0f * (0.5f + strength); wr = 1.0f; break;
                case quieter:  target.rmsDb -= 6.0f * (0.5f + strength); wr = 1.0f; break;
                case wider:    target.correlation = std::max (-0.5f, in.correlation - (0.3f + 0.7f * strength)); wp = 1.0f; break;
                default:       wc = wr = wk = wp = 1.0f; break;
            }
            return -(wc * std::abs (f.centroid - target.centroid) + wr * std::abs (f.rmsDb - target.rmsDb) / 6.0f
                     + wk * std::abs (f.crestDb - target.crestDb) / 6.0f + wp * std::abs (f.correlation - target.correlation));
        }

        void endGeneration (const float* v) noexcept
        {
            const auto in = inputAnalyser.features();
            const auto side = sideAnalyser.features();
            inputCentroidHz = std::exp2 (in.centroid);
            int best = 0;
            for (int j = 0; j < populationNow; ++j)
            {
                auto& s = specimens[(size_t) j];
                s.fitness = fitnessOf (s.analyser.features(), in, side, v);
                if (s.fitness > specimens[(size_t) best].fitness)
                    best = j;
            }
            const bool evolving = v[evolve] > 0.5f;
            if (evolving || generations == 0)
            {
                alpha = best;
                alphaCentroidHz = std::exp2 (specimens[(size_t) best].analyser.features().centroid);
                if (generations == 0)   // generation 0's alpha: the neutral genome the voice starts from
                    firstCentroidHz = std::exp2 (specimens[0].analyser.features().centroid);
                if (specimens[(size_t) best].fitness > bestEverFitness)
                {
                    bestEverFitness = specimens[(size_t) best].fitness;
                    bestEver = specimens[(size_t) best].genome;
                }
            }
            for (int j = 0; j < maxPopulation; ++j)
                uiFitness[(size_t) j].store (j < populationNow ? specimens[(size_t) j].fitness : -100.0f);
            if (evolving)
                breed (v);
            for (auto& s : specimens)
                s.analyser.clear();
            inputAnalyser.clear();
            sideAnalyser.clear();
            ++generations;
            uiAlpha.store (alpha);
            uiGeneration.store ((int) generations);
            uiPopulation.store (populationNow);
        }

        // Two elites stay; every other slot gets a child of two tournament winners (size 3): uniform crossover, then a
        // gaussian mutation (sigma 0.2 x MUTATION) from two hash01 draws (Box-Muller).
        void breed (const float* v) noexcept
        {
            std::array<int, maxPopulation> order {};
            for (int j = 0; j < populationNow; ++j)
                order[(size_t) j] = j;
            std::stable_sort (order.begin(), order.begin() + populationNow,
                              [this] (int a, int b) { return specimens[(size_t) a].fitness > specimens[(size_t) b].fitness; });
            const int elites = std::min (2, populationNow);
            const auto gen = generations;
            const auto strain = (uint32_t) seedNow;
            auto draw = [gen, strain] (int slot, int purpose) { return dsp::hash01 (gen, slot * 64 + purpose, 0x5E0Bu, strain); };
            auto tournament = [&] (int slot, int salt)
            {
                int winner = (int) (draw (slot, salt) * (float) populationNow) % populationNow;
                for (int k = 1; k < 3; ++k)
                {
                    const int c = (int) (draw (slot, salt + k) * (float) populationNow) % populationNow;
                    if (specimens[(size_t) c].fitness > specimens[(size_t) winner].fitness)
                        winner = c;
                }
                return winner;
            };
            std::array<Genome, maxPopulation> next {};
            for (int j = 0; j < populationNow; ++j)
                next[(size_t) j] = specimens[(size_t) j].genome;
            const float sigma = 0.2f * v[mutation];
            for (int k = elites; k < populationNow; ++k)
            {
                const int slot = order[(size_t) k];
                const auto& a = specimens[(size_t) tournament (slot, 1)].genome;
                const auto& b = specimens[(size_t) tournament (slot, 5)].genome;
                for (int g = 0; g < genes; ++g)
                {
                    float x = draw (slot, 10 + g) < v[crossover] ? b[(size_t) g] : a[(size_t) g];
                    const float u1 = std::max (1.0e-6f, draw (slot, 20 + g)), u2 = draw (slot, 30 + g);
                    x += sigma * std::sqrt (-2.0f * std::log (u1)) * std::cos (dsp::twoPi * u2);
                    next[(size_t) slot][(size_t) g] = std::clamp (x, 0.0f, 1.0f);
                }
            }
            for (int j = 0; j < populationNow; ++j)
                specimens[(size_t) j].genome = next[(size_t) j];
            // The alpha is an elite, so its genome is unchanged by breeding.
        }

        std::array<Specimen, maxPopulation> specimens;
        Specimen voice;
        Analyser inputAnalyser, sideAnalyser;
        Genome bestEver = neutral;
        const float* p = nullptr;
        float sr = 48000.0f, glide = 0.001f, bestEverFitness = -1.0e9f, alphaCentroidHz = 0.0f, firstCentroidHz = 0.0f, inputCentroidHz = 0.0f;
        int seedNow = -1, populationNow = 8, alpha = 0;
        int64_t generations = 0, lastIndex = std::numeric_limits<int64_t>::min();
        bool seeded = false, sideActive = false;
    };

    class SnorkProcessor final : public host::EffectProcessor
    {
    public:
        SnorkProcessor() : EffectProcessor (effectInfo(), std::make_unique<SnorkChain>()) {}
        SnorkChain& snork() { return static_cast<SnorkChain&> (getChain()); }
        juce::AudioProcessorEditor* createEditor() override;
    };

    class SnorkEditor final : public ui::EffectEditor
    {
    public:
        explicit SnorkEditor (SnorkProcessor& p) : EffectEditor (p), fx (p)
        {
            fitnessBars = own (std::make_unique<ui::BarsView>());
            fitnessBars->setCaption ("POPULATION FITNESS");
            fitnessBars->setLabels ({ "1", "2", "3", "4", "5", "6", "7", "8" });
            genomeBars = own (std::make_unique<ui::BarsView>());
            genomeBars->setCaption ("ALPHA GENOME");
            genomeBars->setLabels ({ "HP", "LP", "TLT", "DRV", "CRU", "CHO", "DLY", "PIT" });
            readout = own (std::make_unique<ui::BigReadout>());
            addRow ({ panel ("BREEDING // СЕЛЕКЦИЯ", { { fitnessBars, 2.8f }, { genomeBars, 2.8f }, { readout, 1.8f } }, true) });
            addRow ({ panel ("GOAL // ЦЕЛЬ", { { choices ({ { "goal", "GOAL" } }), 1.6f }, { knob ("drive_goal", "STRENGTH"), 1.0f } }),
                      panel ("GENETICS // ГЕНЕТИКА", { { knob ("generation", "BARS"), 1.0f }, { knob ("population", "POP"), 1.0f },
                                                      { knob ("mutation", "MUTATION", true), 1.0f }, { knob ("crossover", "CROSSOVER"), 1.0f } }) });
            addRow ({ panel ("STRAIN // ШТАММ", { { choices ({ { "evolve", "EVOLVE" } }), 1.2f }, { knob ("seed", "STRAIN"), 1.0f },
                                                 { knob ("instinct", "INSTINCT"), 1.0f } }),
                      outputPanel() });
            finish();
        }

    private:
        void refresh (float) override
        {
            auto& s = fx.snork();
            std::array<float, maxPopulation> fit {};
            const int pop = s.uiPopulation.load();
            float lo = 0.0f;
            for (int j = 0; j < pop; ++j)
                lo = std::min (lo, s.uiFitness[(size_t) j].load());
            for (int j = 0; j < maxPopulation; ++j)
                fit[(size_t) j] = j < pop ? 1.0f - s.uiFitness[(size_t) j].load() / std::min (-0.01f, lo - 0.01f) : 0.0f;
            fitnessBars->setValues (fit.data(), maxPopulation);
            fitnessBars->setHighlight (s.uiAlpha.load());
            std::array<float, genes> g {};
            for (int k = 0; k < genes; ++k)
                g[(size_t) k] = s.uiGenome[(size_t) k].load();
            genomeBars->setValues (g.data(), genes);
            readout->set ("GENERATION", juce::String (s.uiGeneration.load()) + "  //  ALPHA " + juce::String (s.uiAlpha.load() + 1));
        }

        SnorkProcessor& fx;
        ui::BarsView* fitnessBars = nullptr;
        ui::BarsView* genomeBars = nullptr;
        ui::BigReadout* readout = nullptr;
    };

    juce::AudioProcessorEditor* SnorkProcessor::createEditor() { return new SnorkEditor (*this); }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SnorkProcessor();
}
