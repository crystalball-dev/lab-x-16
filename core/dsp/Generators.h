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

    // PROGRAMMER (LAB X-3): a slow, irregular random walk in [-1, 1]. It hops to new targets at about `rate` Hz and
    // glides toward them through two one-poles, so its slope never jumps (a pitch drift made from it never steps).
    class Programmer
    {
    public:
        void seed (uint32_t s) noexcept { rng.seed (s); }

        void reset (float sr) noexcept
        {
            sampleRate = sr;
            value = mid = 0.0f;
            target = rng.bi() * 0.5f;
            countdown = 0.0f;
        }

        // Advances by n samples and returns the value in [-1, 1].
        float advance (int n, float rateHz) noexcept
        {
            rateHz = std::max (rateHz, 0.005f);
            countdown -= (float) n;
            if (countdown <= 0.0f)
            {
                target = rng.bi() * (0.55f + 0.45f * rng.uni());
                countdown = sampleRate / rateHz * (0.5f + rng.uni());
            }
            const float coef = 1.0f - std::exp (-(float) n * rateHz * 4.0f / sampleRate);
            mid += (target - mid) * coef;
            value += (mid - value) * coef;
            return value;
        }

    private:
        Rng rng;
        float sampleRate = 48000.0f, value = 0.0f, mid = 0.0f, target = 0.0f, countdown = 0.0f;
    };

    // WHISPER (LAB X-3): white noise through three band-passes (Q 12) at 1:2:3 of a formant base, like the psy voices.
    class Whisper
    {
    public:
        void prepare (float sr, uint32_t seed) noexcept
        {
            sampleRate = sr;
            rng.seed (seed);
            setFormant (127.0f);
            reset();
        }

        void reset() noexcept { for (auto& b : bands) b.reset(); }

        void setFormant (float f0) noexcept
        {
            for (int b = 0; b < 3; ++b)
                bands[(size_t) b].setHz (std::min (f0 * (float) (b + 1), 0.45f * sampleRate), 12.0f, sampleRate);
        }

        // One sample of whisper at about the level of the noise that feeds it (X-3's band-passes peaked at Q).
        float next() noexcept
        {
            const float w = rng.bi();
            return (bands[0].bandpass (w) + 0.6f * bands[1].bandpass (w) + 0.35f * bands[2].bandpass (w)) * 10.8f;
        }

    private:
        Rng rng;
        std::array<SVF, 3> bands {};
        float sampleRate = 48000.0f;
    };

    // PRESENCE (LAB X-3): a sine and its octave at 35 %, with 5.3 Hz vibrato: the controller's whine.
    class Presence
    {
    public:
        void prepare (float sr) noexcept { sampleRate = sr; reset(); }
        void reset() noexcept { p1 = p2 = vib = 0.0f; }

        float next (float hz) noexcept
        {
            vib = wrap01 (vib + 5.3f / sampleRate);
            const float f = hz * (1.0f + 0.004f * sin2pi (vib));
            p1 = wrap01 (p1 + std::min (f / sampleRate, 0.45f));
            p2 = wrap01 (p2 + std::min (2.0f * f / sampleRate, 0.45f));
            return (sin2pi (p1) + 0.35f * sin2pi (p2)) * 0.74f;
        }

    private:
        float sampleRate = 48000.0f, p1 = 0.0f, p2 = 0.0f, vib = 0.0f;
    };

    // SIREN: the blowout siren's triad (350, 705, 1055 Hz, LAB X-3's measurement) with a 0.7 Hz tremolo, over noise
    // through a band-pass that sweeps from 800 Hz to 3 kHz as `sweep` goes 0 .. 1.
    class Siren
    {
    public:
        void prepare (float sr, uint32_t seed) noexcept
        {
            sampleRate = sr;
            rng.seed (seed);
            reset();
        }

        void reset() noexcept
        {
            phases.fill (0.0f);
            trem = 0.0f;
            band.reset();
        }

        float next (float sweep) noexcept
        {
            static constexpr float hz[3] = { 350.0f, 705.0f, 1055.0f };
            float tone = 0.0f;
            for (int k = 0; k < 3; ++k)
            {
                phases[(size_t) k] = wrap01 (phases[(size_t) k] + hz[k] / sampleRate);
                tone += sin2pi (phases[(size_t) k]) * (k == 0 ? 0.5f : 0.3f);
            }
            trem = wrap01 (trem + 0.7f / sampleRate);
            band.setHz (800.0f * std::exp2 (std::clamp (sweep, 0.0f, 1.0f) * 1.907f), 3.0f, sampleRate);
            const float noise = band.bandpass (rng.bi()) * 1.2f;
            return (tone * (0.75f + 0.25f * sin2pi (trem)) + noise) * 0.6f;
        }

    private:
        Rng rng;
        std::array<float, 3> phases {};
        float sampleRate = 48000.0f, trem = 0.0f;
        SVF band;
    };
}
