// NUMBERS STATION // РАДИОСТАНЦИЯ — cryptography: one-time pads and frequency-hopping spread spectrum; shortwave
// numbers stations and UVB-76's buzzer. The Zone's radio chatter, the Monolith's jammer in the Pripyat kindergarten, the
// Duga array. A KEY seeds a hop sequence locked to FL's bar: a hopping band-pass (or notch), a single-sideband carrier,
// five-digit number groups as tone pips, and the buzzer. Two instances with the same key and COMPLEMENT set opposite
// pass exactly complementary spectra (band-pass + notch = the input), so two tracks interleave and never collide.

#include "core/host/EffectProcessor.h"
#include "core/dsp/Filters.h"
#include "core/dsp/Hilbert.h"
#include "core/dsp/Modulation.h"
#include "ui/EffectEditor.h"
#include "ui/Views.h"

namespace
{
    using namespace lab;
    using host::Kind;
    using host::Smooth;

    enum Param : int { keySeed = 0, hopRate, filterMode, complement, fLo, fHi, q, smooth, carrierHz, sideband, carrierDepth, numbers,
                       numbersLevel, buzzerLevel, transmission };
    enum FilterMode : int { pass = 0, notch, off };

    const host::EffectInfo& effectInfo()
    {
        static const host::EffectInfo i = []
        {
            host::EffectInfo e;
            e.product = LABX16_PRODUCT;
            e.subtitle = "РАДИОСТАНЦИЯ";
            e.slug = LABX16_SLUG;
            e.version = LABX16_VERSION;
            e.code = LABX16_CODE;
            e.faction = host::Faction::etherealSignal;
            e.params = {
                { "key_seed",      "Key",          Kind::integer, 0.0f,   9999.0f,  1.0f,    0.0f,    Smooth::none,   "",   "" },
                { "hop_rate",      "Hop Rate",     Kind::choice,  0.0f,   12.0f,    3.0f,    0.0f,    Smooth::none,   "",   dsp::rateChoicesSynced },
                { "filter_mode",   "Filter",       Kind::choice,  0.0f,   2.0f,     0.0f,    0.0f,    Smooth::none,   "",   "Pass|Notch|Off" },
                { "complement",    "Complement",   Kind::choice,  0.0f,   1.0f,     0.0f,    0.0f,    Smooth::none,   "",   "Off|On" },
                { "f_lo",          "Hop Low",      Kind::real,    50.0f,  2000.0f,  150.0f,  300.0f,  Smooth::log,    "Hz", "" },
                { "f_hi",          "Hop High",     Kind::real,    500.0f, 16000.0f, 6000.0f, 3000.0f, Smooth::log,    "Hz", "" },
                { "q",             "Q",            Kind::real,    0.5f,   20.0f,    4.0f,    3.0f,    Smooth::log,    "",   "" },
                { "smooth",        "Glide",        Kind::real,    1.0f,   200.0f,   20.0f,   20.0f,   Smooth::none,   "ms", "" },
                { "carrier_hz",    "Carrier",      Kind::real,    50.0f,  5000.0f,  440.0f,  440.0f,  Smooth::log,    "Hz", "" },
                { "sideband",      "Sideband",     Kind::choice,  0.0f,   2.0f,     1.0f,    0.0f,    Smooth::none,   "",   "Both|Upper|Lower" },
                { "carrier_depth", "Carrier Depth",Kind::real,    0.0f,   1.0f,     0.0f,    0.0f,    Smooth::linear, "%",  "" },
                { "numbers",       "Numbers",      Kind::choice,  0.0f,   1.0f,     0.0f,    0.0f,    Smooth::none,   "",   "Off|On" },
                { "numbers_level", "Numbers Level",Kind::real,    0.0f,   1.0f,     0.3f,    0.0f,    Smooth::linear, "%",  "" },
                { "buzzer_level",  "Buzzer",       Kind::real,    0.0f,   1.0f,     0.0f,    0.0f,    Smooth::linear, "%",  "" },
                { "transmission",  "Transmission", Kind::real,    0.0f,   1.0f,     0.0f,    0.0f,    Smooth::linear, "%",  "" },
            };
            e.stages = { "hop", "carrier" };
            e.presets = {
                { "Init", {} },
                { "UVB-76", { { "buzzer_level", 0.6f }, { "filter_mode", 2.0f }, { "key_seed", 76.0f }, { "output", -3.2f } } },
                { "Five Digit Groups", { { "numbers", 1.0f }, { "numbers_level", 0.4f }, { "filter_mode", 2.0f }, { "carrier_depth", 0.2f }, { "output", -1.3f } } },
                { "Hop Sixteenth", { { "hop_rate", 4.0f }, { "q", 2.0f }, { "f_lo", 50.0f }, { "f_hi", 3000.0f }, { "output", 9.7f } } },
                { "Complement A", { { "key_seed", 1986.0f }, { "complement", 0.0f } } },
                { "Complement B", { { "key_seed", 1986.0f }, { "complement", 1.0f } } },
                { "Jammer", { { "filter_mode", 1.0f }, { "q", 1.0f }, { "carrier_depth", 0.6f }, { "hop_rate", 5.0f }, { "output", 3.1f } } },
            };
            return e;
        }();
        return i;
    }

