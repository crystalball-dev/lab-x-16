// C-CONSCIOUSNESS // О-СОЗНАНИЕ — Hebbian learning (neurons that fire together wire together) and a pitch-class
// memory. C-Consciousness, many minds fused into one, and Lab X-3's dream of moving a mind into the Noosphere. The
// reverb network's mixing matrix learns from the correlations of its own lines (Oja's rule, each step projected back to
// an orthogonal matrix, so the mixing rotates energy and never drains or adds it), and a memory of the pitch classes that
// pass through tunes combs that ring inside the network: what enters it goes through them, so the space slowly takes
// the shape of the music. (Fed from the network's own output, the combs made a second loop that could oscillate.)
// ABSORB stops the learning and lets what was learned ring, even on noise; TRANSFER lets the learned harmony hum in
// silence. The mind saves with the project.

#include <bit>
#include "core/host/EffectProcessor.h"
#include "core/dsp/Fdn.h"
#include "core/dsp/Filters.h"
#include "core/dsp/Resonator.h"
#include "ui/EffectEditor.h"
#include "ui/Views.h"

namespace
{
    using namespace lab;
    using host::Kind;
    using host::Smooth;

    enum Param : int { memory = 0, forget, harmony, voices, combOctave, absorb, transfer, size, decay, damping, predelay, erase };
    constexpr int N = dsp::Fdn::numLines, numClasses = 12, numFilters = 36, maxVoices = 4;

    const host::EffectInfo& effectInfo()
    {
        static const host::EffectInfo i = []
        {
            host::EffectInfo e;
            e.product = LABX16_PRODUCT;
            e.subtitle = "О-СОЗНАНИЕ";
            e.slug = LABX16_SLUG;
            e.version = LABX16_VERSION;
            e.code = LABX16_CODE;
            e.faction = host::Faction::etherealSignal;
            e.mixLaw = host::MixLaw::equalPower;
            e.params = {
                { "memory",      "Memory",      Kind::real,    0.0f, 1.0f,   0.5f, 0.0f,  Smooth::linear, "%",  "" },
                { "forget",      "Forget",      Kind::real,    0.0f, 1.0f,   0.2f, 0.0f,  Smooth::linear, "%",  "" },
                { "harmony",     "Harmony",     Kind::real,    0.0f, 1.0f,   0.6f, 0.0f,  Smooth::linear, "%",  "" },
                { "voices",      "Voices",      Kind::integer, 1.0f, 4.0f,   3.0f, 0.0f,  Smooth::none,   "",   "" },
                { "comb_octave", "Comb Octave", Kind::integer, 2.0f, 5.0f,   3.0f, 0.0f,  Smooth::none,   "",   "" },
                { "absorb",      "Absorb",      Kind::choice,  0.0f, 1.0f,   0.0f, 0.0f,  Smooth::none,   "",   "Off|On" },
                { "transfer",    "Transfer",    Kind::real,    0.0f, 1.0f,   0.0f, 0.0f,  Smooth::linear, "%",  "" },
                { "size",        "Size",        Kind::real,    0.0f, 1.0f,   0.7f, 0.0f,  Smooth::linear, "%",  "" },
                { "decay",       "Decay",       Kind::real,    0.0f, 1.0f,   0.6f, 0.0f,  Smooth::linear, "%",  "" },
                { "damping",     "Damping",     Kind::real,    0.0f, 1.0f,   0.4f, 0.0f,  Smooth::linear, "%",  "" },
                { "predelay",    "Pre-delay",   Kind::real,    0.0f, 250.0f, 10.0f, 60.0f, Smooth::linear, "ms", "" },
                { "erase",       "Erase",       Kind::choice,  0.0f, 1.0f,   0.0f, 0.0f,  Smooth::none,   "",   "Off|On" },
            };
            e.stages = { "fdn" };
            e.presets = {
                { "Init", {} },
                { "Absorbing", { { "mix", 0.3f }, { "memory", 0.8f }, { "forget", 0.05f } } },
                { "Collective", { { "mix", 0.3f }, { "voices", 4.0f }, { "harmony", 0.8f } } },
                { "Transfer Complete", { { "mix", 0.3f }, { "transfer", 0.5f }, { "memory", 0.7f } } },
                { "Forgetful", { { "mix", 0.3f }, { "forget", 1.0f } } },
                { "Key of the Song", { { "mix", 0.3f }, { "harmony", 1.0f }, { "memory", 0.9f }, { "decay", 0.7f } } },
            };
            return e;
        }();
        return i;
    }

