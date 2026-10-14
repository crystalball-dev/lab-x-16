#pragma once

#include <array>
#include "DelayLine.h"
#include "Filters.h"
#include "MicroShift.h"

namespace lab::dsp
{
    // The suite's reverb core: one 8-line feedback delay network (LAB X-3 NOOSPHERE + LAB X-19 SHIMMER REVERB).
    // Pre-delay, an 80 Hz guard on the input, per-line damping and RT60 gains, slow delay modulation on four of the
    // lines but not the two shortest (Hermite reads, whose loss per pass stays small even in SILO's few-millisecond
    // lines), and:
    //   - a modal blend of the mixing matrix, (1 - modal) Hadamard + modal identity (SILO). The normalised Hadamard is
    //     symmetric and orthogonal (eigenvalues +-1), so the blend's eigenvalues are 1 and 2 modal - 1: its spectral
    //     norm stays exactly 1 and no renormalisation is needed;
    //   - up to three shimmer heads at any ratios, band-passed, soft-limited and fed back into the input (WISH GRANTER).
    //     All three heads always run, so a head joining (a new interval) never plays a stale buffer; their gains glide;
    //   - freeze, reached over 50 ms so nothing clicks: line gains to 1, damping, modulation and input out. Once fully
    //     frozen the reads lock to integer delays (interpolation would low-pass the loop a little on every pass) and the
    //     tail holds exactly. Shimmer keeps sounding on the output but is not fed back, which would grow a lossless loop.
    // Output is the wet signal only (the EffectProcessor base mixes with the latency-aligned dry signal).
    class Fdn
    {
    public:
        static constexpr int numLines = 8;
        static constexpr float noosphereMs[numLines] = { 29.7f, 37.1f, 41.1f, 43.7f, 53.3f, 59.9f, 67.7f, 73.1f };

        struct Settings
        {
            const float* baseMs = noosphereMs;   // per-line base lengths (ms), scaled by `scale`
            float scale = 1.4f;                  // size: 0.3 .. 2.5 as NOOSPHERE
            float rt60 = 2.4f;                   // seconds
            float dampHz = 9000.0f;              // damping one-pole corner; fades out toward 0.45 x the sample rate
            float modal = 0.0f;                  // 0 diffuse .. 1 every line its own resonance
            float modulation = 1.0f;             // slow delay modulation depth, 1 = 0.6 ms (NOOSPHERE)
            float predelaySeconds = 0.0f;
            int heads = 0;                       // shimmer heads 0..3
            float ratios[3] { 2.0f, 1.5f, 1.26f };
            float shimmer = 0.0f;                // 0..1
            bool freeze = false;
        };

        void prepare (float sr, float maxScale = 2.6f, float maxPredelaySeconds = 0.26f)
        {
            sampleRate = sr;
            const int maxLen = (int) std::ceil (sr * 0.0731f * maxScale) + (int) std::ceil (0.0013f * sr) + 8;
            for (auto& line : lines)
                line.prepare (maxLen);
            pre.prepare ((int) std::ceil (maxPredelaySeconds * sr) + 8);
            const float g = SVF::gFor (80.0f, sr);
            hpL.set (g, 1.4142f);
            hpR.set (g, 1.4142f);
            for (auto& h : heads)
            {
                h.prepare (sr, 0.09f);
                h.setWindow (0.06f);
            }
            shimHp.setHz (300.0f, sr);
            shimLp.setHz (6000.0f, sr);
            freezeStep = 1.0f / (0.05f * sr);
            headGlide = onePoleCoef (0.02f, sr);
            scale = -1.0f;
            reset();
        }

        void reset() noexcept
        {
            for (auto& line : lines)
                line.reset();
            lineLp.fill (0.0f);
            pre.reset();
            hpL.reset();
            hpR.reset();
            for (auto& h : heads)
                h.reset();
            headGain.fill (0.0f);
            shimHp.reset();
            shimLp.reset();
            shimOut = 0.0f;
            frozen = 0.0f;
            lfo1 = 0.0f;
            lfo2 = 0.37f;
        }

        // Preset changes: clear the tail and jump to the new settings.
        void flush() noexcept
        {
            reset();
            scale = -1.0f;
        }

        // Current line lengths in samples (harness readout for SILO's LEVEL staircase).
        float lineLength (int k, const Settings& s) const noexcept { return s.baseMs[k] * 0.001f * sampleRate * s.scale; }

        // 0 live .. 1 fully frozen.
        float frozenAmount() const noexcept { return frozen; }

