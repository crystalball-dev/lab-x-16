// Lx16Stub / Lx16StubZero: M0 host checks only (handoff section 5); never installed for the user's music.
// Gain, an impulse on every bar line (into the input path, so it travels through the latency like audio), a 4 s
// decaying tone (TAIL), a sidechain meter, a beep per MIDI note, the Zone Bus PEERS panel and both themes.
// LABX16_STUB_LATENCY_MS sets the fixed reported latency: 250 ms (12000 samples at 48 kHz) or 0.

#include "core/host/EffectProcessor.h"
#include "ui/EffectEditor.h"

#ifndef LABX16_STUB_LATENCY_MS
 #define LABX16_STUB_LATENCY_MS 250
#endif

namespace
{
    using namespace lab;
    using host::Kind;
    using host::Smooth;

    enum Param : int { gain = 0, impulse, theme };

    const host::EffectInfo& stubInfo()
    {
        static const host::EffectInfo info = []
        {
            host::EffectInfo i;
            i.product = LABX16_STUB_LATENCY_MS > 0 ? "STUB" : "STUB ZERO";
            i.subtitle = "ЗАГЛУШКА";
            i.slug = LABX16_SLUG;
            i.version = LABX16_VERSION;
            i.code = LABX16_CODE;
            i.faction = host::Faction::deepZone;
            i.sidechain = true;
            i.midi = true;
            i.busClient = true;
            i.params = {
                { "gain",    "Gain",    Kind::real,   -24.0f, 24.0f, 0.0f, 0.0f, Smooth::linear, "dB", "" },
                { "impulse", "Impulse", Kind::choice, 0.0f,   3.0f,  0.0f, 0.0f, Smooth::none,   "",   "Off|Bar L|Bar R|Bar L+R" },
                { "theme",   "Theme",   Kind::choice, 0.0f,   1.0f,  0.0f, 0.0f, Smooth::none,   "",   "Oxide|Vacuum" },
            };
            i.stages = { "stub" };
            i.presets = { { "Init", {} }, { "Impulse Left", { { "impulse", 1.0f } } }, { "Impulse Right", { { "impulse", 2.0f } } } };
            return i;
        }();
        return info;
    }

    class StubChain final : public host::Chain
    {
    public:
        std::atomic<bool> impulseRequest { false }, tailRequest { false };
        std::atomic<int> lastNote { -1 };

        void prepare (double sampleRate, int) override
        {
            sr = sampleRate;
            delay.prepare (std::max (1, latency (sampleRate)));
            tailDecay = std::exp (std::log (0.001) / (4.0 * sampleRate));
        }

        void reset() override
        {
            delay.reset();
            tailGain = 0.0;
            beepLeft = 0;
        }

        int latency (double sampleRate) const override { return (int) std::lround (LABX16_STUB_LATENCY_MS * 0.001 * sampleRate); }
        double tailSeconds (const float*) const override { return 4.5; }

        void control (const float* v, const host::BlockContext& ctx, int n) override
        {
            gainLin = dsp::dbToGain (v[gain]);
            impulseMode = (int) std::lround (v[impulse]);
            impulseAt = -1;
            const auto& t = *ctx.transport;
            if (impulseMode > 0 && t.playing && t.ppqPerSample > 0.0)
            {
                // The first bar line at or after this block's start, if it falls inside the block.
                const double qpb = t.quartersPerBar();
                const double barQ = std::ceil (t.ppq / qpb - 1.0e-9) * qpb;
                const double offset = (barQ - t.ppq) / t.ppqPerSample;
                if (offset >= -1.0e-6 && offset < (double) n)
                    impulseAt = std::clamp ((int) std::ceil (offset - 1.0e-6), 0, n - 1);
            }
            if (impulseRequest.exchange (false))
                impulseAt = 0;
            if (tailRequest.exchange (false))
            {
                tailGain = 0.5;
                tailPhase = 0.0;
            }
        }

        void midi (const juce::MidiMessage& m, const host::BlockContext&) override
        {
            if (m.isNoteOn())
            {
                lastNote.store (m.getNoteNumber());
                beepHz = dsp::midiToHz ((float) m.getNoteNumber());
                beepLeft = (int) (0.15 * sr);
                beepPhase = 0.0;
            }
        }

        void process (float* l, float* r, const float*, const float*, int n, host::Guard& guard) override
        {
            for (int i = 0; i < n; ++i)
            {
                float a = l[i] * gainLin, b = r[i] * gainLin;
                if (i == impulseAt)
                {
                    if (impulseMode != 2) a += 0.5f;
                    if (impulseMode != 1) b += 0.5f;
                }
                delay.push (a, b);
                const int d = latency (sr);
                float outL = d > 0 ? delay.readInt (0, d) : a;
                float outR = d > 0 ? delay.readInt (1, d) : b;

                if (tailGain > 1.0e-5)
                {
                    const float s = (float) (tailGain * std::sin (tailPhase));
                    tailPhase += dsp::twoPi * 440.0 / sr;
                    tailGain *= tailDecay;
                    outL += s;
                    outR += s;
                }
                if (beepLeft > 0)
                {
                    const float env = std::min (1.0f, (float) beepLeft / (float) (0.01 * sr));
                    const float s = 0.25f * env * (float) std::sin (beepPhase);
                    beepPhase += dsp::twoPi * beepHz / sr;
                    --beepLeft;
                    outL += s;
                    outR += s;
                }
                l[i] = outL;
                r[i] = outR;
            }
            guard (0, l, r, n);
        }

