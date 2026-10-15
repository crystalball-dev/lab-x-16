// 100 RADS // 100 РЕНТГЕН — auction theory, a second-price auction with wealth. The 100 Rads bar, Barkeep's cut,
// Sidorovich's prices, the bandits taking their share. Six bands bid every block for gain out of a fixed loudness
// budget: the winners are boosted and pay the next bid down as tax, which goes to the losers, who are cut but save.
// GREED makes the rich richer (winner takes all); TAX moves the loudness around the spectrum (a fair market). A reigning
// winner keeps its seat until it is outbid by 10 %, so a level's ripple never flips the market; the budget holds the
// K-weighted power of the bands (BS.1770's curve at each band's centre), which is what loudness meters read.

#include "core/host/EffectProcessor.h"
#include "core/dsp/Crossovers.h"
#include "core/dsp/Filters.h"
#include "ui/EffectEditor.h"
#include "ui/Views.h"

#include <complex>

namespace
{
    using namespace lab;
    using host::Kind;
    using host::Smooth;

    enum Param : int { depth = 0, slots, boost, cut, hunger, greed, tax, speed, budget, lowSplit, highSplit };
    constexpr int numBands = 6;

    // The power gain of the ITU-R BS.1770-4 K-weighting (its shelf, then its high-pass) at f.
    double kWeightPower (double f, double sr)
    {
        const double pi = 3.14159265358979323846;
        const std::complex<double> z1 = std::polar (1.0, -2.0 * pi * f / sr);
        auto biquad = [&z1] (double b0, double b1, double b2, double a1, double a2)
        { return (b0 + b1 * z1 + b2 * z1 * z1) / (1.0 + a1 * z1 + a2 * z1 * z1); };
        const double k = std::tan (pi * 1681.974450955533 / sr), q = 0.7071752369554196;
        const double vh = std::pow (10.0, 3.999843853973347 / 20.0), vb = std::pow (10.0, 3.999843853973347 / 40.0);
        const double a0 = 1.0 + k / q + k * k;
        const auto shelf = biquad ((vh + vb * k / q + k * k) / a0, 2.0 * (k * k - vh) / a0, (vh - vb * k / q + k * k) / a0,
                                   2.0 * (k * k - 1.0) / a0, (1.0 - k / q + k * k) / a0);
        const double k1 = std::tan (pi * 38.13547087602444 / sr), q1 = 0.5003270373238773;
        const double a01 = 1.0 + k1 / q1 + k1 * k1;
        const auto highPass = biquad (1.0, -2.0, 1.0, 2.0 * (k1 * k1 - 1.0) / a01, (1.0 - k1 / q1 + k1 * k1) / a01);
        return std::norm (shelf * highPass);
    }

