// Lx16BusHost: loads LAB X-16 VST3 plugins into this one process (as FL Studio does when nothing is bridged), runs a
// few blocks through each, then reads the Zone Bus block directly and checks that every instance registered on the
// same bus with a fresh heartbeat: instances of different effect DLLs must see each other.
//
//   Lx16BusHost <plugin.vst3> <plugin.vst3> [...]       (a VST3 bundle folder or the single-file DLL)

#include <juce_audio_processors/juce_audio_processors.h>
#include <iostream>
#include "core/host/ZoneBus.h"

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI gui;
    juce::VST3PluginFormat vst3;
    std::vector<std::unique_ptr<juce::AudioPluginInstance>> plugins;
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 512;

    for (int i = 1; i < argc; ++i)
    {
        const auto path = juce::String::fromUTF8 (argv[i]);
        juce::OwnedArray<juce::PluginDescription> found;
        vst3.findAllTypesForFile (found, path);
        if (found.isEmpty())
        {
            std::cerr << "no VST3 plugin in " << path << std::endl;
            return 2;
        }
        juce::String error;
        auto instance = vst3.createInstanceFromDescription (*found[0], sampleRate, blockSize, error);
        if (instance == nullptr)
        {
            std::cerr << "cannot load " << path << ": " << error << std::endl;
            return 2;
        }
        instance->setRateAndBufferSizeDetails (sampleRate, blockSize);
        instance->prepareToPlay (sampleRate, blockSize);
        std::cout << "loaded \"" << instance->getName() << "\" inputs=" << instance->getTotalNumInputChannels()
                  << " outputs=" << instance->getTotalNumOutputChannels() << " latency=" << instance->getLatencySamples() << std::endl;
        plugins.push_back (std::move (instance));
    }

    juce::MidiBuffer midi;
    for (int k = 0; k < 20; ++k)
        for (auto& p : plugins)
        {
            juce::AudioBuffer<float> buffer (std::max (2, std::max (p->getTotalNumInputChannels(), p->getTotalNumOutputChannels())), blockSize);
            buffer.clear();
            p->processBlock (buffer, midi);
        }

    // This process maps the same named block the plugins mapped (the name carries the process id).
    auto* block = lab::host::bus::mapSharedBlock();
    if (block == nullptr)
    {
        std::cout << "bus_peers_fresh=0 bus_ok=0 (no shared block)" << std::endl;
        return 1;
    }
    const auto now = juce::Time::currentTimeMillis();
    std::cout << "block initialised=" << std::atomic_ref<uint32_t> (block->initialised).load() << " magic=" << std::hex << block->magicValue
              << std::dec << " version=" << block->version << std::endl;
    for (int i = 0; i < lab::host::bus::maxPeers; ++i)
    {
        auto& p = block->peers[i];
        const auto id = std::atomic_ref<uint32_t> (p.id).load();
        if (id != 0)
            std::cout << "slot " << i << " id=" << std::hex << id << std::dec << " heartbeat_age_ms=" << (now - std::atomic_ref<int64_t> (p.heartbeatMs).load()) << std::endl;
    }
    int fresh = 0;
    for (auto& p : block->peers)
    {
        const auto id = std::atomic_ref<uint32_t> (p.id).load();
        const auto beat = std::atomic_ref<int64_t> (p.heartbeatMs).load();
        if (id == 0 || beat <= 0 || now - beat > lab::host::bus::staleMs)
            continue;
        ++fresh;
        const auto code = std::atomic_ref<uint32_t> (p.code).load();
        char codeText[5] = { (char) (code & 0xff), (char) ((code >> 8) & 0xff), (char) ((code >> 16) & 0xff), (char) (code >> 24), 0 };
        std::cout << "peer code=" << codeText << " callsign=" << lab::host::bus::unpackCallsign (std::atomic_ref<uint64_t> (p.callsign).load()) << std::endl;
    }
    const bool ok = fresh == (int) plugins.size();
    std::cout << "bus_peers_fresh=" << fresh << " plugins=" << plugins.size() << " bus_ok=" << (ok ? 1 : 0) << std::endl;
    for (auto& p : plugins)
        p->releaseResources();
    plugins.clear();
    return ok ? 0 : 1;
}
