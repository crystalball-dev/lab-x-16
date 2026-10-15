#pragma once

#include "DelayLine.h"

namespace lab::dsp
{
    // ENSEMBLE (LAB X-19): a three-tap, string-machine style chorus. Each side reads three delay taps swept by
    // phase-shifted slow sines plus a faster shimmer, so a single voice becomes a section. Mix 0 keeps the line filled
    // and returns the input untouched.
    class Ensemble
    {
    public:
        void prepare (float sr)
        {
            sampleRate = sr;
            line.prepare ((int) (0.04f * sr) + 8);
            reset();
        }

        void reset() noexcept
        {
            line.reset();
            phaseSlow = phaseFast = 0.0f;
        }

        void process (float* left, float* right, int n, float mix, float depth, float rateHz) noexcept
        {
            mix = std::clamp (mix, 0.0f, 1.0f);
            if (mix < 1.0e-4f)
            {
                for (int i = 0; i < n; ++i)
                    line.push (left[i], right[i]);
                return;
            }
            const float slowInc = std::clamp (rateHz, 0.01f, 10.0f) / sampleRate;
            const float fastInc = 6.3f / sampleRate;
            const float baseDelay = 0.008f * sampleRate;
            const float sweep = 0.006f * sampleRate * std::clamp (depth, 0.0f, 1.0f);
            const float shimmer = 0.00012f * sampleRate * std::clamp (depth, 0.0f, 1.0f);
            const float dryGain = 1.0f - 0.45f * mix;
            const float wetGain = 0.75f * mix / 3.0f * 1.7f;
            for (int i = 0; i < n; ++i)
            {
                line.push (left[i], right[i]);
                phaseSlow = wrap01 (phaseSlow + slowInc);
                phaseFast = wrap01 (phaseFast + fastInc);
                float wl = 0.0f, wr = 0.0f;
                for (int t = 0; t < 3; ++t)
                {
                    const float off = (float) t / 3.0f;
                    const float fast = sin2pi (phaseFast + off) * shimmer;
                    wl += line.readLinear (0, baseDelay + sweep * (0.5f + 0.5f * sin2pi (phaseSlow + off)) + fast);
                    wr += line.readLinear (1, baseDelay + sweep * (0.5f + 0.5f * sin2pi (phaseSlow + off + 0.5f)) - fast);
                }
                left[i] = left[i] * dryGain + wl * wetGain;
                right[i] = right[i] * dryGain + wr * wetGain;
            }
        }

    private:
        StereoDelayLine line;
        float sampleRate = 48000.0f, phaseSlow = 0.0f, phaseFast = 0.0f;
    };
}