    uint32_t hashText (const juce::String& text) noexcept
    {
        uint32_t h = 2166136261u;   // FNV-1a over the UTF-8 bytes
        for (auto p = text.toRawUTF8(); *p != 0; ++p)
        {
            h ^= (uint8_t) *p;
            h *= 16777619u;
        }
        return text.isEmpty() ? 0u : h;
    }

    class NumbersChain final : public host::Chain
    {
    public:
        std::atomic<float> uiHz { 1000.0f };
        std::atomic<int> uiDigits { 0 }, uiDigit { -1 };

        void prepare (double sampleRate, int) override
        {
            sr = (float) sampleRate;
            for (auto& s : shifters)
                s.prepare (sampleRate);
            buzzHp.setHz (300.0f, true, sr);
            buzzLp.setHz (3000.0f, false, sr);
            edge = 1.0f / std::max (1.0f, 0.002f * sr);
            onAir.prepare (sr);
            reset();
        }

        void reset() override
        {
            for (auto& f : hopFilter) f.reset();
            for (auto& s : shifters) s.reset();
            buzzHp.reset();
            buzzLp.reset();
            logHz = -1.0f;
            pipPhase = buzzPhase = 0.0f;
            pipGain = buzzGain = 0.0f;
        }

        double tailSeconds (const float*) const override { return 0.1; }

        // The KEY as text (message thread). It is hashed into the seed with the KEY number.
        void setKeyText (const juce::String& text)
        {
            {
                const juce::SpinLock::ScopedLockType lock (textLock);
                keyText = text.substring (0, 64);
            }
            textHash.store (hashText (text.substring (0, 64)));
        }
        juce::String getKeyText() const
        {
            const juce::SpinLock::ScopedLockType lock (textLock);
            return keyText;
        }

        void presetLoaded (int) override { setKeyText ({}); }   // a preset's key is its KEY number alone
        bool loadText (const juce::String& text) override { setKeyText (text); return true; }

        int blobVersion() const override { return 1; }
        void saveBlob (juce::MemoryOutputStream& out) const override { out.writeString (getKeyText()); }
        bool loadBlob (juce::MemoryInputStream& in, int version) override
        {
            if (version != 1)
                return false;
            setKeyText (in.readString());
            return true;
        }

