#pragma once

#include <array>
#include "Filters.h"

namespace lab::dsp
{
    // TRANSIENT (LAB X-0): the gap between a fast and a slow envelope marks attacks (fast above slow) and tails (slow
    // above fast). ATTACK and SUSTAIN turn each gap into gain, up to +-18 dB.
    class Transient
    {
    public:
        void prepare (float sr) noexcept
        {
            fastAttack = onePoleCoef (0.0005f, sr);
            fastRelease = onePoleCoef (0.02f, sr);
            slowAttack = onePoleCoef (0.015f, sr);
            slowRelease = onePoleCoef (0.15f, sr);
            gainCoef = onePoleCoef (0.0003f, sr);
            reset();
        }

        void reset() noexcept { fast = slow = 0.0f; gainDb = 0.0f; }

        void process (float* l, float* r, int n, float attack, float sustain) noexcept
        {
            const bool active = std::abs (attack) > 0.005f || std::abs (sustain) > 0.005f;
            for (int i = 0; i < n; ++i)
            {
                const float x = std::max (std::abs (l[i]), std::abs (r[i]));
                fast += (x - fast) * (x > fast ? fastAttack : fastRelease);
                slow += (x - slow) * (x > slow ? slowAttack : slowRelease);
                if (! active && std::abs (gainDb) < 1.0e-4f)
                    continue;
                const float gap = gainToDb (fast + 1.0e-6f) - gainToDb (slow + 1.0e-6f);
                float target = 0.75f * (attack * std::clamp (gap, 0.0f, 24.0f) + sustain * std::clamp (-gap, 0.0f, 24.0f));
                if (slow < 0.001f)
                    target *= slow * 1000.0f;   // nothing to shape below -60 dBFS
                target = active ? std::clamp (target, -18.0f, 18.0f) : 0.0f;
                gainDb += (target - gainDb) * gainCoef;
                const float g = dbToGain (gainDb);
                l[i] *= g;
                r[i] *= g;
            }
        }

    private:
        float fast = 0.0f, slow = 0.0f, gainDb = 0.0f;
        float fastAttack = 0.5f, fastRelease = 0.01f, slowAttack = 0.01f, slowRelease = 0.001f, gainCoef = 0.1f;
    };

    // A stereo-linked feed-forward compressor without look-ahead (the REGULATOR's compressor, LAB X-10): peak detector,
    // soft knee, half the static reduction made up. `amount` 0..1 sets threshold (0 .. -30 dB) and ratio (1 .. 8).
    class Compressor
    {
    public:
        void prepare (float sr) noexcept
        {
            attackCoef = onePoleCoef (0.005f, sr);
            releaseCoef = onePoleCoef (0.08f, sr);
            detectorRelease = std::exp (-1.0f / (0.08f * sr));
            reset();
        }

        void reset() noexcept { env = 0.0f; gainDb = 0.0f; }

        float process (float& l, float& r, float amount) noexcept
        {
            amount = std::clamp (amount, 0.0f, 1.0f);
            const float x = std::max (std::abs (l), std::abs (r));
            env = x > env ? x : env * detectorRelease;
            float targetDb = 0.0f;
            if (amount > 0.001f)
            {
                const float threshold = -30.0f * amount, ratio = 1.0f + 7.0f * amount;
                const float slope = 1.0f / ratio - 1.0f;
                constexpr float knee = 6.0f;
                const float over = gainToDb (env + 1.0e-9f) - threshold;
                float reduction = 0.0f;
                if (over >= 0.5f * knee)
                    reduction = slope * over;
                else if (over > -0.5f * knee)
                    reduction = slope * (over + 0.5f * knee) * (over + 0.5f * knee) / (2.0f * knee);
                targetDb = reduction - threshold * (1.0f - 1.0f / ratio) * 0.5f;
            }
            gainDb += (targetDb - gainDb) * (targetDb < gainDb ? attackCoef : releaseCoef);
            const float g = dbToGain (gainDb);
            l *= g;
            r *= g;
            return gainDb;
        }

    private:
        float env = 0.0f, gainDb = 0.0f, attackCoef = 0.004f, releaseCoef = 0.0003f, detectorRelease = 0.9997f;
    };

    // GRIT (LAB X-19): only the band above CORNER is driven, and the result is added in parallel, so the low end stays
    // exactly as it was. Modes tube, tape, radio, crush.
    class ParallelGrit
    {
    public:
        enum Mode { tube = 0, tape, radio, crush };

        void prepare (float sr) noexcept
        {
            sampleRate = sr;
            reset();
        }

        void reset() noexcept
        {
            for (auto& c : ch)
                c = {};
            lastCorner = -1.0f;
            lastMode = -1;
        }

        void process (float* left, float* right, int n, int mode, float amount, float driveDb, float cornerHz) noexcept
        {
            amount = std::clamp (amount, 0.0f, 1.0f);
            if (amount < 1.0e-4f)
            {
                lastMode = -1;
                return;
            }
            if (mode != lastMode)
            {
                for (auto& c : ch)
                    c = {};
                lastMode = mode;
                lastCorner = -1.0f;
            }
            const float corner = std::max (cornerHz, mode == radio ? 400.0f : 60.0f);
            if (std::abs (corner - lastCorner) > 0.5f)
            {
                lastCorner = corner;
                const float g = SVF::gFor (corner, sampleRate);
                for (auto& c : ch)
                {
                    c.hp.set (g, 1.4142f);
                    c.hp2.set (g, 1.4142f);
                }
            }
            const float g = dbToGain (std::clamp (driveDb, 0.0f, 48.0f));
            const float comp = 0.5f / fastTanh (0.5f * std::max (g, 1.0f));
            const float postHz = mode == tape ? 6500.0f : (mode == radio ? 3000.0f : 9000.0f);
            const float postCoef = onePoleCoefHz (std::min (postHz, sampleRate * 0.45f), sampleRate);
            const float crushAmount = std::clamp (driveDb / 36.0f, 0.0f, 1.0f);
            const int holdSamples = 1 + (int) (crushAmount * 11.0f);
            const float levels = std::exp2 (12.0f - 8.0f * crushAmount);
            float* io[2] = { left, right };
            for (int c = 0; c < 2; ++c)
            {
                auto& s = ch[(size_t) c];
                float* x = io[c];
                for (int i = 0; i < n; ++i)
                {
                    const float hp = s.hp.highpass (x[i]);
                    float wet;
                    const float d = hp * g;
                    switch (mode)
                    {
                        case tube:  wet = (fastTanh (d + 0.25f) - fastTanh (0.25f)) * comp; break;
                        case tape:  wet = fastTanh (d) * comp; break;
                        case radio: wet = std::clamp (d, -1.0f, 1.0f) * comp; break;
                        default:
                            if (--s.holdCount <= 0)
                            {
                                s.held = std::round (hp * levels) / levels;
                                s.holdCount = holdSamples;
                            }
                            wet = s.held;
                            break;
                    }
                    const float hp2 = s.hp2.highpass (wet);
                    s.post1 += (hp2 - s.post1) * postCoef;
                    s.post2 += (s.post1 - s.post2) * postCoef;
                    x[i] += s.post2 * amount;
                }
            }
        }

    private:
        struct Channel
        {
            SVF hp, hp2;
            float post1 = 0.0f, post2 = 0.0f, held = 0.0f;
            int holdCount = 0;
        };

        std::array<Channel, 2> ch {};
        float sampleRate = 48000.0f, lastCorner = -1.0f;
        int lastMode = -1;
    };
}