    const host::EffectInfo& effectInfo()
    {
        static const host::EffectInfo i = []
        {
            host::EffectInfo e;
            e.product = LABX16_PRODUCT;
            e.subtitle = "100 РЕНТГЕН";
            e.slug = LABX16_SLUG;
            e.version = LABX16_VERSION;
            e.code = LABX16_CODE;
            e.faction = host::Faction::deepZone;
            e.params = {
                { "depth",      "Depth",      Kind::real,    0.0f,    1.0f,     0.7f,    0.0f,    Smooth::linear, "%",  "" },
                { "slots",      "Winners",    Kind::integer, 1.0f,    3.0f,     1.0f,    0.0f,    Smooth::none,   "",   "" },
                { "boost",      "Boost",      Kind::real,    0.0f,    12.0f,    6.0f,    0.0f,    Smooth::linear, "dB", "" },
                { "cut",        "Cut",        Kind::real,    0.0f,    24.0f,    9.0f,    0.0f,    Smooth::linear, "dB", "" },
                { "hunger",     "Hunger",     Kind::real,    0.0f,    1.0f,     0.5f,    0.0f,    Smooth::linear, "%",  "" },
                { "greed",      "Greed",      Kind::real,    0.0f,    1.0f,     0.5f,    0.0f,    Smooth::linear, "%",  "" },
                { "tax",        "Tax",        Kind::real,    0.0f,    1.0f,     0.3f,    0.0f,    Smooth::linear, "%",  "" },
                { "speed",      "Speed",      Kind::real,    0.0f,    1.0f,     0.5f,    0.0f,    Smooth::linear, "%",  "" },
                { "budget",     "Budget",     Kind::real,    0.0f,    6.0f,     0.0f,    0.0f,    Smooth::linear, "dB", "" },
                { "low_split",  "Low Split",  Kind::real,    60.0f,   300.0f,   120.0f,  120.0f,  Smooth::log,    "Hz", "" },
                { "high_split", "High Split", Kind::real,    2000.0f, 10000.0f, 5000.0f, 5000.0f, Smooth::log,    "Hz", "" },
            };
            e.stages = { "auction" };
            e.presets = {
                { "Init", {} },
                { "Barkeep's Cut", { { "cut", 12.0f }, { "tax", 0.4f }, { "greed", 0.3f }, { "output", -0.2f } } },
                { "Fair Trade", { { "tax", 1.0f }, { "greed", 0.2f }, { "boost", 4.0f }, { "cut", 6.0f }, { "output", 0.0f } } },
                { "Bandit Tax", { { "cut", 24.0f }, { "tax", 0.6f }, { "boost", 3.0f }, { "output", -0.2f } } },
                { "Sidorovich", { { "speed", 0.1f }, { "greed", 0.7f }, { "tax", 0.2f }, { "output", -0.1f } } },
                { "Winner Takes All", { { "tax", 0.0f }, { "greed", 1.0f }, { "boost", 8.0f }, { "output", -0.2f } } },
                { "Quiet Market", { { "depth", 0.3f }, { "output", -0.1f } } },
            };
            return e;
        }();
        return i;
    }

    class HundredRadsChain final : public host::Chain
    {
    public:
        std::array<std::atomic<float>, numBands> uiWealth {}, uiGain {};
        std::atomic<int> uiWinner { 0 };

        void prepare (double sampleRate, int) override
        {
            sr = (float) sampleRate;
            envCoef = dsp::onePoleCoef (0.03f, sr);
            engageStep = 1.0f / (0.02f * sr);
            splitsSet = false;
            reset();
        }

        void reset() override
        {
            for (auto& c : chan)
            {
                for (auto& x : c.xo) x.reset();
                for (auto& row : c.ap) for (auto& a : row) a.reset();
            }
            env.fill (0.0f);
            wealth.fill (0.5f);
            gainDb.fill (0.0f);
            appliedDb.fill (0.0f);
            linNow.fill (1.0f);
            reigning.fill (false);
            engaged = 0.0f;
            lastBeat = std::numeric_limits<int64_t>::min();
        }

        double tailSeconds (const float*) const override { return 0.05; }

