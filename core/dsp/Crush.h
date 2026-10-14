#pragma once

#include "Common.h"

namespace lab::dsp
{
    // One sample-hold + bit-reduction primitive for the suite (LAB X-3 SCRUB, LAB X-19 GRIT crush, LAB X-10 EMITTER
    // crush). `bits` 1..16 (16 = off), `hold` 1..64 samples (1 = off). `bias` trades the two: positive gives rate
    // reduction priority, negative bit reduction (EMITTER's crush bias).
    class Crush
    {
    public:
        void reset() noexcept
        {
            phase = 1.0f;
            held[0] = held[1] = 0.0f;
        }

        // Quantise to `bits` around zero: steps of 1 / 2^(bits - 1) (bits 4 = steps of 1/8 full scale).
        static float quantise (float x, float bits) noexcept
        {
            if (bits >= 15.95f)
                return x;
            const float q = std::exp2 (std::clamp (bits, 1.0f, 16.0f) - 1.0f);
            return std::round (x * q) / q;
        }

        void process (float* l, float* r, int n, float bits, float hold, float bias = 0.0f) noexcept
        {
            bias = std::clamp (bias, -1.0f, 1.0f);
            const float effBits = std::clamp (bits - std::max (0.0f, -bias) * 8.0f, 1.0f, 16.0f);
            const float effHold = std::clamp (hold * (1.0f + std::max (0.0f, bias) * 7.0f), 1.0f, 512.0f);
            const bool crush = effBits < 15.95f, sampleHold = effHold > 1.001f;
            if (! crush && ! sampleHold)
                return;
            const float step = 1.0f / effHold;
            for (int i = 0; i < n; ++i)
            {
                float a = l[i], b = r[i];
                if (sampleHold)
                {
                    phase += step;
                    if (phase >= 1.0f)
                    {
                        phase -= std::floor (phase);
                        held[0] = a;
                        held[1] = b;
                    }
                    a = held[0];
                    b = held[1];
                }
                if (crush)
                {
                    a = quantise (a, effBits);
                    b = quantise (b, effBits);
                }
                l[i] = a;
                r[i] = b;
            }
        }

    private:
        float phase = 1.0f;
        float held[2] {};
    };
}