    // The normalised 8 x 8 Hadamard, the mind before it has learned anything.
    std::array<float, N * N> hadamardMatrix()
    {
        std::array<float, N * N> h {};
        for (int r = 0; r < N; ++r)
            for (int c = 0; c < N; ++c)
                h[(size_t) (r * N + c)] = (std::popcount ((unsigned) (r & c)) & 1 ? -1.0f : 1.0f) * 0.35355339f;
        return h;
    }

    class ConsciousnessChain final : public host::Chain
    {
    public:
        std::array<std::atomic<float>, N * N> uiMatrix {};
        std::array<std::atomic<float>, numClasses> uiClasses {};

        ConsciousnessChain() { eraseMind(); }

        void prepare (double sampleRate, int) override
        {
            sr = (float) sampleRate;
            fdn.prepare (sr, 2.6f, 0.26f);
            for (int k = 0; k < numFilters; ++k)
                analysers[(size_t) k].setHz (dsp::midiToHz (36.0f + (float) k), 30.0f, sr);   // C2 .. B4
            for (auto& c : combs)
                c.prepare (sr, 30.0f);
            glide = dsp::onePoleCoef (0.05f, sr);
            silenceLimit = (int64_t) (2.0 * sampleRate);
            noise.seed (0xC0C0u);
            reset();
        }

        void reset() override
        {
            fdn.reset();
            for (auto& a : analysers) a.reset();
            for (auto& c : combs) c.reset();
            silent = 0;
        }

        void flush() override
        {
            fdn.flush();
            reset();
        }

        double tailSeconds (const float* v) const override
        {
            return std::min (60.0, 0.3 * std::exp2 (6.0 * (double) v[decay]) * 1.5 + (double) v[predelay] * 0.001 + 1.0);
        }

        void control (const float* v, const host::BlockContext& ctx, int n) override
        {
            playingNow = ctx.transport->playing;
            const bool absorbing = v[absorb] > 0.5f;
            const bool eraseNow = v[erase] > 0.5f;
            if (eraseNow && ! lastErase)
                eraseMind();
            lastErase = eraseNow;

            settings.scale = 0.3f + 2.2f * v[size];
            settings.rt60 = 0.3f * std::exp2 (std::clamp (v[decay], 0.0f, 1.0f) * 6.0f);
            settings.dampHz = 9000.0f * std::exp2 (-std::clamp (v[damping], 0.0f, 1.0f) * 2.6f);
            settings.predelaySeconds = v[predelay] * 0.001f;
            settings.matrix = matrix.data();
            learnRate = absorbing ? 0.0f : v[memory] * 1.0e-4f * (float) n;
            forgetRate = absorbing ? 0.0f : v[forget] * 1.0e-3f * (float) n / 16.0f;
            classRate = absorbing ? 0.0f : v[memory] * 2.0f;
            classDecay = absorbing ? 1.0f : std::exp (-v[forget] * (float) n / (sr * 5.0f));
            harmonyGain = v[harmony] * 3.0f * (1.0f - combLoop);   // the combs' peaks at unity, then up to +12 dB
            transferLevel = v[transfer];

            // The comb voices: the strongest pitch classes above a quarter of the strongest, at the octave asked for.
            std::array<int, numClasses> order {};
            for (int k = 0; k < numClasses; ++k)
                order[(size_t) k] = k;
            std::stable_sort (order.begin(), order.end(), [this] (int a, int b) { return classes[(size_t) a] > classes[(size_t) b]; });
            const float top = classes[(size_t) order[0]];
            const int wanted = std::clamp ((int) std::lround (v[voices]), 1, maxVoices);
            activeCombs = 0;
            for (int k = 0; k < wanted; ++k)
            {
                const int pc = order[(size_t) k];
                if (top <= 1.0e-6f || classes[(size_t) pc] < 0.25f * top)
                    break;
                const float note = 12.0f * (std::lround (v[combOctave]) + 1.0f) + (float) pc;
                combs[(size_t) activeCombs].set (dsp::midiToHz (note), combLoop, n, glide);
                combNotes[(size_t) activeCombs] = (int) note;
                ++activeCombs;
            }
        }