    private:
        double sr = 48000.0, tailGain = 0.0, tailPhase = 0.0, tailDecay = 0.9999, beepPhase = 0.0;
        float gainLin = 1.0f, beepHz = 880.0f;
        int impulseMode = 0, impulseAt = -1, beepLeft = 0;
        dsp::StereoDelayLine delay;
    };

    class StubProcessor final : public host::EffectProcessor
    {
    public:
        StubProcessor() : EffectProcessor (stubInfo(), std::make_unique<StubChain>()) {}
        StubChain& stub() { return static_cast<StubChain&> (getChain()); }
        juce::AudioProcessorEditor* createEditor() override;
    };

    class StubEditor final : public ui::EffectEditor
    {
    public:
        explicit StubEditor (StubProcessor& p) : EffectEditor (p), stubProcessor (p)
        {
            auto* impulseButton = own (std::make_unique<juce::TextButton> ("IMPULSE"));
            auto* tailButton = own (std::make_unique<juce::TextButton> ("TAIL"));
            ui::styleButton (*impulseButton, "One impulse now, through the latency like audio");
            ui::styleButton (*tailButton, "A 4 s decaying 440 Hz tone (does Smart disable cut it?)");
            impulseButton->onClick = [this] { stubProcessor.stub().impulseRequest.store (true); };
            tailButton->onClick = [this] { stubProcessor.stub().tailRequest.store (true); };
            sideBar = own (std::make_unique<ui::LevelBar>());
            sideLabel = own (std::make_unique<juce::Label>());
            midiLabel = own (std::make_unique<juce::Label>());
            for (auto* label : { sideLabel, midiLabel })
                label->setFont (ui::monoFont (11.0f));
            scope = own (std::make_unique<ui::ScopeView>());

            addRow ({ panel ("STUB // ЗАГЛУШКА", { { knob ("gain", "GAIN"), 1.0f }, { choices ({ { "impulse", "IMPULSE" }, { "theme", "THEME" } }), 1.4f },
                                                  { impulseButton, 1.0f }, { tailButton, 1.0f } }),
                      panel ("SIDECHAIN // САЙДЧЕЙН", { { sideBar, 1.6f } }, true) });
            addRow ({ panel ("MIDI // МИДИ", { { midiLabel, 2.0f } }), outputPanel() });
            addSide (panel ("PEERS // СВЯЗЬ", { { peersView(), 1.0f } }));
            addSide (panel ("SCOPE // ОСЦИЛЛОГРАФ", { { scope, 1.0f } }));
            finish();
        }

        void resized() override
        {
            EffectEditor::resized();
            // Buttons and the sidechain bar sit in the middle of their cells, not stretched over the whole panel.
            for (auto* c : getChildren())
                if (auto* b = dynamic_cast<juce::TextButton*> (c); b != nullptr && b->getHeight() > 30)
                    b->setBounds (b->getBounds().withSizeKeepingCentre (b->getWidth() - 8, 28));
            if (sideBar != nullptr)
            {
                const auto cell = sideBar->getBounds();
                sideBar->setBounds (cell.withSizeKeepingCentre (cell.getWidth() - 8, 14));
                sideLabel->setBounds (cell.withTrimmedTop (cell.getHeight() / 2 + 10).withHeight (16));
            }
        }

    private:
        void refresh (float) override
        {
            const auto& m = processor.meters;
            sideBar->setLevel (m.sideDb.load());
            sideLabel->setText (m.sidechainActive.load() ? "SIDECHAIN " + juce::String (m.sideDb.load(), 1) + " dB"
                                                         : juce::String ("SIDECHAIN: OFF IN HOST"),
                                juce::dontSendNotification);
            const int note = stubProcessor.stub().lastNote.load();
            midiLabel->setText ("MIDI EVENTS " + juce::String (m.midiEvents.load()) + (note >= 0 ? "   LAST NOTE " + juce::String (note) : juce::String()),
                                juce::dontSendNotification);
            std::array<float, 1024> buffer {};
            processor.copyScope (buffer.data(), (int) buffer.size());
            scope->setSamples (buffer.data(), (int) buffer.size());
            const auto& wanted = processor.effectRaw (Param::theme) > 0.5f ? ui::vacuumTheme() : ui::oxideTheme();
            if (&wanted != &getTheme())
                setTheme (wanted);
            sideLabel->setColour (juce::Label::textColourId, getTheme().phosphor);
            midiLabel->setColour (juce::Label::textColourId, getTheme().phosphor);
        }

        StubProcessor& stubProcessor;
        ui::LevelBar* sideBar = nullptr;
        juce::Label* sideLabel = nullptr;
        juce::Label* midiLabel = nullptr;
        ui::ScopeView* scope = nullptr;
    };

    juce::AudioProcessorEditor* StubProcessor::createEditor() { return new StubEditor (*this); }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new StubProcessor();
}
