#pragma once

#include "Common.h"
#include "Filters.h"

namespace lab::dsp
{
    // GEIGER (LAB X-3): Poisson clicks. Each click is noise under a 0.35 ms decay through a band-pass at the tone.
    // `rate` is the mean clicks per second for this block; the caller derives it (DOSIMETER: dose and envelope).
    class Geiger
    {
    public:
        void prepare (float sr, uint32_t seed) noexcept
        {
            sampleRate = sr;
            rng.seed (seed);
            decay = std::exp (-1.0f / (0.00035f * sr));
            reset();
        }

        void reset() noexcept
        {
            env = 0.0f;
            bp.reset();
            clicks = 0;
        }

        void setTone (float hz) noexcept { bp.set (SVF::gFor (hz, sampleRate), 0.33f); }

        // One sample: returns the click signal; `rate` clicks per second.
        float next (float rate) noexcept
        {
            if (rate > 0.0f && rng.uni() < rate / sampleRate)
            {
                env = 0.5f + 0.5f * rng.uni();
                ++clicks;
            }
            if (env < 1.0e-5f)
                return bp.bandpass (0.0f) * 0.9f;
            const float y = bp.bandpass (rng.bi() * env) * 0.9f;
            env *= decay;
            return y;
        }

        int takeClicks() noexcept { const int c = clicks; clicks = 0; return c; }

    private:
        Rng rng;
        SVF bp;
        float sampleRate = 48000.0f, env = 0.0f, decay = 0.9f;
        int clicks = 0;
    };

    // DUST (LAB X-19 GRIT): crackle and hiss that follow the input's envelope; silent when the input is.
    class Dust
    {
    public:
        void prepare (float sr, uint32_t seed) noexcept
        {
            sampleRate = sr;
            rng.seed (seed);
            followRelease = std::exp (-1.0f / (0.08f * sr));
            clickCoef = std::exp (-1.0f / (0.0006f * sr));
            reset();
        }

        void reset() noexcept { env = click = hiss = 0.0f; }

        float next (float input, float amount) noexcept
        {
            const float a = std::abs (input);
            env = a > env ? a : env * followRelease;
            if (amount <= 1.0e-4f)
                return 0.0f;
            if (rng.uni() < amount * amount * 40.0f / sampleRate)
                click += rng.bi() * env * 0.8f;
            click *= clickCoef;
            hiss += (rng.bi() - hiss) * 0.35f;
            return click + hiss * env * amount * 0.04f;
        }

    private:
        Rng rng;
        float sampleRate = 48000.0f, env = 0.0f, click = 0.0f, hiss = 0.0f, followRelease = 0.999f, clickCoef = 0.9f;
    };
}