        void process (float* l, float* r, const float*, const float*, int n, host::Guard& guard) override
        {
            // Listen: the pitch classes of what comes in, and whether anything comes in at all.
            std::array<float, numClasses> energy {};
            bool anyInput = false;
            for (int i = 0; i < n; ++i)
            {
                const float mono = 0.5f * (l[i] + r[i]);
                anyInput = anyInput || std::abs (mono) > 1.0e-4f;
                for (int k = 0; k < numFilters; ++k)
                {
                    const float b = analysers[(size_t) k].bandpass (mono);
                    energy[(size_t) (k % numClasses)] += b * b;
                }
            }
            silent = anyInput ? 0 : silent + n;
            for (int k = 0; k < numClasses; ++k)
                classes[(size_t) k] = classes[(size_t) k] * classDecay + energy[(size_t) k] * classRate;
            float top = 0.0f;
            for (const auto c : classes)
                top = std::max (top, c);
            if (top > 4.0f)
                for (auto& c : classes)
                    c *= 4.0f / top;   // the memory is relative: keep its numbers in range

            // The learned harmony: what enters (and, with TRANSFER, a breath of noise in silence) through the combs, into
            // the network, where the learned pitches ring on.
            std::array<float, host::controlBlock> inject {};
            const bool transferring = transferLevel > 0.0f && silent > silenceLimit && playingNow;
            for (int i = 0; i < n; ++i)
            {
                float x = 0.5f * (l[i] + r[i]);
                if (transferring)
                    x += noise.bi() * 0.004f * transferLevel;   // -48 dBFS
                float c = 0.0f;
                for (int k = 0; k < activeCombs; ++k)
                    c += combs[(size_t) k].process (x);
                inject[(size_t) i] = c * harmonyGain;
            }
            settings.extraIn = inject.data();
            fdn.process (l, r, n, settings);
            guard (0, l, r, n);

            learn();
        }

        void meter (host::Meters& m) const override
        {
            m.custom[0].store (spectralNorm);
            m.custom[1].store ((float) activeCombs);
            m.custom[2].store (activeCombs > 0 ? (float) combNotes[0] : -1.0f);
            m.custom[3].store (activeCombs > 1 ? (float) combNotes[1] : -1.0f);
            m.custom[4].store (activeCombs > 2 ? (float) combNotes[2] : -1.0f);
            float distance = 0.0f;
            const auto h = hadamardMatrix();
            for (int k = 0; k < N * N; ++k)
                distance += std::abs (matrix[(size_t) k] - h[(size_t) k]);
            m.custom[5].store (distance);   // how far the mind has moved from where it started
        }

        int blobVersion() const override { return 1; }
        void saveBlob (juce::MemoryOutputStream& out) const override
        {
            for (const auto x : matrix)
                out.writeFloat (x);
            for (const auto x : classes)
                out.writeFloat (x);
        }
        bool loadBlob (juce::MemoryInputStream& in, int version) override
        {
            if (version != 1 || in.getNumBytesRemaining() < (int64_t) (sizeof (float) * (N * N + numClasses)))
                return false;
            for (auto& x : matrix)
                x = in.readFloat();
            for (auto& x : classes)
                x = in.readFloat();
            // A mind saved by this version is orthogonal to float precision and loads untouched (projecting it again
            // would change its last bits and the round trip would not be exact); a damaged one is taken back to the
            // nearest orthogonal matrix.
            if (estimateNorm() > 1.001f || ! nearlyOrthogonal())
            {
                clampNorm();
                orthogonalise (8);
                clampNorm();
            }
            publish();
            return true;
        }

    private:
        void eraseMind()
        {
            matrix = hadamardMatrix();
            classes.fill (0.0f);
            for (int k = 0; k < N; ++k)
                powerVector[(size_t) k] = 1.0f / std::sqrt ((float) N) * (k % 2 == 0 ? 1.0f : 0.8f);
            spectralNorm = 1.0f;
            publish();
        }

