// PSY-FIELD // ПСИ-ПОЛЕ — zone_field_psychic: the psy-emission that drains mental strength and fills the head with
// voices. EXPOSURE integrates while the field source stays above its threshold (Shadow of Chornobyl's mental-strength
// bar, run backwards) and deepens everything: an anomalous pitch DRIFT, a vowel RESONATOR, WHISPER formants and the
// controller's PRESENCE whine. The voices follow the input's envelope, so silence stays silent.

#include "core/host/EffectProcessor.h"
#include "core/dsp/Filters.h"
#include "core/dsp/Follower.h"
#include "core/dsp/Generators.h"
#include "core/dsp/MicroShift.h"
#include "core/dsp/Resonator.h"
#include "ui/EffectEditor.h"

namespace
{
    using namespace lab;
    using host::Kind;
    using host::Smooth;

    enum Param : int { exposure = 0, exposureMode, exposureTime, recoveryTime, threshold, fieldSource, pitchSource, resPitch,
                       resFeedback, vowel, resLevel, whisper, whisperF0, presence, driftDepth, driftRate };

    const host::EffectInfo& effectInfo()
    {
        static const host::EffectInfo i = []
        {
            host::EffectInfo e;
            e.product = LABX16_PRODUCT;
            e.subtitle = "ПСИ-ПОЛЕ";
            e.slug = LABX16_SLUG;
            e.version = LABX16_VERSION;
            e.code = LABX16_CODE;
            e.faction = host::Faction::deepZone;
            e.sidechain = true;
            e.midi = true;
            e.params = {
                { "exposure",      "Exposure",          Kind::real,   0.0f,   1.0f,    0.0f,   0.0f,   Smooth::linear, "%",  "" },
                { "exposure_mode", "Exposure Mode",     Kind::choice, 0.0f,   1.0f,    0.0f,   0.0f,   Smooth::none,   "",   "Auto|Manual" },
                { "exposure_time", "Onset Time",        Kind::real,   1.0f,   60.0f,   12.0f,  12.0f,  Smooth::linear, "s",  "" },
                { "recovery_time", "Recovery",          Kind::real,   1.0f,   120.0f,  20.0f,  20.0f,  Smooth::linear, "s",  "" },
                { "threshold",     "Field Threshold",   Kind::real,   -60.0f, 0.0f,    -36.0f, 0.0f,   Smooth::linear, "dB", "" },
                { "field_source",  "Field Source",      Kind::choice, 0.0f,   1.0f,    0.0f,   0.0f,   Smooth::none,   "",   "Input|Sidechain" },
                { "pitch_source",  "Pitch Source",      Kind::choice, 0.0f,   2.0f,    0.0f,   0.0f,   Smooth::none,   "",   "Fixed|MIDI|Auto" },
                { "res_pitch",     "Resonator Pitch",   Kind::real,   30.0f,  2000.0f, 110.0f, 200.0f, Smooth::log,    "Hz", "" },
                { "res_feedback",  "Resonator Feedback",Kind::real,   0.0f,   1.0f,    0.4f,   0.0f,   Smooth::linear, "%",  "" },
                { "vowel",         "Vowel",             Kind::real,   0.0f,   1.0f,    0.3f,   0.0f,   Smooth::linear, "%",  "" },
                { "res_level",     "Resonator Level",   Kind::real,   0.0f,   1.0f,    0.5f,   0.0f,   Smooth::linear, "%",  "" },
                { "whisper",       "Whisper",           Kind::real,   0.0f,   1.0f,    0.3f,   0.0f,   Smooth::linear, "%",  "" },
                { "whisper_f0",    "Whisper Formant",   Kind::real,   60.0f,  400.0f,  127.0f, 150.0f, Smooth::log,    "Hz", "" },
                { "presence",      "Presence",          Kind::real,   0.0f,   1.0f,    0.15f,  0.0f,   Smooth::linear, "%",  "" },
                { "drift_depth",   "Drift",             Kind::real,   0.0f,   200.0f,  40.0f,  50.0f,  Smooth::linear, "ct", "" },
                { "drift_rate",    "Drift Rate",        Kind::real,   0.02f,  2.0f,    0.15f,  0.2f,   Smooth::log,    "Hz", "" },
            };
            e.stages = { "drift", "resonator" };
            e.presets = {
                { "Init", {} },
                { "Yantar Approach", { { "exposure_time", 40.0f }, { "recovery_time", 60.0f }, { "whisper", 0.4f }, { "presence", 0.1f },
                                       { "drift_depth", 30.0f }, { "output", 1.3f } } },
                { "Mental Strength Zero", { { "exposure_mode", 1.0f }, { "exposure", 1.0f }, { "res_feedback", 0.6f }, { "whisper", 0.5f }, { "output", 1.4f } } },
                { "Controller's Den", { { "vowel", 1.0f }, { "drift_depth", 120.0f }, { "drift_rate", 0.3f }, { "res_feedback", 0.7f },
                                        { "exposure_mode", 1.0f }, { "exposure", 0.7f }, { "output", 1.2f } } },
                { "Red Forest Phantoms", { { "whisper", 0.9f }, { "whisper_f0", 90.0f }, { "presence", 0.2f }, { "exposure_time", 6.0f }, { "output", 1.1f } } },
                { "Kindergarten Jammer", { { "presence", 0.8f }, { "drift_rate", 1.5f }, { "drift_depth", 60.0f }, { "exposure_mode", 1.0f },
                                           { "exposure", 0.6f }, { "output", 0.9f } } },
                { "Psy-block", { { "recovery_time", 2.0f }, { "exposure_time", 6.0f }, { "res_level", 0.35f }, { "output", 1.2f } } },
            };
            return e;
        }();
        return i;
    }

