#pragma once

#include <array>
#include "DelayLine.h"
#include "Filters.h"

namespace lab::dsp
{
    // RESONATOR (LAB X-10): a feedback comb tuned to a pitch, damped at 5 kHz and soft-limited so full feedback rings
    // without running away, followed by three vowel band-passes (male formants, U-O-A-E-I). Mono. The delay is read
    // with the integer whole/fraction split (X-10 0.1.2's fix), and glides across a block to its new tuning.
    class Resonator
    {
    public:
        void prepare (float sr, float lowestHz = 15.0f)
        {
            sampleRate = sr;
            line.prepare ((int) std::ceil (sr / std::max (5.0f, lowestHz)) + 8);
            dampCoef = onePoleCoefHz (5000.0f, sr);
            reset();
        }

        void reset() noexcept
        {
            line.reset();
            damp = 0.0f;
            delay = -1.0f;
            for (auto& f : formants)
                f.reset();
        }

        // Once per block: the comb's pitch, its feedback 0..1 and the vowel 0..1 (U .. I).
        void set (float freqHz, float feedback, float vowel, int n) noexcept
        {
            float target = sampleRate / std::max (freqHz, 5.0f);
            if (! std::isfinite (target))   // a NaN tuning must never reach the integer split
                target = 2.0f;
            target = std::clamp (target, 2.0f, (float) line.capacity() - 2.0f);
            if (delay < 0.0f)
                delay = target;
            dDelay = (target - delay) / (float) std::max (1, n);
            fb = std::clamp (feedback, 0.0f, 1.0f) * 0.97f;
            outGain = 0.9f * (1.0f - 0.6f * fb) * 2.5f;

            static constexpr float table[5][3] = {
                { 300.0f, 870.0f, 2240.0f }, { 570.0f, 840.0f, 2410.0f }, { 730.0f, 1090.0f, 2440.0f },
                { 530.0f, 1840.0f, 2480.0f }, { 270.0f, 2290.0f, 3010.0f } };
            const float x = std::clamp (vowel, 0.0f, 1.0f) * 4.0f;
            const int v0 = std::min ((int) x, 3);
            const float vf = x - (float) v0;
            for (int b = 0; b < 3; ++b)
                formants[(size_t) b].setHz (table[v0][b] + (table[v0 + 1][b] - table[v0][b]) * vf, 7.0f, sampleRate);
        }

        // One sample in, the resonator's output (scale it by a level).
        float process (float in) noexcept
        {
            delay += dDelay;
            const float delayed = line.readLinear (0, delay - 1.0f);   // `delay` samples ago
            damp += (delayed - damp) * dampCoef;
            const float y = 2.0f * fastTanh (0.5f * (in + fb * damp));
            line.push (y);
            const float v = formants[0].bandpass (y) + 0.6f * formants[1].bandpass (y) + 0.35f * formants[2].bandpass (y);
            return v * outGain;
        }

    private:
        DelayLine line;
        std::array<SVF, 3> formants {};
        float sampleRate = 48000.0f, dampCoef = 0.5f, damp = 0.0f, delay = -1.0f, dDelay = 0.0f, fb = 0.0f, outGain = 1.0f;
    };

    // A bare feedback comb (CARRIER's chord bank, C-CONSCIOUSNESS's harmony): no vowels, a one-pole damping in the loop
    // and a soft limit. `gain` is the loop gain (below 1).
    class CombFilter
    {
    public:
        void prepare (float sr, float lowestHz = 20.0f)
        {
            sampleRate = sr;
            line.prepare ((int) std::ceil (sr / std::max (5.0f, lowestHz)) + 8);
            setDamping (5000.0f);
            reset();
        }

        void reset() noexcept { line.reset(); damp = 0.0f; delay = -1.0f; }
        void setDamping (float hz) noexcept { dampCoef = onePoleCoefHz (std::min (hz, 0.45f * sampleRate), sampleRate); }

        // Once per block. glideCoef 0: the delay moves to the new pitch linearly across the block; otherwise it follows
        // it with that one-pole coefficient per sample (C-CONSCIOUSNESS glides over 200 ms).
        void set (float freqHz, float loopGain, int n, float glideCoef = 0.0f) noexcept
        {
            target = sampleRate / std::max (freqHz, 5.0f);
            if (! std::isfinite (target))
                target = 2.0f;
            target = std::clamp (target, 2.0f, (float) line.capacity() - 2.0f);
            if (delay < 0.0f)
                delay = target;
            glide = glideCoef;
            dDelay = glideCoef > 0.0f ? 0.0f : (target - delay) / (float) std::max (1, n);
            gain = std::clamp (loopGain, 0.0f, 0.995f);
        }

        float process (float in) noexcept
        {
            if (glide > 0.0f)
                delay += (target - delay) * glide;
            else
                delay += dDelay;
            const float delayed = line.readLinear (0, delay - 1.0f);
            damp += (delayed - damp) * dampCoef;
            const float y = in + gain * damp;
            line.push (1.6f * fastTanh (y * 0.625f));   // a soft limit well above the normal level
            return delayed;
        }

    private:
        DelayLine line;
        float sampleRate = 48000.0f, dampCoef = 0.5f, damp = 0.0f, delay = -1.0f, dDelay = 0.0f, gain = 0.5f;
        float target = 2.0f, glide = 0.0f;
    };
}
