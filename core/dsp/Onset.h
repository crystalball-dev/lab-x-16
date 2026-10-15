#pragma once

#include "Common.h"

namespace lab::dsp
{
    // Dual-envelope onset detector (the envelopes of LAB X-0's transient shaper): a fast follower (0.5 ms attack, 20 ms
    // release) and a slow one (15 ms / 150 ms). An onset is the sample where the fast envelope first rises MARGIN dB
    // above the slow one while above a floor; then it is quiet for the minimum gap, and it fires again only after that
    // condition has once been false (a lone click's fast envelope still leads when the gap ends, and without the re-arm
    // the click's own decay fired a second onset). SENSITIVITY 0..1 maps the margin from 12 to 3 dB and the floor from
    // -50 to -70 dBFS. Used by APPARITION, GAUSS, SARCOPHAGUS, POLTERGEIST, ZEALOTS (the predator) and PSY-DOG (the
    // attack measure).
    class OnsetDetector
    {
    public:
        void prepare (float sr) noexcept
        {
            sampleRate = sr;
            fastAttack = onePoleCoef (0.0005f, sr);
            fastRelease = onePoleCoef (0.02f, sr);
            slowAttack = onePoleCoef (0.015f, sr);
            slowRelease = onePoleCoef (0.15f, sr);
            setSensitivity (0.5f);
            reset();
        }

        void reset() noexcept
        {
            fast = slow = 0.0f;
            quiet = 0;
            onsetPeak = 0.0f;
            armed = true;
        }

        void setSensitivity (float s, float minGapSeconds = 0.05f) noexcept
        {
            s = std::clamp (s, 0.0f, 1.0f);
            margin = dbToGain (12.0f - 9.0f * s);
            floor = dbToGain (-50.0f - 20.0f * s);
            gap = std::max (1, (int) (minGapSeconds * sampleRate));
        }

        // One stereo sample; true on the onset sample.
        bool process (float l, float r) noexcept
        {
            const float x = std::max (std::abs (l), std::abs (r));
            fast += (x - fast) * (x > fast ? fastAttack : fastRelease);
            slow += (x - slow) * (x > slow ? slowAttack : slowRelease);
            const bool attack = fast > floor && fast > slow * margin;
            if (! attack)
                armed = true;
            if (quiet > 0)
            {
                --quiet;
                onsetPeak = std::max (onsetPeak, x);
                return false;
            }
            if (attack && armed)
            {
                quiet = gap;
                onsetPeak = x;
                armed = false;
                return true;
            }
            return false;
        }

        // The ratio of the fast to the slow envelope (1 = steady; above 2 = an attack), for ducking (PSY-DOG).
        float attackRatio() const noexcept { return fast / std::max (slow, 1.0e-6f); }
        float fastEnvelope() const noexcept { return fast; }
        float slowEnvelope() const noexcept { return slow; }
        // The peak of the latest onset so far (it keeps rising for the gap after the onset).
        float peak() const noexcept { return onsetPeak; }

    private:
        float sampleRate = 48000.0f, fast = 0.0f, slow = 0.0f, onsetPeak = 0.0f;
        float fastAttack = 0.04f, fastRelease = 0.001f, slowAttack = 0.001f, slowRelease = 0.0001f;
        float margin = 2.0f, floor = 0.001f;
        int gap = 2400, quiet = 0;
        bool armed = true;
    };
}
