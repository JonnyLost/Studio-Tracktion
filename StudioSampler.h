// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <JuceHeader.h>
#include "SamplerEngine.h"
#include "StudioResources.h"

namespace studio::sampler
{
struct SampleZone
{
    juce::File file;
    Zone mapping;
    juce::String problem;
};

struct Instrument
{
    juce::String name { "Studio Sampler" }, preset;
    bool dirty = false;
    Settings settings;
    std::vector<SampleZone> zones;
};

class Plugin final : public tracktion::Plugin, public InstrumentDiagnostics, private juce::Timer
{
public:
    static constexpr auto xmlTypeName = "studioNativeSampler";
    explicit Plugin (const tracktion::PluginCreationInfo&);
    ~Plugin() override;

    juce::String getName() const override { return "Studio Sampler"; }
    juce::String getVendor() override { return "Studio"; }
    juce::String getPluginType() override { return xmlTypeName; }
    juce::String getSelectableDescription() override { return "Studio Sampler"; }
    bool isSynth() override { return true; }
    bool takesMidiInput() override { return true; }
    bool producesAudioWhenNoAudioInput() override { return true; }
    bool noTail() override { return false; }
    bool canBeAddedToClip() override { return false; }
    bool canBeAddedToRack() override { return false; }
    int getNumOutputChannelsGivenInputs (int n) override { return juce::jmax (2, n); }
    BusLayout getBusses() const override;
    void initialise (const tracktion::PluginInitialisationInfo&) override;
    void deinitialise() override;
    void reset() override;
    void applyToBuffer (const tracktion::PluginRenderContext&) override;
    std::unique_ptr<EditorComponent> createEditor() override;

    const Instrument& instrument() const { return model; }
    unsigned changeCount() const { return modelRevision; }
    juce::String stateError() const { return restoreError; }
    bool noteActive (int note) const { return renderer.noteActive (note); }
    void preview (int source, int channel, int note, int velocity);
    juce::Result loadSample (const juce::File&, int replace = -1);
    void removeZone (int);
    void editZone (int, const Zone&);
    void editSettings (const Settings&);
    void rename (juce::String);
    juce::Result savePreset (const juce::File&);
    juce::Result loadPreset (const juce::File&);
    InstrumentDiagnosticSnapshot resourceSnapshot() const override;

private:
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void loadState();
    void change (Instrument, bool markDirty);
    void publish (Instrument);
    void timerCallback() override;
    juce::File projectDirectory() const;

    Instrument model;
    juce::String restoreError;
    unsigned modelRevision = 0;
    std::uint64_t sampleRevision = 0;
    bool writing = false;

    Engine renderer;
    double rate = 44100;
    bool wasPlaying = false;
    std::atomic<bool> needsReset { false };

    // One render reader, one message-thread writer. Retired banks are freed
    // only by the writer, after the reader has stopped borrowing them.
    std::unique_ptr<const Bank> current;
    std::unique_ptr<const Bank> retired;
    std::atomic<const Bank*> published { nullptr }, borrowed { nullptr };

    struct Preview { int source, channel, note, velocity; };
    static constexpr unsigned queueSize = 128;
    std::array<Preview, queueSize> queue {};
    std::atomic<unsigned> writeIndex { 0 }, readIndex { 0 };
    std::atomic<bool> previewOverflow { false };
};

void registerPlugin (tracktion::Engine&);
}