        // Oja's rule on the lines' correlation (from their outputs at the end of the block), FORGET pulling toward the
        // Hadamard, the spectral norm held at or below 1, then two steps back to an orthogonal matrix.
        void learn() noexcept
        {
            if (learnRate > 0.0f || forgetRate > 0.0f)
            {
                const auto& o = fdn.lineOutputs();
                const auto h = hadamardMatrix();
                float scale = 0.0f;
                for (const auto x : o)
                    scale += x * x;
                const float norm = scale > 1.0e-12f ? 1.0f / scale : 0.0f;   // correlations of the normalised outputs
                for (int i = 0; i < N; ++i)
                    for (int j = 0; j < N; ++j)
                    {
                        auto& m = matrix[(size_t) (i * N + j)];
                        const float cij = o[(size_t) i] * o[(size_t) j] * norm, cjj = o[(size_t) j] * o[(size_t) j] * norm;
                        m += learnRate * (cij - m * cjj) + forgetRate * (h[(size_t) (i * N + j)] - m);
                    }
                clampNorm();
                orthogonalise (2);
                clampNorm();
            }
            publish();
        }

        // Newton-Schulz steps toward the nearest orthogonal matrix (the polar factor): M <- M (3I - M^T M) / 2. From
        // singular values at most 1 (clampNorm ran first) a step keeps them at most 1 and draws them to 1 quadratically.
        void orthogonalise (int steps) noexcept
        {
            for (int step = 0; step < steps; ++step)
            {
                std::array<float, N * N> mtm {}, next {};
                for (int i = 0; i < N; ++i)
                    for (int j = 0; j < N; ++j)
                    {
                        float acc = 0.0f;
                        for (int k = 0; k < N; ++k)
                            acc += matrix[(size_t) (k * N + i)] * matrix[(size_t) (k * N + j)];
                        mtm[(size_t) (i * N + j)] = acc;
                    }
                for (int i = 0; i < N; ++i)
                    for (int j = 0; j < N; ++j)
                    {
                        float acc = 0.0f;
                        for (int k = 0; k < N; ++k)
                            acc += matrix[(size_t) (i * N + k)] * ((k == j ? 3.0f : 0.0f) - mtm[(size_t) (k * N + j)]);
                        next[(size_t) (i * N + j)] = 0.5f * acc;
                    }
                matrix = next;
            }
        }

        bool nearlyOrthogonal() const noexcept
        {
            for (int i = 0; i < N; ++i)
                for (int j = 0; j < N; ++j)
                {
                    float acc = 0.0f;
                    for (int k = 0; k < N; ++k)
                        acc += matrix[(size_t) (k * N + i)] * matrix[(size_t) (k * N + j)];
                    if (std::abs (acc - (i == j ? 1.0f : 0.0f)) > 1.0e-3f)
                        return false;
                }
            return true;
        }

        // Eight power iterations on M^T M (warm-started from the last vector) estimate the largest singular value.
        float estimateNorm() noexcept
        {
            for (int pass = 0; pass < 8; ++pass)
            {
                std::array<float, N> mv {}, mtmv {};
                for (int i = 0; i < N; ++i)
                    for (int j = 0; j < N; ++j)
                        mv[(size_t) i] += matrix[(size_t) (i * N + j)] * powerVector[(size_t) j];
                for (int j = 0; j < N; ++j)
                    for (int i = 0; i < N; ++i)
                        mtmv[(size_t) j] += matrix[(size_t) (i * N + j)] * mv[(size_t) i];
                float len = 0.0f;
                for (const auto x : mtmv)
                    len += x * x;
                len = std::sqrt (len);
                if (len < 1.0e-12f)
                    break;
                for (int j = 0; j < N; ++j)
                    powerVector[(size_t) j] = mtmv[(size_t) j] / len;
                spectralNorm = std::sqrt (len);
            }
            return spectralNorm;
        }

        // Holds the spectral norm at or below 1 (a hair under: the estimate is from below).
        void clampNorm() noexcept
        {
            estimateNorm();
            if (spectralNorm > 1.0f)
            {
                const float s = 0.9999f / spectralNorm;   // a hair under 1: the estimate is from below
                for (auto& m : matrix)
                    m *= s;
                spectralNorm *= s;
            }
        }

