#pragma once

#include "DelayLine.h"

namespace lab::dsp
{
    // MICROSHIFT (LAB X-19's shimmer heads, standalone): two read heads sliding through a short buffer, crossfaded
    // with sin^2 windows, shift the pitch by any ratio. Window 20-80 ms. Ratio > 1 shifts up, < 1 down.
    //
    // Each head's delay changes by (1 - ratio) per sample and wraps within [2, 2 + window], where its window weight is
    // zero; the second head sits half a window away, so the two weights' squares always sum to 1. Written this way the
    // delay is continuous when the ratio crosses 1 (a detune walk through zero). Wave 1's version mirrored the heads'
    // positions when the direction changed and played a plain 2-sample delay at a ratio of exactly 1, so a walk through
    // zero jumped (RAINBOW's damaged antenna); for ratios that stay on one side of 1 the two versions are identical.
    // At a ratio of exactly 1 the heads hold still, a two-tap comb; callers that rest at no shift bypass the shifter.
    class MicroShift
    {
    public:
        void prepare (float sr, float maxWindowSeconds = 0.09f)
        {
            sampleRate = sr;
            line.prepare ((int) std::ceil (maxWindowSeconds * sr) + 8);
            setWindow (0.05f);
            reset();
        }

        void reset() noexcept
        {
            line.reset();
            phase = 0.0f;
        }

        void setWindow (float seconds) noexcept
        {
            window = std::clamp (seconds * sampleRate, 32.0f, (float) line.capacity() - 4.0f);
        }

        // One sample in, one shifted sample out.
        float process (float x, float ratio) noexcept
        {
            line.push (x);
            phase = wrap01 (phase - (ratio - 1.0f) / window);
            const float p2 = wrap01 (phase + 0.5f);
            const float w1 = sin2pi (0.5f * phase), w2 = sin2pi (0.5f * p2);
            return line.readLinear (0, 2.0f + window * phase) * w1 * w1 + line.readLinear (0, 2.0f + window * p2) * w2 * w2;
        }

        // The average delay the heads add (half a window): what a caller compensates when it lines a shifted copy up
        // with something else.
        float averageDelay() const noexcept { return 2.0f + 0.5f * window; }

        static float centsToRatio (float cents) noexcept { return std::exp2 (cents / 1200.0f); }

    private:
        DelayLine line;
        float sampleRate = 48000.0f, window = 2400.0f, phase = 0.0f;
    };
}