    // Pitch tracking (PITCH SOURCE Auto): YIN on a 2 kHz low-passed mono copy decimated to about 12 kHz, run every 10 ms
    // over a 40 ms window for 50-1000 Hz; the last confident pitch is held for 100 ms when confidence drops.
    class PitchTracker
    {
    public:
        void prepare (double sr)
        {
            decimation = std::max (1, (int) std::lround (sr / 12000.0));
            rate = sr / (double) decimation;
            lp.setHz (2000.0f, false, (float) sr);
            window = (int) std::lround (0.02 * rate);
            maxLag = std::min (window, (int) (rate / 50.0));
            minLag = std::max (2, (int) (rate / 1000.0));
            ring.assign ((size_t) (2 * window + 4), 0.0f);
            diff.assign ((size_t) maxLag + 2, 0.0f);
            hop = std::max (1, (int) std::lround (0.01 * sr));
            holdSamples = (int) (0.1 * sr);
            reset();
        }

        void reset() noexcept
        {
            lp.reset();
            std::fill (ring.begin(), ring.end(), 0.0f);
            pos = phase = counter = 0;
            hz = 0.0f;
            sinceGood = holdSamples + 1;
        }

        void push (float x) noexcept
        {
            const float y = lp.process (x);
            if (++phase >= decimation)
            {
                phase = 0;
                ring[(size_t) pos] = y;
                pos = (pos + 1) % (int) ring.size();
            }
            ++sinceGood;
            if (++counter >= hop)
            {
                counter = 0;
                analyse();
            }
        }

        // The tracked pitch, or 0 when there is none (silence, noise, or confidence lost for over 100 ms).
        float pitch() const noexcept { return sinceGood <= holdSamples ? hz : 0.0f; }

    private:
        void analyse() noexcept
        {
            const int size = (int) ring.size();
            auto at = [&] (int k) { return ring[(size_t) ((pos - 2 * window + k + 2 * size) % size)]; };   // oldest first
            float energy = 0.0f;
            for (int j = 0; j < 2 * window; ++j)
                energy += at (j) * at (j);
            if (energy < 1.0e-7f * (float) (2 * window))
                return;
            float running = 0.0f;
            int found = -1;
            for (int tau = 1; tau <= maxLag; ++tau)
            {
                float d = 0.0f;
                for (int j = 0; j < window; ++j)
                {
                    const float e = at (j) - at (j + tau);
                    d += e * e;
                }
                running += d;
                diff[(size_t) tau] = running > 0.0f ? d * (float) tau / running : 1.0f;
            }
            for (int tau = minLag; tau < maxLag; ++tau)
                if (diff[(size_t) tau] < 0.15f)
                {
                    while (tau + 1 < maxLag && diff[(size_t) tau + 1] < diff[(size_t) tau])
                        ++tau;
                    found = tau;
                    break;
                }
            if (found < 0)
                return;
            const float a = diff[(size_t) found - 1], b = diff[(size_t) found], c = diff[(size_t) found + 1];
            const float den = a - 2.0f * b + c;
            const float shift = std::abs (den) > 1.0e-9f ? 0.5f * (a - c) / den : 0.0f;
            hz = (float) (rate / ((double) found + (double) std::clamp (shift, -0.5f, 0.5f)));
            sinceGood = 0;
        }

        dsp::Butter4 lp;
        std::vector<float> ring, diff;
        double rate = 12000.0;
        int decimation = 4, window = 240, maxLag = 240, minLag = 12, hop = 480, pos = 0, phase = 0, counter = 0;
        int holdSamples = 4800, sinceGood = 0;
        float hz = 0.0f;
    };