        void control (const float* v, const host::BlockContext& ctx, int n) override
        {
            // The splits: LOW and HIGH at the ends, three more geometric between them.
            if (! splitsSet || std::abs (v[lowSplit] - lowHz) > 0.5f || std::abs (v[highSplit] - highHz) > 2.0f)
            {
                lowHz = v[lowSplit];
                highHz = v[highSplit];
                for (int k = 0; k < numBands - 1; ++k)
                    splits[(size_t) k] = lowHz * std::pow (highHz / lowHz, (float) k / (float) (numBands - 2));
                for (int b = 0; b < numBands; ++b)
                {
                    const double lo = b == 0 ? 30.0 : (double) splits[(size_t) b - 1];
                    const double hi = b == numBands - 1 ? std::min (16000.0, 0.45 * (double) sr) : (double) splits[(size_t) b];
                    kWeight[(size_t) b] = kWeightPower (std::sqrt (lo * hi), (double) sr);
                }
                for (auto& c : chan)
                    for (int k = 0; k < numBands - 1; ++k)
                    {
                        c.xo[(size_t) k].set (splits[(size_t) k], sr);
                        for (int j = 0; j < numBands - 1; ++j)
                            c.ap[(size_t) k][(size_t) j].set (dsp::SVF::gFor (splits[(size_t) j], sr), 1.41421356f);
                    }
                splitsSet = true;
            }
            depthNow = v[depth];

            // The auction, every block: bids are band energy (0..1 over -60..0 dB) lifted by wealth and hunger.
            std::array<float, numBands> e {}, bid {};
            for (int b = 0; b < numBands; ++b)
            {
                e[(size_t) b] = std::clamp ((dsp::gainToDb (std::sqrt (env[(size_t) b]) + 1.0e-9f) + 60.0f) / 60.0f, 0.0f, 1.0f);
                bid[(size_t) b] = e[(size_t) b] * (1.0f + v[hunger] * wealth[(size_t) b]);
            }
            std::array<int, numBands> order {};
            for (int b = 0; b < numBands; ++b)
                order[(size_t) b] = b;
            // A reigning winner's bid stands 10 % taller (a challenger must clearly outbid it); ties go to the lower band.
            std::array<float, numBands> standing {};
            for (int b = 0; b < numBands; ++b)
                standing[(size_t) b] = bid[(size_t) b] * (reigning[(size_t) b] ? 1.1f : 1.0f);
            std::stable_sort (order.begin(), order.end(), [&] (int a, int b) { return standing[(size_t) a] > standing[(size_t) b] + 1.0e-6f; });
            const int winners = std::clamp ((int) std::lround (v[slots]), 1, 3);
            const float price = bid[(size_t) order[(size_t) winners]];
            std::array<bool, numBands> won {};
            for (int k = 0; k < winners; ++k)
                won[(size_t) order[(size_t) k]] = true;
            reigning = won;
            if (order[0] != winner)
            {
                ++winnerChanges;
                winner = order[0];
            }

            // Once per beat (the host's while it plays, every half second while stopped) the money moves.
            const auto& t = *ctx.transport;
            const int64_t beat = t.playing ? (int64_t) std::floor (t.ppq) : ctx.sampleClock / std::max<int64_t> (1, (int64_t) (0.5f * sr));
            if (beat != lastBeat)
            {
                if (lastBeat != std::numeric_limits<int64_t>::min())
                {
                    float collected = 0.0f;
                    for (int b = 0; b < numBands; ++b)
                        if (won[(size_t) b])
                        {
                            const float paid = std::min (wealth[(size_t) b], v[tax] * price);
                            wealth[(size_t) b] += v[greed] * 0.1f * (1.0f - wealth[(size_t) b]) - paid;
                            collected += paid;
                        }
                    for (int b = 0; b < numBands; ++b)
                        if (! won[(size_t) b])
                            wealth[(size_t) b] += collected / (float) (numBands - winners);
                    for (auto& w : wealth)
                        w = std::clamp (w, 0.0f, 1.0f);
                }
                lastBeat = beat;
            }

            // Target gains (winners +BOOST, losers -CUT x (1 - W / 2)), smoothed with SPEED. Then one shift for every
            // band holds the bands' K-weighted power where it was (or lets it rise up to BUDGET), computed on the gains
            // as they stand, so a market in motion keeps its loudness too (a new winner rises slower than the old one
            // falls); a few passes, because bands at +BOOST or -CUT stop taking the shift.
            const float attack = 0.001f + 0.049f * (1.0f - v[speed]), release = 0.02f + 0.48f * (1.0f - v[speed]);
            for (int b = 0; b < numBands; ++b)
            {
                const float goal = won[(size_t) b] ? v[boost] : -v[cut] * (1.0f - 0.5f * wealth[(size_t) b]);
                auto& g = gainDb[(size_t) b];
                const float tau = goal < g ? attack : release;
                g += (goal - g) * (1.0f - std::exp (-(float) n / (tau * sr)));
            }
            auto netDb = [&] (float s)
            {
                double before = 0.0, after = 0.0;
                for (int b = 0; b < numBands; ++b)
                {
                    const double p = (double) env[(size_t) b] * kWeight[(size_t) b];
                    before += p;
                    after += p * std::pow (10.0, 0.1 * (double) depthNow * (double) std::clamp (gainDb[(size_t) b] + s, -v[cut], v[boost]));
                }
                return before > 1.0e-12 ? (float) (10.0 * std::log10 (after / before)) : 0.0f;
            };
            const float wanted = std::clamp (netDb (0.0f), 0.0f, v[budget]);
            float shift = 0.0f;
            if (depthNow > 1.0e-3f)
                for (int pass = 0; pass < 4; ++pass)
                    shift = std::clamp (shift + (wanted - netDb (shift)) / depthNow, -(v[cut] + v[boost]), v[cut] + v[boost]);
            for (int b = 0; b < numBands; ++b)
            {
                appliedDb[(size_t) b] = std::clamp (gainDb[(size_t) b] + shift, -v[cut], v[boost]);
                minGain = std::min (minGain, appliedDb[(size_t) b]);
                maxGain = std::max (maxGain, appliedDb[(size_t) b]);
                uiWealth[(size_t) b].store (wealth[(size_t) b]);
                uiGain[(size_t) b].store (appliedDb[(size_t) b]);
            }
            uiWinner.store (winner);
        }

