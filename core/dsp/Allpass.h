#pragma once

#include "DelayLine.h"

namespace lab::dsp
{
    // Schroeder all-pass with an integer delay: flat magnitude, smeared phase. Chains of them decorrelate a signal
    // (PSY-DOG's phantoms, APPARITION's PHANTOM).
    class Allpass
    {
    public:
        void prepare (int maxDelay) { line.prepare (std::max (2, maxDelay)); delay = std::min (delay, line.capacity()); }
        void reset() noexcept { line.reset(); }
        void set (int delaySamples, float gain) noexcept
        {
            delay = std::clamp (delaySamples, 1, line.capacity());
            g = std::clamp (gain, -0.95f, 0.95f);
        }

        float process (float x) noexcept
        {
            const float delayed = line.readInt (0, delay - 1);   // `delay` samples ago
            const float v = x + g * delayed;
            line.push (v);
            return delayed - g * v;
        }

    private:
        DelayLine line;
        int delay = 1;
        float g = 0.6f;
    };
}