        void control (const float* v, const host::BlockContext& ctx, int n) override
        {
            const float tx = v[transmission];
            seed = textHash.load() ^ (uint32_t) std::lround (v[keySeed]) * 2654435761u;
            mode = (int) std::lround (v[filterMode]);
            if (v[complement] > 0.5f && mode != off)
                mode = mode == pass ? notch : pass;
            const auto& t = *ctx.transport;

            // The hop: one hash01 decision per step of HOP RATE (the song's steps while it plays, the sample clock's at
            // the last tempo while it is stopped).
            const double beats = dsp::syncedRateBeats ((int) std::lround (v[hopRate]));
            const double quarters = t.playing ? t.ppq : (double) ctx.sampleClock * t.ppqPerSample;
            hopIndex = (int64_t) std::floor (quarters / beats + 1.0e-9);
            const float h = dsp::hash01 (hopIndex, 0, 0x40Bu, seed);
            const float lo = std::min (v[fLo], v[fHi]), hi = std::max (v[fLo], v[fHi]);
            const float target = std::log2 (lo) + h * (std::log2 (hi) - std::log2 (lo));
            if (logHz < 0.0f)
                logHz = target;
            logHz += (target - logHz) * (1.0f - std::exp (-(float) n / (std::max (1.0f, v[smooth]) * 0.001f * sr)));
            const float hz = std::min (std::exp2 (logHz), 0.45f * sr);
            const float qNow = std::clamp (v[q] * (1.0f + 2.0f * tx), 0.5f, 40.0f);
            for (auto& f : hopFilter)
                f.setHz (hz, qNow, sr);
            uiHz.store (hz);

            carrierNow = v[carrierHz];
            sidebandNow = (int) std::lround (v[sideband]);
            depth = std::clamp (v[carrierDepth] + 0.6f * tx, 0.0f, 1.0f);

            // NUMBERS: a five-digit group from the key and the bar, at the start of every bar; digit d is a pip at
            // 400 + 60 d Hz, 80 ms on and 40 ms off.
            // The pips and the buzzer are the station's own transmission: on the air while the song plays, off when it
            // stops (the hop filter on the track's own audio keeps working).
            onAir.set (t.playing);
            numbersOn = v[numbers] > 0.5f;
            numbersGain = std::clamp (v[numbersLevel] + 0.5f * tx, 0.0f, 1.0f) * 0.3f;
            const double qpb = t.quartersPerBar(), secPerQ = 60.0 / std::max (1.0, t.bpm);
            const int64_t bar = (int64_t) std::floor (quarters / qpb + 1.0e-9);
            const double intoBar = (quarters - (double) bar * qpb) * secPerQ;
            int group = 0;
            for (int k = 0; k < 5; ++k)
                group = group * 10 + (int) (dsp::hash01 (bar, k, 0x9A9Eu, seed) * 10.0f) % 10;
            const int slot = (int) std::floor (intoBar / 0.12);
            digit = -1;
            if (numbersOn && slot >= 0 && slot < 5 && intoBar - 0.12 * slot < 0.08)
            {
                int g = group;
                for (int k = 4; k > slot; --k)
                    g /= 10;
                digit = g % 10;
            }
            uiDigits.store (group);
            uiDigit.store (digit);

            // The buzzer: 25 Hz (two semitones either way, from the key), on for 0.8 s of every 1.25 s of song time.
            buzzerGain = v[buzzerLevel] * 0.3f;
            buzzHz = 25.0f * dsp::semisToRatio ((dsp::hash01 (0, 0, 0xB022u, seed) - 0.5f) * 4.0f);
            songSeconds = quarters * secPerQ;
        }

        void process (float* l, float* r, const float*, const float*, int n, host::Guard& guard) override
        {
            if (mode != off)
                for (int i = 0; i < n; ++i)
                {
                    const float bl = hopFilter[0].bandpass (l[i]), br = hopFilter[1].bandpass (r[i]);
                    if (mode == pass) { l[i] = bl; r[i] = br; }
                    else              { l[i] -= bl; r[i] -= br; }
                }
            guard (0, l, r, n);

            const double perSample = 1.0 / (double) sr;
            for (int i = 0; i < n; ++i)
            {
                if (depth > 1.0e-4f)
                {
                    const float sl = shifters[0].process (l[i], carrierNow, sidebandNow);
                    const float srr = shifters[1].process (r[i], carrierNow, sidebandNow);
                    l[i] += (sl - l[i]) * depth;
                    r[i] += (srr - r[i]) * depth;
                }
                float extra = 0.0f;
                pipGain = std::clamp (pipGain + (digit >= 0 ? edge : -edge), 0.0f, 1.0f);
                if (pipGain > 0.0f && numbersGain > 0.0f)
                {
                    pipPhase = dsp::wrap01 (pipPhase + (400.0f + 60.0f * (float) std::max (0, lastDigit)) / sr);
                    extra += dsp::sin2pi (pipPhase) * pipGain * numbersGain;
                }
                if (digit >= 0)
                    lastDigit = digit;
                if (buzzerGain > 0.0f)
                {
                    const double t = songSeconds + (double) i * perSample;
                    const bool on = std::fmod (t, 1.25) < 0.8;
                    buzzGain = std::clamp (buzzGain + (on ? edge : -edge), 0.0f, 1.0f);
                    buzzPhase = dsp::wrap01 (buzzPhase + buzzHz / sr);
                    const float pulse = buzzPhase < 0.08f ? 1.0f : 0.0f;   // narrow pulses: a buzz rich in harmonics
                    extra += buzzLp.process (buzzHp.process (pulse - 0.08f)) * buzzGain * buzzerGain * 4.0f;
                }
                extra *= onAir.next();
                l[i] += extra;
                r[i] += extra;
            }
            guard (1, l, r, n);
        }

        void meter (host::Meters& m) const override
        {
            m.custom[0].store (uiHz.load());
            m.custom[1].store ((float) hopIndex);
            m.custom[2].store ((float) (seed & 0xffffff));
            m.custom[3].store ((float) uiDigits.load());
            m.custom[4].store ((float) digit);
            m.custom[5].store (buzzGain);
        }

