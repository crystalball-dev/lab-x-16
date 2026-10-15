#pragma once

#include <array>
#include "Filters.h"

namespace lab::dsp
{
    // FORMANT BANK (LAB X-19's CHOIR formants, split from the singers): four TPT band-passes per channel gliding
    // between the alto vowels U, O, A, E and I (Csound's formant table, bandwidths x 1.6), interpolated in log
    // frequency; SIZE moves every formant from a small voice (-1, up 0.4 octave) to a large one (+1, down). X-19 lifted
    // each formant to undo a saw's slope; an effect filters any input, so here each vowel is normalised so that pink
    // noise passes at the level it came in (pink has equal energy per octave, so a band-pass of bandwidth B at fc keeps
    // about (pi / 2) (B / fc) / ln 2 octaves of it).
    class FormantBank
    {
    public:
        static constexpr int numFormants = 4;

        void prepare (float sr) noexcept { sampleRate = sr; reset(); }
        void reset() noexcept
        {
            for (auto& f : bands)
                f.reset();
        }

        // Once per block: vowel 0..1 (U, O, A, E, I) and size -1..1.
        void set (float vowel, float size) noexcept
        {
            const float shift = std::exp2 (-0.4f * std::clamp (size, -1.0f, 1.0f));
            const float v = std::clamp (vowel, 0.0f, 1.0f) * 4.0f;
            const int a = std::min ((int) v, 3);
            const float t = v - (float) a;
            float octaves = 0.0f;
            for (int f = 0; f < numFormants; ++f)
            {
                const auto& va = vowels[a];
                const auto& vb = vowels[a + 1];
                const float freq = std::exp (std::log (va.freq[f]) + t * (std::log (vb.freq[f]) - std::log (va.freq[f]))) * shift;
                const float bw = (va.bw[f] + t * (vb.bw[f] - va.bw[f])) * 1.6f * shift;
                const float level = dbToGain (va.db[f] + t * (vb.db[f] - va.db[f]));
                const float fc = std::min (freq, sampleRate * 0.45f);
                for (int ch = 0; ch < 2; ++ch)
                    bands[(size_t) (ch * numFormants + f)].setHz (fc, fc / std::max (10.0f, bw), sampleRate);
                gains[(size_t) f] = level;
                octaves += level * level * 1.5708f * (bw / fc) / 0.6931f;
            }
            // Pink noise from 20 Hz to 20 kHz spans ten octaves; the vowel passes `octaves` of them.
            norm = std::sqrt (10.0f / std::max (octaves, 1.0e-4f));
        }

        float process (int channel, float x) noexcept
        {
            float y = 0.0f;
            for (int f = 0; f < numFormants; ++f)
                y += bands[(size_t) (channel * numFormants + f)].bandpass (x) * gains[(size_t) f];
            return y * norm;
        }

    private:
        struct Vowel { float freq[numFormants], bw[numFormants], db[numFormants]; };

        // Ordered dark to bright: U, O, A, E, I (alto, from the Csound manual's formant table).
        static constexpr Vowel vowels[5] = {
            { { 325.0f, 700.0f, 2530.0f, 3500.0f }, { 50.0f, 60.0f, 170.0f, 180.0f }, { 0.0f, -12.0f, -30.0f, -40.0f } },
            { { 450.0f, 800.0f, 2830.0f, 3500.0f }, { 70.0f, 80.0f, 100.0f, 130.0f }, { 0.0f, -9.0f, -16.0f, -28.0f } },
            { { 800.0f, 1150.0f, 2800.0f, 3500.0f }, { 80.0f, 90.0f, 120.0f, 130.0f }, { 0.0f, -4.0f, -20.0f, -36.0f } },
            { { 400.0f, 1600.0f, 2700.0f, 3300.0f }, { 60.0f, 80.0f, 120.0f, 150.0f }, { 0.0f, -24.0f, -30.0f, -35.0f } },
            { { 350.0f, 1700.0f, 2700.0f, 3700.0f }, { 50.0f, 100.0f, 120.0f, 150.0f }, { 0.0f, -20.0f, -30.0f, -36.0f } },
        };

    public:
        // The vowel table's formant frequencies (CARRIER T1 checks the peaks against them).
        static float formantHz (int vowelIndex, int formant) noexcept { return vowels[std::clamp (vowelIndex, 0, 4)].freq[std::clamp (formant, 0, 3)]; }

    private:
        std::array<SVF, 2 * numFormants> bands {};
        std::array<float, numFormants> gains {};
        float sampleRate = 48000.0f, norm = 1.0f;
    };
}
