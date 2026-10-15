#pragma once

#include <array>
#include "Common.h"

namespace lab::dsp
{
    // A 90-degree phase splitter: two cascades of second-order all-pass sections in z^2, (a - z^-2) / (1 - a z^-2), the
    // second fed one sample late. The coefficients are Laurent de Soras' polyphase half-band design (12 coefficients,
    // transition 0.0003 of the sample rate) used at a quarter-sample-rate rotation: the outputs stay 90 degrees apart
    // within 0.15 degrees from 20 Hz to 20 kHz at 44.1, 48 and 96 kHz, which leaves a frequency shifter's image about
    // 58 dB down (checked in Python before the port, and by CARRIER T3). Path 0 leads path 1.
    class PhaseSplitter
    {
    public:
        static constexpr int numCoefs = 12;
        static constexpr double coefs[numCoefs] = {
            0.10382977217856484, 0.34086144816493602, 0.58155228210640886, 0.75835277090421194,
            0.86795307698494417, 0.93000819108501021, 0.96350784692043479, 0.98115876042321359,
            0.9903636046894434,  0.99517878187408637, 0.99778007879566466, 0.99935822241407546 };

        void reset() noexcept
        {
            for (auto& s : state)
                s = {};
            prev = 0.0f;
        }

        // x -> (in-phase, quadrature): the analytic signal is p0 + j p1.
        void process (float x, float& p0, float& p1) noexcept
        {
            float a = x, b = prev;
            prev = x;
            for (int k = 0; k < numCoefs; ++k)
            {
                auto& s = state[(size_t) k];
                const float c = (float) coefs[k];
                float& v = (k & 1) == 0 ? a : b;
                const float y = c * v - s.x2 + c * s.y2;   // y[n] = c x[n] - x[n-2] + c y[n-2]
                s.x2 = s.x1;
                s.x1 = v;
                s.y2 = s.y1;
                s.y1 = y;
                v = y;
            }
            p0 = a;
            p1 = b;
        }

    private:
        struct Section { float x1 = 0.0f, x2 = 0.0f, y1 = 0.0f, y2 = 0.0f; };
        std::array<Section, numCoefs> state {};
        float prev = 0.0f;
    };

    // Single-sideband frequency shifter (and ring modulator): the analytic signal times a carrier. UPPER moves every
    // partial up by `hz` (down for negative hz), LOWER the other way, BOTH is plain ring modulation (both sidebands).
    // The carrier phase is kept in double precision so long sessions do not drift.
    class FrequencyShifter
    {
    public:
        enum Sideband { both = 0, upper, lower };

        void prepare (double sampleRate) noexcept { sr = sampleRate; reset(); }
        void reset() noexcept { split.reset(); phase = 0.0; }
        void setPhase (double cycles) noexcept { phase = cycles - std::floor (cycles); }

        float process (float x, float hz, int sideband) noexcept
        {
            float p0, p1;
            split.process (x, p0, p1);
            const float c = sin2pi ((float) phase + 0.25f), s = sin2pi ((float) phase);
            phase += (double) hz / sr;
            phase -= std::floor (phase);
            switch (sideband)
            {
                case upper: return p0 * c - p1 * s;
                case lower: return p0 * c + p1 * s;
                default:    return p0 * c * 1.41421356f;   // two sidebands at the power of one
            }
        }

    private:
        PhaseSplitter split;
        double sr = 48000.0, phase = 0.0;
    };
}
