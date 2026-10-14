#pragma once

#include <memory>
#include <juce_dsp/juce_dsp.h>
#include "Crossovers.h"
#include "DelayLine.h"

namespace lab::dsp
{
    // SPLIT-BAND DRIVE (LAB X-10 EMITTER): oversampled (off, 2x, 4x; polyphase IIR half-bands) and split by an LR4
    // crossover. Below the split the signal passes clean (summed to mono or kept stereo); above it one of five shapers
    // runs, pre-emphasised above 400 Hz with drive, faded in over the first 6 dB, and only the change it makes is kept,
    // DC-blocked and high-passed at the split, then tilted around 900 Hz. Latency is constant whatever the oversampling:
    // lower settings are padded to the 4x path.
    class SplitBandDrive
    {
    public:
        enum Mode { tube = 0, clip, fold, rectify, crush };

        struct Settings
        {
            int mode = tube;
            float driveDb = 12.0f;
            float bias = 0.0f;       // -1..1
            float tone = 0.0f;       // -1..1
            float splitHz = 120.0f;
            float highGain = 1.0f;   // COOLANT Level target
            bool subMono = true;
        };

        static constexpr int maxBlock = 16;
        static constexpr float referenceLevel = 0.7f;
        static constexpr float emphasisMax = 2.98f;   // +12 dB above 400 Hz

        void prepare (double newSampleRate)
        {
            sampleRate = newSampleRate;
            using OS = juce::dsp::Oversampling<float>;
            os2 = std::make_unique<OS> (4, 1, OS::filterHalfBandPolyphaseIIR, true, true);
            os4 = std::make_unique<OS> (4, 2, OS::filterHalfBandPolyphaseIIR, true, true);
            os2->initProcessing ((size_t) maxBlock);
            os4->initProcessing ((size_t) maxBlock);
            // Each path's latency is measured, not taken from the filter design: the half-bands' latency is fractional,
            // and padding by its rounded value put the 2x path's impulse one sample off the others.
            latency[0] = 0;
            latency[1] = measurePath (*os2);
            latency[2] = measurePath (*os4);
            maxLatency = std::max ({ latency[0], latency[1], latency[2] });
            work.setSize (4, maxBlock, false, true, false);
            pad.prepare (std::max (1, maxLatency));
            padLow.prepare (std::max (1, maxLatency));
            reset();
        }

        int getLatency() const noexcept { return maxLatency; }
        int getOversampling() const noexcept { return osIndex; }

        // Only while the effect is faded out (preset fade): the new path starts from silence.
        void setOversampling (int index) noexcept
        {
            if (std::clamp (index, 0, 2) != osIndex)
            {
                osIndex = std::clamp (index, 0, 2);
                reset();
            }
        }

        void reset() noexcept
        {
            for (auto& c : chan)
                c = {};
            if (os2 != nullptr) os2->reset();
            if (os4 != nullptr) os4->reset();
            pad.reset();
            padLow.reset();
            firstBlock = true;
        }