    class PsyFieldChain final : public host::Chain
    {
    public:
        void prepare (double sampleRate, int) override
        {
            sr = (float) sampleRate;
            fieldFollower.set (0.005f, 0.12f, sr);
            inputFollower.set (0.005f, 0.12f, sr);
            for (auto& s : shift)
            {
                s.prepare (sr, 0.06f);
                s.setWindow (0.05f);
            }
            resonator.prepare (sr, 25.0f);
            presenceGen.prepare (sr);
            tracker.prepare (sampleRate);
            bypassStep = 1.0f / (0.03f * sr);
            seed();
            reset();
        }

        void reset() override
        {
            fieldFollower.reset();
            inputFollower.reset();
            for (auto& s : shift)
                s.reset();
            resonator.reset();
            for (auto& w : whisperGen)
                w.reset();
            presenceGen.reset();
            tracker.reset();
            walk.reset (sr);
            exposed = 0.0f;
        }

        double tailSeconds (const float*) const override { return 3.0; }

        void control (const float* v, const host::BlockContext& ctx, int) override
        {
            if (ctx.callsign != callsign)
            {
                callsign = ctx.callsign;
                seed();
            }
            p = v;
            sideActive = ctx.sidechainActive;
        }

        void midi (const juce::MidiMessage& m, const host::BlockContext&) override
        {
            if (m.isNoteOn())
                midiHz.store (dsp::midiToHz ((float) m.getNoteNumber()));
        }

        void process (float* l, float* r, const float* sideL, const float* sideR, int n, host::Guard& guard) override
        {
            if (p == nullptr)
                return;
            const bool fromSide = p[fieldSource] > 0.5f && sideActive;
            float fieldEnv = 0.0f;
            std::array<float, host::controlBlock> envIn {};
            for (int i = 0; i < n; ++i)
            {
                fieldEnv = fromSide ? fieldFollower.processStereo (sideL[i], sideR[i]) : fieldFollower.processStereo (l[i], r[i]);
                envIn[(size_t) i] = inputFollower.processStereo (l[i], r[i]);
                if (pitchMode == 2)
                    tracker.push (0.5f * (l[i] + r[i]));
            }

            // EXPOSURE: Auto integrates (the knob adds to it); Manual is the knob.
            const float dt = (float) n / sr;
            if (dsp::gainToDb (fieldEnv) > p[threshold])
                exposed += dt / std::max (0.1f, p[exposureTime]);
            else
                exposed -= dt / std::max (0.1f, p[recoveryTime]);
            exposed = std::clamp (exposed, 0.0f, 1.0f);
            const bool manual = p[exposureMode] > 0.5f;
            e = manual ? p[exposure] : std::clamp (exposed + p[exposure], 0.0f, 1.0f);

            // DRIFT: a two-head shifter detuned by a slow walk; at no drift the shifter fades out (it would comb at rest).
            const float depth = p[driftDepth] * (0.3f + 0.7f * e);
            cents = depth * walk.advance (n, p[driftRate]);
            const float ratio = dsp::MicroShift::centsToRatio (cents);
            const float bypassTarget = depth < 0.5f ? 0.0f : 1.0f;
            for (int i = 0; i < n; ++i)
            {
                shiftMix = bypassTarget > shiftMix ? std::min (1.0f, shiftMix + bypassStep) : std::max (bypassTarget, shiftMix - bypassStep);
                const float sl = shift[0].process (l[i], ratio), srr = shift[1].process (r[i], ratio);
                l[i] += (sl - l[i]) * shiftMix;
                r[i] += (srr - r[i]) * shiftMix;
            }
            guard (0, l, r, n);

            // RESONATOR at the pitch source's frequency, then the voices keyed by the input's envelope.
            pitchMode = (int) std::lround (p[pitchSource]);
            float hz = p[resPitch];
            if (pitchMode == 1 && midiHz.load() > 0.0f)
                hz = midiHz.load();
            else if (pitchMode == 2 && tracker.pitch() > 0.0f)
                hz = tracker.pitch();
            resonatorHz = hz;
            resonator.set (hz, std::min (0.97f, p[resFeedback] + 0.3f * e), std::min (1.0f, p[vowel] + 0.2f * e), n);
            for (auto& w : whisperGen)
                w.setFormant (p[whisperF0]);
            const float resGain = p[resLevel];
            const float whisperGain = p[whisper] * e * 0.5f;
            const float presenceGain = p[presence] * e * 0.18f;
            for (int i = 0; i < n; ++i)
            {
                const float res = resonator.process (0.5f * (l[i] + r[i])) * resGain;
                const float env = envIn[(size_t) i];
                const float voice = presenceGen.next (3100.0f) * presenceGain * env;
                l[i] += res + (whisperGen[0].next() * whisperGain * env + voice);
                r[i] += res + (whisperGen[1].next() * whisperGain * env + voice);
            }
            guard (1, l, r, n);
        }