        void process (float* l, float* r, const float*, const float*, int n, host::Guard& guard) override
        {
            // At DEPTH 0 the stage hands back its input untouched (no crossover phase), crossfading over 20 ms.
            const bool processing = depthNow > 0.0005f;
            std::array<float, numBands> lin {}, step {};
            for (int b = 0; b < numBands; ++b)
            {
                const float target = dsp::dbToGain (depthNow * appliedDb[(size_t) b]);
                lin[(size_t) b] = linNow[(size_t) b];
                step[(size_t) b] = (target - linNow[(size_t) b]) / (float) std::max (1, n);
                linNow[(size_t) b] = target;
            }
            float* io[2] = { l, r };
            for (int i = 0; i < n; ++i)
            {
                engaged = std::clamp (engaged + (processing ? engageStep : -engageStep), 0.0f, 1.0f);
                float bands[2][numBands];
                for (int ch = 0; ch < 2; ++ch)
                    split (chan[(size_t) ch], io[ch][i], bands[ch]);
                for (int b = 0; b < numBands; ++b)
                {
                    const float x = 0.5f * (bands[0][b] * bands[0][b] + bands[1][b] * bands[1][b]);
                    env[(size_t) b] += (x - env[(size_t) b]) * envCoef;
                }
                if (engaged <= 0.0f)
                    continue;
                for (int b = 0; b < numBands; ++b)
                    lin[(size_t) b] += step[(size_t) b];   // the gains move sample by sample across the block
                for (int ch = 0; ch < 2; ++ch)
                {
                    float y = 0.0f;
                    for (int b = 0; b < numBands; ++b)
                        y += bands[ch][b] * lin[(size_t) b];
                    io[ch][i] += (y - io[ch][i]) * engaged;
                }
            }
            guard (0, l, r, n);
        }

        void meter (host::Meters& m) const override
        {
            m.custom[0].store ((float) winner);
            m.custom[1].store ((float) winnerChanges);
            m.custom[2].store (minGain);
            m.custom[3].store (maxGain);
            float lo = 1.0f, hi = 0.0f;
            for (const auto w : wealth) { lo = std::min (lo, w); hi = std::max (hi, w); }
            m.custom[4].store (lo);
            m.custom[5].store (hi);
        }

    private:
        struct Channel
        {
            std::array<dsp::LR4, numBands - 1> xo;
            std::array<std::array<dsp::SVF, numBands - 1>, numBands - 1> ap {};   // ap[band][split]
        };

        // Six bands from a Linkwitz-Riley tree, each passed through the all-passes of the splits above it, so the
        // bands sum to one all-pass (flat).
        static void split (Channel& c, float x, float* out) noexcept
        {
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
                out[k] = low;
                rest = high;
            }
            out[numBands - 1] = rest;
        }