        // l, r in place (n <= maxBlock): writes the clean low band plus the processed high band, at the constant latency.
        void process (float* l, float* r, int n, const Settings& s) noexcept
        {
            jassert (n <= maxBlock);
            work.copyFrom (0, 0, l, n);
            work.copyFrom (1, 0, r, n);
            work.clear (2, 0, n);
            work.clear (3, 0, n);

            const float g = dbToGain (std::clamp (s.driveDb, 0.0f, 72.0f));
            Ramp target;
            target.drive = g;
            target.comp = referenceLevel / fastTanh (g * referenceLevel);
            target.bias = std::clamp (s.bias, -1.0f, 1.0f);
            target.tone = std::clamp (s.tone, -1.0f, 1.0f) * 0.85f;
            target.crush = std::clamp (s.driveDb / 40.0f, 0.0f, 1.0f);
            target.emphasis = emphasisMax * std::clamp (s.driveDb / 24.0f, 0.0f, 1.0f);
            target.amount = s.mode == crush ? 1.0f : std::clamp (s.driveDb / 6.0f, 0.0f, 1.0f);
            target.highGain = std::clamp (s.highGain, 0.0f, 2.0f);
            if (firstBlock)
            {
                current = target;
                firstBlock = false;
            }

            const int factor = 1 << osIndex;
            const float rate = (float) sampleRate * (float) factor;
            const float split = std::clamp (s.splitHz, 20.0f, 1000.0f);
            for (auto& c : chan)
            {
                c.xo[(size_t) osIndex].set (split, rate);
                c.product[(size_t) osIndex].set (split, rate);
            }

            juce::dsp::AudioBlock<float> block (work.getArrayOfWritePointers(), 4, (size_t) n);
            if (osIndex == 0)
                processBands (block, n, factor, s, target);
            else
            {
                auto& os = osIndex == 1 ? *os2 : *os4;
                auto up = os.processSamplesUp (block);
                processBands (up, (int) up.getNumSamples(), factor, s, target);
                os.processSamplesDown (block);
            }
            current = target;

            // Pad this path's latency up to the constant maximum, then add the low band back.
            const int padSamples = maxLatency - latency[osIndex];
            for (int i = 0; i < n; ++i)
            {
                float hl = work.getSample (0, i), hr = work.getSample (1, i);
                float ll = work.getSample (2, i), lr = work.getSample (3, i);
                if (maxLatency > 0)
                {
                    pad.push (hl, hr);
                    padLow.push (ll, lr);
                    hl = pad.readInt (0, padSamples);
                    hr = pad.readInt (1, padSamples);
                    ll = padLow.readInt (0, padSamples);
                    lr = padLow.readInt (1, padSamples);
                }
                if (s.subMono)
                    ll = lr = 0.5f * (ll + lr);
                l[i] = ll + hl;
                r[i] = lr + hr;
            }
        }

    private:
        // Where an impulse comes out of an up-then-down pass (the peak of its response).
        static int measurePath (juce::dsp::Oversampling<float>& os)
        {
            os.reset();
            juce::AudioBuffer<float> buf (4, maxBlock);
            int at = 0;
            float best = 0.0f;
            for (int block = 0; block < 16; ++block)
            {
                buf.clear();
                if (block == 0)
                    buf.setSample (0, 0, 1.0f);
                juce::dsp::AudioBlock<float> b (buf.getArrayOfWritePointers(), 4, (size_t) maxBlock);
                os.processSamplesUp (b);
                os.processSamplesDown (b);
                for (int i = 0; i < maxBlock; ++i)
                    if (std::abs (buf.getSample (0, i)) > best)
                    {
                        best = std::abs (buf.getSample (0, i));
                        at = block * maxBlock + i;
                    }
            }
            os.reset();
            return at;
        }

        struct Ramp
        {
            float drive = 1.0f, comp = 1.0f, bias = 0.0f, tone = 0.0f, crush = 0.0f, emphasis = 0.0f, amount = 0.0f, highGain = 1.0f;
        };

        struct ChannelState
        {
            std::array<LR4, 3> xo, product;
            float dcIn = 0.0f, dcOut = 0.0f, toneLp = 0.0f, emphasisLp = 0.0f, held = 0.0f, holdPhase = 1.0f;
        };

        static float shape (float x, int mode, float g, float bias, float comp) noexcept
        {
            switch (mode)
            {
                case tube:
                {
                    const float b = 0.6f * bias;
                    return (fastTanh (g * x + b) - fastTanh (b)) * comp;
                }
                case clip:
                {
                    const float b = 0.5f * bias;
                    return (std::clamp (g * x + b, -1.0f, 1.0f) - std::clamp (b, -1.0f, 1.0f)) * comp;
                }
                case fold:
                {
                    const float b = 0.5f * bias;
                    return (sin2pi (0.25f * (g * x + b)) - sin2pi (0.25f * b)) * comp;
                }
                case rectify:
                {
                    const float y = fastTanh (g * x);
                    const float w = 0.5f * (bias + 1.0f);   // -1 half-wave, +1 full-wave
                    return ((1.0f - w) * std::max (y, 0.0f) * 1.5f + w * std::abs (y)) * comp;
                }
                default:
                    return x;
            }
        }

