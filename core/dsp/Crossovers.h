#pragma once

#include <array>
#include "Filters.h"

namespace lab::dsp
{
    // Linkwitz-Riley 4th order on the SVF (LAB X-0): two 2nd-order Butterworths per band; the bands sum to an all-pass.
    // Replaces juce::dsp::LinkwitzRileyFilter, so the only juce_dsp dependency left in the suite is Oversampling.
    struct LR4
    {
        SVF first, low2, high2;

        void reset() noexcept { first.reset(); low2.reset(); high2.reset(); }

        void set (float hz, float sampleRate) noexcept
        {
            const float g = SVF::gFor (hz, sampleRate);
            first.set (g, 1.41421356f);
            low2.set (g, 1.41421356f);
            high2.set (g, 1.41421356f);
        }

        void process (float x, float& low, float& high) noexcept
        {
            float lp, bp, hp;
            first.process (x, lp, bp, hp);
            float lp2, bp2, hp2;
            low2.process (lp, lp2, bp2, hp2);
            low = lp2;
            float lp3, bp3, hp3;
            high2.process (hp, lp3, bp3, hp3);
            high = hp3;
        }
    };

    // Linkwitz-Riley 8th order (LAB X-0 SUB SAFE): 48 dB/oct per band, the bands still sum to an all-pass.
    struct LR8
    {
        SVF first;
        std::array<SVF, 3> low, high;

        void reset() noexcept
        {
            first.reset();
            for (auto& f : low)  f.reset();
            for (auto& f : high) f.reset();
        }

        void set (float hz, float sampleRate) noexcept
        {
            const float g = SVF::gFor (hz, sampleRate);
            constexpr float k1 = 1.84775907f, k2 = 0.76536686f;   // Butterworth 4: Q 0.541 and 1.307
            first.set (g, k1);
            low[0].set (g, k2);  low[1].set (g, k1);  low[2].set (g, k2);
            high[0].set (g, k2); high[1].set (g, k1); high[2].set (g, k2);
        }

        void process (float x, float& lowOut, float& highOut) noexcept
        {
            float lp, bp, hp, a, b, c;
            first.process (x, lp, bp, hp);
            for (auto& f : low)
            {
                f.process (lp, a, b, c);
                lp = a;
            }
            for (auto& f : high)
            {
                f.process (hp, a, b, c);
                hp = c;
            }
            lowOut = lp;
            highOut = hp;
        }
    };
}