        std::array<Channel, 2> chan;
        std::array<float, numBands - 1> splits {};
        std::array<float, numBands> env {}, wealth {}, gainDb {};
        std::array<float, numBands> appliedDb {}, linNow {};
        std::array<double, numBands> kWeight {};
        std::array<bool, numBands> reigning {};
        float sr = 48000.0f, envCoef = 0.001f, engageStep = 0.001f, engaged = 0.0f, depthNow = 0.7f, lowHz = 120.0f, highHz = 5000.0f;
        float minGain = 0.0f, maxGain = 0.0f;
        int winner = 0;
        int64_t winnerChanges = 0, lastBeat = 0;
        bool splitsSet = false;
    };

    class HundredRadsProcessor final : public host::EffectProcessor
    {
    public:
        HundredRadsProcessor() : EffectProcessor (effectInfo(), std::make_unique<HundredRadsChain>()) {}
        HundredRadsChain& rads() { return static_cast<HundredRadsChain&> (getChain()); }
        juce::AudioProcessorEditor* createEditor() override;
    };

    class HundredRadsEditor final : public ui::EffectEditor
    {
    public:
        explicit HundredRadsEditor (HundredRadsProcessor& p) : EffectEditor (p), fx (p)
        {
            wealthBars = own (std::make_unique<ui::BarsView>());
            wealthBars->setCaption ("WEALTH");
            wealthBars->setLabels ({ "LOW", "2", "3", "4", "5", "HIGH" });
            gainBars = own (std::make_unique<ui::BarsView>());
            gainBars->setCaption ("GAIN");
            gainBars->setBipolar (true);
            gainBars->setLabels ({ "LOW", "2", "3", "4", "5", "HIGH" });
            needle = own (std::make_unique<ui::Needle> ("MARKET", 0.85f));
            addRow ({ panel ("MARKET // РЫНОК", { { wealthBars, 2.6f }, { gainBars, 2.6f }, { needle, 1.7f } }, true) });
            addRow ({ panel ("AUCTION // АУКЦИОН", { { knob ("depth", "DEPTH", true), 1.0f }, { knob ("slots", "WINNERS"), 1.0f },
                                                    { knob ("boost", "BOOST"), 1.0f }, { knob ("cut", "CUT"), 1.0f },
                                                    { knob ("budget", "BUDGET"), 1.0f } }),
                      panel ("ECONOMY // ЭКОНОМИКА", { { knob ("hunger", "HUNGER"), 1.0f }, { knob ("greed", "GREED"), 1.0f },
                                                      { knob ("tax", "TAX"), 1.0f }, { knob ("speed", "SPEED"), 1.0f } }) });
            addRow ({ panel ("BANDS // ПОЛОСЫ", { { knob ("low_split", "LOW"), 1.0f }, { knob ("high_split", "HIGH"), 1.0f } }),
                      outputPanel() });
            finish();
        }

    private:
        void refresh (float seconds) override
        {
            auto& c = fx.rads();
            std::array<float, numBands> w {}, g {};
            const float cutDb = std::max (1.0f, fx.effectRaw (Param::cut)), boostDb = std::max (1.0f, fx.effectRaw (Param::boost));
            for (int b = 0; b < numBands; ++b)
            {
                w[(size_t) b] = c.uiWealth[(size_t) b].load();
                const float db = c.uiGain[(size_t) b].load();
                g[(size_t) b] = db >= 0.0f ? db / boostDb : db / cutDb;
            }
            const int winner = c.uiWinner.load();
            wealthBars->setValues (w.data(), numBands);
            wealthBars->setHighlight (winner);
            gainBars->setValues (g.data(), numBands);
            gainBars->setHighlight (winner);
            static const char* names[numBands] = { "LOW", "LOW-MID", "MID", "UPPER", "PRESENCE", "HIGH" };
            needle->setReadout (names[std::clamp (winner, 0, numBands - 1)]);
            needle->advance (w[(size_t) std::clamp (winner, 0, numBands - 1)], seconds);
        }

        HundredRadsProcessor& fx;
        ui::BarsView* wealthBars = nullptr;
        ui::BarsView* gainBars = nullptr;
        ui::Needle* needle = nullptr;
    };

    juce::AudioProcessorEditor* HundredRadsProcessor::createEditor() { return new HundredRadsEditor (*this); }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new HundredRadsProcessor();
}