    private:
        std::array<dsp::SVF, 2> hopFilter {};
        std::array<dsp::FrequencyShifter, 2> shifters;
        dsp::Butter4 buzzHp, buzzLp;
        mutable juce::SpinLock textLock;
        juce::String keyText;
        std::atomic<uint32_t> textHash { 0 };
        float sr = 48000.0f, logHz = -1.0f, carrierNow = 440.0f, depth = 0.0f, numbersGain = 0.09f, buzzerGain = 0.0f, buzzHz = 25.0f;
        float pipPhase = 0.0f, buzzPhase = 0.0f, pipGain = 0.0f, buzzGain = 0.0f, edge = 0.01f;
        dsp::PlayGate onAir;
        double songSeconds = 0.0;
        int mode = pass, sidebandNow = 1, digit = -1, lastDigit = 0;
        bool numbersOn = false;
        int64_t hopIndex = 0;
        uint32_t seed = 0;
    };

    class NumbersProcessor final : public host::EffectProcessor
    {
    public:
        NumbersProcessor() : EffectProcessor (effectInfo(), std::make_unique<NumbersChain>()) {}
        NumbersChain& station() { return static_cast<NumbersChain&> (getChain()); }
        juce::AudioProcessorEditor* createEditor() override;
    };

    class NumbersEditor final : public ui::EffectEditor
    {
    public:
        explicit NumbersEditor (NumbersProcessor& p) : EffectEditor (p), fx (p)
        {
            readout = own (std::make_unique<ui::BigReadout>());
            keyEditor = own (std::make_unique<juce::TextEditor>());
            keyEditor->setFont (ui::monoFont (13.0f, true));
            keyEditor->setTextToShowWhenEmpty ("TYPE A KEY", getTheme().dim);
            keyEditor->setText (fx.station().getKeyText(), false);
            keyEditor->setTooltip ("The key's text, hashed with the KEY number into the hop sequence (saved with the project)");
            keyEditor->onReturnKey = keyEditor->onFocusLost = [this] { fx.station().setKeyText (keyEditor->getText()); };
            addRow ({ panel ("TRANSMISSION // ПЕРЕДАЧА", { { readout, 3.0f }, { keyEditor, 2.2f }, { knob ("transmission", "TRANSMIT", true), 1.0f } }, true) });
            addRow ({ panel ("HOP // СКАЧОК", { { knob ("key_seed", "KEY"), 1.0f }, { choices ({ { "hop_rate", "RATE" }, { "filter_mode", "FILTER" } }), 1.3f },
                                               { choices ({ { "complement", "COMPLEMENT" } }), 1.2f }, { knob ("f_lo", "LOW"), 1.0f },
                                               { knob ("f_hi", "HIGH"), 1.0f }, { knob ("q", "Q"), 1.0f }, { knob ("smooth", "GLIDE"), 1.0f } }) });
            addRow ({ panel ("CARRIER // НЕСУЩАЯ", { { knob ("carrier_hz", "CARRIER"), 1.0f }, { choices ({ { "sideband", "SIDEBAND" } }), 1.2f },
                                                   { knob ("carrier_depth", "DEPTH"), 1.0f } }),
                      panel ("NUMBERS // ЦИФРЫ", { { choices ({ { "numbers", "NUMBERS" } }), 1.2f }, { knob ("numbers_level", "LEVEL"), 1.0f },
                                                  { knob ("buzzer_level", "BUZZER"), 1.0f } }),
                      outputPanel() });
            finish();
        }

        void resized() override
        {
            EffectEditor::resized();
            if (keyEditor != nullptr)
                keyEditor->setBounds (keyEditor->getBounds().withSizeKeepingCentre (keyEditor->getWidth() - 8, 26));
        }

    private:
        void refresh (float) override
        {
            auto& s = fx.station();
            const int digits = s.uiDigits.load(), now = s.uiDigit.load();
            const auto hz = s.uiHz.load();
            readout->set ("HOP " + (hz >= 1000.0f ? juce::String (hz / 1000.0f, 2) + " kHz" : juce::String (juce::roundToInt (hz)) + " Hz"),
                          juce::String (digits).paddedLeft ('0', 5) + (now >= 0 ? "  [" + juce::String (now) + "]" : juce::String()), now >= 0);
            const auto& t = getTheme();
            keyEditor->setColour (juce::TextEditor::backgroundColourId, t.background);
            keyEditor->setColour (juce::TextEditor::textColourId, t.bright);
            keyEditor->setColour (juce::TextEditor::outlineColourId, t.border);
        }

        NumbersProcessor& fx;
        ui::BigReadout* readout = nullptr;
        juce::TextEditor* keyEditor = nullptr;
    };

    juce::AudioProcessorEditor* NumbersProcessor::createEditor() { return new NumbersEditor (*this); }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new NumbersProcessor();
}
