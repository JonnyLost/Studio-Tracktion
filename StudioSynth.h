// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <JuceHeader.h>
#include "SynthEngine.h"
#include <mutex>

namespace studio::synth
{
struct Parameter
{
    juce::String name;
    float minimum, maximum, initial;
    bool logarithmic;
};
struct FactorySound
{
    juce::String id, name, category, summary;
    Family family;
    juce::var patch;
};
struct Catalog
{
    std::array<std::vector<Parameter>, 6> parameters;
    std::vector<FactorySound> sounds;
};
const Catalog& catalog();
juce::String familyKey (Family);
juce::String familyName (Family);
juce::Identifier patchKey (Family);
Family parseFamily (const juce::String&);
juce::Result makePlan (const juce::var& patch, const juce::File& base, Patch&, juce::String& warning);

class Plugin final : public tracktion::Plugin, private juce::Timer
{
public:
    static constexpr auto xmlTypeName = "studioNativeSynth";
    explicit Plugin (const tracktion::PluginCreationInfo&);
    ~Plugin() override;
    juce::String getName() const override { return "Studio Synth"; }
    juce::String getVendor() override { return "Studio"; }
    juce::String getPluginType() override { return xmlTypeName; }
    juce::String getSelectableDescription() override { return getName(); }
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

    juce::Result selectFactory (const FactorySound&);
    juce::Result loadPreset (const juce::File&);
    juce::Result savePreset (const juce::File&);
    juce::Result editPatch (juce::var changed, bool rebuild);
    const juce::var& getPatch() const;
    juce::String presetName() const;
    juce::String factoryID() const;
    juce::String warning() const { return problem; }
    unsigned changeCount() const { return revision; }
    bool dirty() const;
    bool isUserPreset() const;
    void preview (int key, int velocity);
    bool noteActive (int note) const noexcept;

private:
    struct Bank
    {
        Patch patch;
        std::shared_ptr<Engine> engine;
        double rate = 0;
        std::shared_ptr<const tracktion::tempo::Sequence> tempo;
    };
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void restore();
    juce::Result commit (juce::var, bool rebuild, const juce::File& base);
    juce::Result build (const juce::var&, bool rebuild, const juce::File&, std::unique_ptr<Bank>&, juce::String& warning);
    void publish (std::unique_ptr<Bank>);
    void timerCallback() override;
    juce::File projectDirectory() const;

    juce::var document;
    juce::String problem;
    unsigned revision = 0;
    bool writing = false;
    std::mutex publicationMutex; // Only graph preparation and the message thread take this lock.
    std::unique_ptr<const Bank> current, retired;
    std::atomic<const Bank*> published { nullptr }, borrowed { nullptr };
    std::atomic<double> rate { 44100 };
    std::atomic<bool> resetPending { false };
    bool wasPlaying = false;
    tracktion::AutomatableParameter::Ptr outputTrim;
    tracktion::AutomatableParameter::Ptr macroOffset;
    std::array<std::atomic<std::uint64_t>, 2> noteBits {};
    std::array<int, 128> previewQueue {};
    std::atomic<unsigned> sent { 0 }, received { 0 };
    std::atomic<bool> overflow { false };
    juce::CachedValue<float> trimValue, macroValue;
};

void registerPlugin (tracktion::Engine&);
int runSynthIntegrationTest (const juce::File& screenshots);
#if STUDIO_SPATIAL_TESTS
bool verifyPresetBrowser (tracktion::Plugin::EditorComponent&, const juce::File& userPreset);
#endif
}
