#pragma once

#include "Common.h"
#include "Transport.h"

namespace lab::dsp
{
    // Beats (quarter notes) per cycle for the suite's RATE list (LAB X-19 lfo_sync): index 0 is Free.
    // Free|1/1|1/2|1/4|1/8|1/16|1/32|1/4 T|1/8 T|1/16 T|1/4 D|1/8 D|2 bars|4 bars
    inline constexpr float rateBeats[] = { 0.0f, 4.0f, 2.0f, 1.0f, 0.5f, 0.25f, 0.125f,
                                           2.0f / 3.0f, 1.0f / 3.0f, 1.0f / 6.0f, 1.5f, 0.75f, 8.0f, 16.0f };
    inline constexpr const char* rateChoices = "Free|1/1|1/2|1/4|1/8|1/16|1/32|1/4 T|1/8 T|1/16 T|1/4 D|1/8 D|2 bars|4 bars";
    // The same list without Free (for parameters that are always synced).
    inline constexpr const char* rateChoicesSynced = "1/1|1/2|1/4|1/8|1/16|1/32|1/4 T|1/8 T|1/16 T|1/4 D|1/8 D|2 bars|4 bars";
    inline float syncedRateBeats (int choiceWithoutFree) noexcept
    {
        return rateBeats[(size_t) std::clamp (choiceWithoutFree + 1, 1, (int) std::size (rateBeats) - 1)];
    }

    // Beats per repeat for the suite's TIME list (LAB X-19 delay_sync, plus 3/16, 1/32 and 1/1 appended).
    // Free|1/4|1/8|1/8 D|1/8 T|1/16|1/4 D|1/2|3/16|1/32|1/1
    inline constexpr float timeBeats[] = { 0.0f, 1.0f, 0.5f, 0.75f, 1.0f / 3.0f, 0.25f, 1.5f, 2.0f, 0.75f, 0.125f, 4.0f };
    inline constexpr const char* timeChoices = "Free|1/4|1/8|1/8 D|1/8 T|1/16|1/4 D|1/2|3/16|1/32|1/1";

    // A global LFO (LAB X-10): free-running in Hz, or locked to the host's beat position when it plays.
    class Lfo
    {
    public:
        enum Shape { sine = 0, triangle, saw, square, sampleHold };

        void prepare (float sr, uint32_t seed) noexcept
        {
            sampleRate = sr;
            rng.seed (seed * 747796405u + 2891336453u);
            reset();
        }

        void reset() noexcept
        {
            phase = 0.0;
            lastCycle = -1;
            held = rng.bi();
            value = 0.0f;
        }

        // Advances by n samples; returns the bipolar value for this block. syncBeats 0 = free at rateHz.
        float advance (int n, int shape, float rateHz, float syncBeats, const Transport& t) noexcept
        {
            const bool synced = syncBeats > 0.0f;
            const double cyclesPerSecond = synced ? (t.bpm / 60.0) / (double) syncBeats : (double) rateHz;
            bool wrapped = false;
            if (synced && t.playing)
            {
                const double cycles = t.ppq / (double) syncBeats;
                const auto index = (long long) std::floor (cycles);
                wrapped = index != lastCycle;
                lastCycle = index;
                phase = cycles - std::floor (cycles);
            }
            else
            {
                phase += cyclesPerSecond * (double) n / (double) sampleRate;
                if (phase >= 1.0)
                {
                    phase -= std::floor (phase);
                    wrapped = true;
                }
                lastCycle = -1;
            }
            if (wrapped)
                held = rng.bi();
            const float p = (float) phase;
            switch (shape)
            {
                case sine:     value = sin2pi (p); break;
                case triangle: value = p < 0.25f ? 4.0f * p : (p < 0.75f ? 2.0f - 4.0f * p : 4.0f * p - 4.0f); break;
                case saw:      value = 1.0f - 2.0f * p; break;
                case square:   value = p < 0.5f ? 1.0f : -1.0f; break;
                default:       value = held; break;
            }
            return value;
        }

        float getValue() const noexcept { return value; }
        double getPhase() const noexcept { return phase; }

    private:
        Rng rng;
        float sampleRate = 48000.0f, value = 0.0f, held = 0.0f;
        double phase = 0.0;
        long long lastCycle = -1;
    };

    // COOLDOWN (LAB X-10): the emitter's duty cycle, a beat-locked gain curve in [1 - depth, 1].
    class Cooldown
    {
    public:
        enum Shape { pump = 0, gate, saw, sine };

        void prepare (float sr) noexcept
        {
            sampleRate = sr;
            smoothCoef = onePoleCoef (0.001f, sr);
            reset();
        }

        void reset() noexcept
        {
            phase = 0.0;
            gain = 1.0f;
        }

        // Fills gains[0..n). beats: cycle length in quarter notes, 0 = off (gain glides to 1).
        void process (float* gains, int n, float beats, int shape, float depth, float hold, const Transport& t) noexcept
        {
            if (beats <= 0.0f || depth <= 1.0e-4f)
            {
                for (int i = 0; i < n; ++i)
                {
                    gain += (1.0f - gain) * smoothCoef;
                    gains[i] = gain;
                }
                return;
            }
            const double inc = (t.bpm / 60.0) / (double) beats / (double) sampleRate;
            if (t.playing)
            {
                const double cycles = t.ppq / (double) beats;
                phase = cycles - std::floor (cycles);
            }
            depth = std::clamp (depth, 0.0f, 1.0f);
            const float tau = 0.05f + 0.6f * std::clamp (hold, 0.0f, 1.0f);
            const float norm = 1.0f / (1.0f - std::exp (-1.0f / tau));
            const float duty = 0.15f + 0.8f * std::clamp (hold, 0.0f, 1.0f);
            for (int i = 0; i < n; ++i)
            {
                const float p = (float) phase;
                float target;
                switch (shape)
                {
                    case pump: target = 1.0f - depth * (1.0f - (1.0f - std::exp (-p / tau)) * norm); break;
                    case gate: target = p < duty ? 1.0f : 1.0f - depth; break;
                    case saw:  target = 1.0f - depth * (1.0f - p); break;
                    default:   target = 1.0f - depth * 0.5f * (1.0f + sin2pi (p + 0.25f)); break;
                }
                gain += (target - gain) * smoothCoef;
                gains[i] = gain;
                phase += inc;
                if (phase >= 1.0)
                    phase -= 1.0;
            }
        }

        double getPhase() const noexcept { return phase; }

    private:
        float sampleRate = 48000.0f, smoothCoef = 0.02f, gain = 1.0f;
        double phase = 0.0;
    };
}
