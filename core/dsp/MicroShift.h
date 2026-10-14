#pragma once

#include "DelayLine.h"

namespace lab::dsp
{
    // MICROSHIFT (LAB X-19's shimmer heads, standalone): two read heads sliding through a short buffer, crossfaded
    // with sin^2 windows, shift the pitch by any ratio. Window 20-80 ms. Ratio > 1 shifts up, < 1 down.
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
            const float slope = ratio - 1.0f;   // how fast the read head runs ahead of (or behind) the write head
            if (std::abs (slope) < 1.0e-5f)
                return line.readLinear (0, 2.0f);
            phase = wrap01 (phase + std::abs (slope) / window);
            const float p2 = wrap01 (phase + 0.5f);
            // Upward: the delay shrinks across the window; downward: it grows.
            const float d1 = 2.0f + window * (slope > 0.0f ? 1.0f - phase : phase);
            const float d2 = 2.0f + window * (slope > 0.0f ? 1.0f - p2 : p2);
            const float w1 = sin2pi (0.5f * phase), w2 = sin2pi (0.5f * p2);
            return line.readLinear (0, d1) * w1 * w1 + line.readLinear (0, d2) * w2 * w2;
        }

        static float centsToRatio (float cents) noexcept { return std::exp2 (cents / 1200.0f); }

    private:
        DelayLine line;
        float sampleRate = 48000.0f, window = 2400.0f, phase = 0.0f;
    };
}