        void meter (host::Meters& m) const override
        {
            m.custom[0].store (e);               // EXPOSURE (effective)
            m.custom[1].store (resonatorHz);     // resonator pitch
            m.custom[2].store (cents);           // drift now (cents)
            m.custom[3].store (tracker.pitch()); // tracked pitch, 0 = none
            m.custom[4].store (exposed);         // the integrator alone
        }

    private:
        void seed()
        {
            walk.seed (dsp::hash32 (0, 0, 0xD21Fu, callsign) | 1u);
            walk.reset (sr);
            whisperGen[0].prepare (sr, dsp::hash32 (0, 1, 0x3157u, callsign));
            whisperGen[1].prepare (sr, dsp::hash32 (0, 2, 0x3157u, callsign));
        }

        dsp::Follower fieldFollower, inputFollower;
        std::array<dsp::MicroShift, 2> shift;
        dsp::Resonator resonator;
        std::array<dsp::Whisper, 2> whisperGen;
        dsp::Presence presenceGen;
        dsp::Programmer walk;
        PitchTracker tracker;
        std::atomic<float> midiHz { 0.0f };
        const float* p = nullptr;
        float sr = 48000.0f, exposed = 0.0f, e = 0.0f, cents = 0.0f, shiftMix = 1.0f, bypassStep = 0.001f, resonatorHz = 110.0f;
        int pitchMode = 0;
        bool sideActive = false;
        uint32_t callsign = 0;
    };

    class PsyFieldProcessor final : public host::EffectProcessor
    {
    public:
        PsyFieldProcessor() : EffectProcessor (effectInfo(), std::make_unique<PsyFieldChain>()) {}
        juce::AudioProcessorEditor* createEditor() override;
    };

    // The editor's tint follows EXPOSURE: Shadow of Chornobyl's yellow (the theme itself) toward Call of Pripyat's blue.
    class Tint final : public juce::Component
    {
    public:
        Tint() { setInterceptsMouseClicks (false, false); }
        void set (float amount)
        {
            if (std::abs (amount - level) > 0.004f)
            {
                level = amount;
                repaint();
            }
        }
        void paint (juce::Graphics& g) override
        {
            if (level > 0.004f)
                g.fillAll (juce::Colour (0xff3a7bd5).withAlpha (0.16f * level));
        }

    private:
        float level = 0.0f;
    };

    class PsyFieldEditor final : public ui::EffectEditor
    {
    public:
        explicit PsyFieldEditor (PsyFieldProcessor& p) : EffectEditor (p)
        {
            needle = own (std::make_unique<ui::Needle> ("EXPOSURE", 0.85f));
            addRow ({ panel ("FIELD // ПОЛЕ", { { needle, 1.8f }, { knob ("exposure", "EXPOSURE", true), 1.0f },
                                               { choices ({ { "exposure_mode", "MODE" }, { "field_source", "SOURCE" } }), 1.3f },
                                               { knob ("exposure_time", "ONSET"), 1.0f }, { knob ("recovery_time", "RECOVERY"), 1.0f },
                                               { knob ("threshold", "THRESHOLD"), 1.0f } }, true) });
            addRow ({ panel ("RESONATOR // РЕЗОНАТОР", { { choices ({ { "pitch_source", "PITCH" } }), 1.2f }, { knob ("res_pitch", "HZ"), 1.0f },
                                                        { knob ("res_feedback", "FEEDBACK"), 1.0f }, { knob ("vowel", "VOWEL"), 1.0f },
                                                        { knob ("res_level", "LEVEL"), 1.0f } }),
                      panel ("VOICES // ГОЛОСА", { { knob ("whisper", "WHISPER"), 1.0f }, { knob ("whisper_f0", "FORMANT"), 1.0f },
                                                  { knob ("presence", "PRESENCE"), 1.0f } }) });
            addRow ({ panel ("DRIFT // ДРЕЙФ", { { knob ("drift_depth", "DRIFT"), 1.0f }, { knob ("drift_rate", "RATE"), 1.0f } }),
                      outputPanel() });
            tint = own (std::make_unique<Tint>());
            finish();
        }

        void resized() override
        {
            EffectEditor::resized();
            if (tint != nullptr)
                tint->setBounds (getLocalBounds());
        }

    private:
        void refresh (float seconds) override
        {
            const auto& m = processor.meters;
            const float exposureNow = m.custom[0].load();
            needle->setReadout (juce::String (juce::roundToInt (exposureNow * 100.0f)) + "%  "
                                + juce::String (juce::roundToInt (m.custom[1].load())) + " Hz");
            needle->advance (exposureNow, seconds, 0.4f * exposureNow);
            tint->set (exposureNow);
        }

        ui::Needle* needle = nullptr;
        Tint* tint = nullptr;
    };

    juce::AudioProcessorEditor* PsyFieldProcessor::createEditor() { return new PsyFieldEditor (*this); }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PsyFieldProcessor();
}