        template <typename Block>
        void processBands (Block& block, int numSamples, int factor, const Settings& s, const Ramp& target) noexcept
        {
            float* io[2] = { block.getChannelPointer (0), block.getChannelPointer (1) };
            float* lowOut[2] = { block.getChannelPointer (2), block.getChannelPointer (3) };
            const float rate = (float) sampleRate * (float) factor;
            const float dcR = 1.0f - twoPi * 10.0f / rate;
            const float toneCoef = onePoleCoefHz (900.0f, rate);
            const float emphasisCoef = onePoleCoefHz (400.0f, rate);
            const float inv = 1.0f / (float) std::max (1, numSamples);

            // Crush: BIAS trades bits for rate.
            const float bitAmount = std::clamp (target.crush * (1.0f - 0.6f * target.bias), 0.0f, 1.0f);
            const float rateAmount = std::clamp (target.crush * (1.0f + 0.6f * target.bias), 0.0f, 1.0f);
            const float quant = std::exp2 (15.0f - 13.0f * std::pow (bitAmount, 0.8f));
            // Hold length 1..24 base-rate samples, counted at the oversampled rate so that no rate reduction holds one
            // oversampled sample (transparent, no added delay) at every oversampling setting.
            const float holdBase = 1.0f + 23.0f * rateAmount * std::sqrt (rateAmount);
            const float holdStep = 1.0f / std::max (1.0f, holdBase * (float) factor - (float) (factor - 1));

            for (int j = 0; j < numSamples; ++j)
            {
                const float t = (float) (j + 1) * inv;
                const float g = current.drive + (target.drive - current.drive) * t;
                const float comp = current.comp + (target.comp - current.comp) * t;
                const float bias = current.bias + (target.bias - current.bias) * t;
                const float tone = current.tone + (target.tone - current.tone) * t;
                const float emphasis = current.emphasis + (target.emphasis - current.emphasis) * t;
                const float amount = current.amount + (target.amount - current.amount) * t;
                const float highGain = current.highGain + (target.highGain - current.highGain) * t;

                for (int c = 0; c < 2; ++c)
                {
                    auto& st = chan[(size_t) c];
                    float lo, hi;
                    st.xo[(size_t) osIndex].process (io[c][j], lo, hi);
                    lowOut[c][j] = lo;

                    float shaped;
                    if (s.mode == crush)
                    {
                        st.holdPhase += holdStep;
                        if (st.holdPhase >= 1.0f)
                        {
                            st.holdPhase -= std::floor (st.holdPhase);
                            st.held = hi;
                        }
                        shaped = std::round (st.held * quant) / quant;
                    }
                    else
                    {
                        st.emphasisLp += (hi - st.emphasisLp) * emphasisCoef;
                        shaped = shape (hi + emphasis * (hi - st.emphasisLp), s.mode, g, bias, comp);
                    }

                    // Only the change, without DC and without anything below the split.
                    float delta = (shaped - hi) * amount;
                    const float dc = delta - st.dcIn + dcR * st.dcOut;
                    st.dcIn = delta;
                    st.dcOut = dc;
                    float pLo, pHi;
                    st.product[(size_t) osIndex].process (dc, pLo, pHi);
                    const float wet = hi + pHi;

                    st.toneLp += (wet - st.toneLp) * toneCoef;
                    const float tilted = st.toneLp * (1.0f - tone) + (wet - st.toneLp) * (1.0f + tone);
                    io[c][j] = tilted * highGain;
                }
            }
        }

        double sampleRate = 48000.0;
        std::unique_ptr<juce::dsp::Oversampling<float>> os2, os4;
        juce::AudioBuffer<float> work;
        StereoDelayLine pad, padLow;
        std::array<int, 3> latency { 0, 0, 0 };
        std::array<ChannelState, 2> chan {};
        int maxLatency = 0, osIndex = 1;
        Ramp current;
        bool firstBlock = true;
    };
}