        void publish() noexcept
        {
            for (int k = 0; k < N * N; ++k)
                uiMatrix[(size_t) k].store (matrix[(size_t) k]);
            float top = 1.0e-9f;
            for (const auto c : classes)
                top = std::max (top, c);
            for (int k = 0; k < numClasses; ++k)
                uiClasses[(size_t) k].store (classes[(size_t) k] / top);
        }

        dsp::Fdn fdn;
        dsp::Fdn::Settings settings;
        std::array<float, N * N> matrix {};
        std::array<float, N> powerVector {};
        std::array<float, numClasses> classes {};
        std::array<dsp::SVF, numFilters> analysers {};
        std::array<dsp::CombFilter, maxVoices> combs;
        std::array<int, maxVoices> combNotes {};
        dsp::Rng noise;
        static constexpr float combLoop = 0.85f;
        float sr = 48000.0f, glide = 0.001f, learnRate = 0.0f, forgetRate = 0.0f, classRate = 1.0f, classDecay = 1.0f;
        float harmonyGain = 0.27f, transferLevel = 0.0f, spectralNorm = 1.0f;
        int activeCombs = 0;
        int64_t silent = 0, silenceLimit = 96000;
        bool lastErase = false, playingNow = false;
    };

    class ConsciousnessProcessor final : public host::EffectProcessor
    {
    public:
        ConsciousnessProcessor() : EffectProcessor (effectInfo(), std::make_unique<ConsciousnessChain>()) {}
        ConsciousnessChain& mind() { return static_cast<ConsciousnessChain&> (getChain()); }
        juce::AudioProcessorEditor* createEditor() override;
    };

    class ConsciousnessEditor final : public ui::EffectEditor
    {
    public:
        explicit ConsciousnessEditor (ConsciousnessProcessor& p) : EffectEditor (p), fx (p)
        {
            matrixView = own (std::make_unique<ui::MatrixView>());
            classBars = own (std::make_unique<ui::BarsView>());
            classBars->setCaption ("PITCH CLASSES");
            classBars->setLabels ({ "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" });
            addRow ({ panel ("MIND // РАЗУМ", { { matrixView, 1.8f }, { classBars, 3.6f } }, true),
                      panel ("LEARNING // ОБУЧЕНИЕ", { { knob ("memory", "MEMORY", true), 1.0f }, { knob ("forget", "FORGET"), 1.0f },
                                                      { choices ({ { "absorb", "ABSORB" }, { "erase", "ERASE" } }), 1.2f } }) });
            addRow ({ panel ("HARMONY // ГАРМОНИЯ", { { knob ("harmony", "HARMONY"), 1.0f }, { knob ("voices", "VOICES"), 1.0f },
                                                     { knob ("comb_octave", "OCTAVE"), 1.0f }, { knob ("transfer", "TRANSFER"), 1.0f } }),
                      panel ("SPACE // ПРОСТРАНСТВО", { { knob ("size", "SIZE"), 1.0f }, { knob ("decay", "DECAY"), 1.0f },
                                                       { knob ("damping", "DAMPING"), 1.0f }, { knob ("predelay", "PRE-DELAY"), 1.0f } }) });
            addRow ({ outputPanel() });
            finish();
        }

    private:
        void refresh (float) override
        {
            auto& m = fx.mind();
            std::array<float, N * N> values {};
            for (int k = 0; k < N * N; ++k)
                values[(size_t) k] = m.uiMatrix[(size_t) k].load() * 2.83f;   // a Hadamard cell is 1/sqrt(8)
            matrixView->setValues (values.data(), N);
            std::array<float, numClasses> classes {};
            int strongest = 0;
            for (int k = 0; k < numClasses; ++k)
            {
                classes[(size_t) k] = m.uiClasses[(size_t) k].load();
                if (classes[(size_t) k] > classes[(size_t) strongest])
                    strongest = k;
            }
            classBars->setValues (classes.data(), numClasses);
            classBars->setHighlight (classes[(size_t) strongest] > 0.01f ? strongest : -1);
        }

        ConsciousnessProcessor& fx;
        ui::MatrixView* matrixView = nullptr;
        ui::BarsView* classBars = nullptr;
    };

    juce::AudioProcessorEditor* ConsciousnessProcessor::createEditor() { return new ConsciousnessEditor (*this); }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ConsciousnessProcessor();
}