        void process (float* left, float* right, int n, const Settings& s) noexcept
        {
            if (scale < 0.0f)
                scale = s.scale;
            const float rt60 = std::max (0.05f, s.rt60);
            float gains[numLines];
            float gainSum = 0.0f;
            for (int k = 0; k < numLines; ++k)
            {
                const float len = s.baseMs[k] * 0.001f * sampleRate * s.scale;
                gains[k] = std::pow (10.0f, -3.0f * len / (rt60 * sampleRate));
                gainSum += gains[k];
            }
            const float gMean = gainSum / (float) numLines;
            const float energyRatio = std::max (1.0f - gMean * gMean, 0.0004f) / 0.19f;
            const float inputGain = 0.35f * std::pow (energyRatio, 0.2f);

            // Damping fades out between 0.3 and 0.45 x the sample rate, so asking for none (SILO at damping 0) is
            // exactly none and the coefficient never jumps on the way there.
            const float dampHz = std::max (200.0f, s.dampHz);
            float dampCoef = onePoleCoefHz (std::min (dampHz, 0.45f * sampleRate), sampleRate);
            if (dampHz > 0.3f * sampleRate)
                dampCoef += (1.0f - dampCoef) * std::min (1.0f, (dampHz - 0.3f * sampleRate) / (0.15f * sampleRate));

            const float modDepth = 0.0006f * sampleRate * std::clamp (s.modulation, 0.0f, 2.0f);
            const float modal = std::clamp (s.modal, 0.0f, 1.0f);
            const float predelay = std::clamp (s.predelaySeconds * sampleRate, 0.0f, (float) pre.capacity());
            const int headCount = std::clamp (s.heads, 0, 3);
            const float shimGain = 0.55f * std::clamp (s.shimmer, 0.0f, 1.0f);
            const float lfoInc1 = 0.13f / sampleRate, lfoInc2 = 0.21f / sampleRate;
            const float freezeTarget = s.freeze ? 1.0f : 0.0f;

            for (int i = 0; i < n; ++i)
            {
                if (frozen != freezeTarget)
                    frozen = freezeTarget > frozen ? std::min (freezeTarget, frozen + freezeStep)
                                                   : std::max (freezeTarget, frozen - freezeStep);
                const float live = 1.0f - frozen;
                const bool locked = frozen >= 1.0f;
                if (! locked)
                {
                    scale += (s.scale - scale) * 0.0005f;   // the frozen geometry holds
                    lfo1 = wrap01 (lfo1 + lfoInc1);
                    lfo2 = wrap01 (lfo2 + lfoInc2);
                }
                const float m1 = sin2pi (lfo1) * modDepth * live;
                const float m2 = sin2pi (lfo2) * modDepth * live;

                float o[numLines];
                for (int k = 0; k < numLines; ++k)
                {
                    float d = s.baseMs[k] * 0.001f * sampleRate * scale;
                    if (k == 2 || k == 5) d += m1;   // the two shortest lines stay still: the first arrivals (and
                    if (k == 4 || k == 7) d += m2;   // so the pre-delay) land on exact samples
                    o[k] = locked ? lines[(size_t) k].readInt (0, std::max (1, (int) std::lround (d) - 1))
                                  : lines[(size_t) k].readHermite (0, (double) std::max (1.0f, d - 1.0f));
                }
                const float wetL = (o[0] - o[2] + o[4] - o[6]) * 0.5f;
                const float wetR = (o[1] - o[3] + o[5] - o[7]) * 0.5f;

                const float dc = dampCoef + (1.0f - dampCoef) * frozen;
                float f[numLines], h[numLines];
                for (int k = 0; k < numLines; ++k)
                {
                    lineLp[(size_t) k] += (o[k] - lineLp[(size_t) k]) * dc;
                    const float g = gains[k] + (1.0f - gains[k]) * frozen;
                    f[k] = h[k] = lineLp[(size_t) k] * g;
                }
                hadamard (h);
                for (int k = 0; k < numLines; ++k)
                    f[k] = (1.0f - modal) * h[k] + modal * f[k];

                float shimmerIn = 0.0f;
                if (headCount > 0)
                {
                    const float mono = 0.5f * (wetL + wetR);
                    float pitched = 0.0f;
                    for (int k = 0; k < 3; ++k)
                    {
                        const float target = k < headCount ? 1.0f / (float) headCount : 0.0f;
                        headGain[(size_t) k] += (target - headGain[(size_t) k]) * headGlide;
                        pitched += heads[(size_t) k].process (mono, s.ratios[k]) * headGain[(size_t) k];
                    }
                    const float band = shimHp.highpass (shimLp.lowpass (pitched));
                    shimOut = shimGain > 1.0e-4f ? fastTanh (band * shimGain) : 0.0f;
                    shimmerIn = shimOut * live;
                }
                else
                    shimOut = 0.0f;

                pre.push (left[i], right[i]);
                const float pl = predelay > 0.0f ? pre.readLinear (0, predelay) : left[i];
                const float pr = predelay > 0.0f ? pre.readLinear (1, predelay) : right[i];
                float lp, bp, hl, hr;
                hpL.process (pl, lp, bp, hl);
                hpR.process (pr, lp, bp, hr);
                const float inL = (hl + shimmerIn) * inputGain * live;
                const float inR = (hr + shimmerIn) * inputGain * live;
                for (int k = 0; k < numLines; ++k)
                    lines[(size_t) k].push ((k & 1) == 0 ? f[k] + inL : f[k] + inR);

                const float frozenShimmer = shimOut * 0.5f * frozen;
                left[i] = wetL + frozenShimmer;
                right[i] = wetR + frozenShimmer;
            }
        }

    private:
        static void hadamard (float* x) noexcept
        {
            for (int h = 1; h < numLines; h <<= 1)
                for (int i = 0; i < numLines; i += h << 1)
                    for (int j = i; j < i + h; ++j)
                    {
                        const float a = x[j], b = x[j + h];
                        x[j] = a + b;
                        x[j + h] = a - b;
                    }
            const float norm = 0.35355339f;   // 1 / sqrt (8)
            for (int i = 0; i < numLines; ++i)
                x[i] *= norm;
        }

        std::array<DelayLine, numLines> lines {};
        std::array<float, numLines> lineLp {};
        StereoDelayLine pre;
        SVF hpL, hpR;
        std::array<MicroShift, 3> heads;
        std::array<float, 3> headGain {};
        OnePole shimHp, shimLp;
        float sampleRate = 48000.0f, scale = -1.0f, shimOut = 0.0f, lfo1 = 0.0f, lfo2 = 0.37f;
        float frozen = 0.0f, freezeStep = 0.0004f, headGlide = 0.001f;
    };
}
