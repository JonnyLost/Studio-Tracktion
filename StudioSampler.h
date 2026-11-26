// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <JuceHeader.h>
#include "SamplerEngine.h"
#include "StudioResources.h"
#include "ArticulationMappings.h"

namespace studio::sampler
{
struct SampleZone
{
    juce::File file;
    Zone mapping;
    juce::String problem;
    int folder = 0; // Organizational group, independent of legacy round-robin IDs.
};

struct InstrumentGroup
{
    int id = 0; juce::String name { "Main" };
    Group playback;
};

// Extracted WAV files are staged in a unique directory. The import review
// owns this object; cancellation removes only its own uncommitted files.
// Accepted media stays on disk for undo, presets and project reopen.
struct BankImportMedia
{
    juce::File directory, parent; // optional caller-selected library root
    bool keep = false;
    ~BankImportMedia() { if (! keep && directory.isDirectory()) directory.deleteRecursively(); }
};
struct BankProgram
{
    juce::String name;
    int bank = 0, number = 0;
    bool drums = false;
};

struct Instrument
{
    juce::String name { "Studio Sampler" }, preset;
    bool dirty = false;
    Settings settings;
    std::vector<SampleZone> zones;
    std::vector<InstrumentGroup> groups { {} };
};

struct GuidedBuild
{
    enum class Layout { pitched, percussion };
    enum class Playback { held, oneShot, embeddedLoop };
    juce::String name;
    Layout layout = Layout::pitched;
    Playback playback = Playback::held;
    int firstNote = 60; // explicit root for a single unknown recording, or first drum key
    bool spread = true, roundRobin = false;
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
    bool sendArticulation (const studio::ArticulationMapping&);
    int activeKeyswitch (int channel) const { return renderer.activeKeyswitch (channel); }
    bool noteActive (int note) const { return renderer.noteActive (note); }
    void preview (int source, int channel, int note, int velocity, int zone = -1, int first = -1, int end = -1);
    void previewRange (int zone, int first, int end);
    juce::Result loadSample (const juce::File&, int replace = -1);
    void removeZone (int);
    void duplicateZone (int);
    void editGroups (std::vector<InstrumentGroup>);
    std::vector<studio::ArticulationMapping> articulations() const;
    static juce::Result prepareGuided(const GuidedBuild&, const juce::Array<juce::File>&, Instrument&, juce::String&,
                                      std::atomic<bool>* cancelled = nullptr, std::atomic<int>* progress = nullptr);
    static juce::Result prepareImport (Instrument initial, const juce::Array<juce::File>& paths, bool automatic, Instrument&, juce::String& summary, std::atomic<bool>* cancelled = nullptr, std::atomic<int>* progress = nullptr);
    static juce::Result prepareSfz (const juce::File&, const Instrument& cache, Instrument&, juce::String& report,
                                   std::atomic<bool>* cancelled = nullptr, std::atomic<int>* progress = nullptr);
    static juce::Result prepareExs (const juce::File&, const juce::File& sampleRoot, const Instrument& cache,
                                   Instrument&, juce::String& report, std::atomic<bool>* cancelled = nullptr,
                                   std::atomic<int>* progress = nullptr);
    static juce::Result prepareXmlInstrument (const juce::File&, const Instrument& cache, Instrument&,
        juce::String& report, BankImportMedia&, std::atomic<bool>* cancelled = nullptr, std::atomic<int>* progress = nullptr);
    static juce::Result catalogSoundBank (const juce::File&, std::vector<BankProgram>&,
                                          std::atomic<bool>* cancelled = nullptr);
    static juce::Result prepareSoundBank (const juce::File&, int programIndex, Instrument&, juce::String& report,
                                          BankImportMedia&, std::atomic<bool>* cancelled = nullptr,
                                          std::atomic<int>* progress = nullptr);
    void moveZone (int, int folder);
    juce::Result proposeImport (const juce::Array<juce::File>&, bool automatic, Instrument&, juce::String& summary);
    void commitImport (Instrument next, bool dirty = true) { change (std::move (next), dirty); }
    static juce::Result prepareSample (Instrument initial, const juce::File&, int replace, Instrument&, std::atomic<bool>* cancelled = nullptr);
    static juce::Result preparePreset (const juce::File&, const Instrument& cache, Instrument&, std::atomic<bool>* cancelled = nullptr);
    void undoMapping (bool redo);
    void editZone (int, const Zone&);
    juce::Result editZones (const std::vector<std::pair<int, Zone>>&);
    void editSettings (const Settings&);
    void rename (juce::String);
    static juce::Result writeConvertedPreset (const Instrument&, const juce::File&);
    juce::Result savePreset (const juce::File&);
    juce::Result loadPreset (const juce::File&);
    InstrumentDiagnosticSnapshot resourceSnapshot() const override;
    juce::Result verifyStreamSources (bool checkOfflineErrors) const;

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
    std::uint64_t sampleRevision = 0, switchRevision = 0;
    bool writing = false;
    std::size_t loadedBytes = 0;
    unsigned loadedSamples = 0;

    Engine renderer;
    double rate = 44100;
    bool wasPlaying = false;
    std::atomic<bool> needsReset { false };

    // One render reader, one message-thread writer. Retired banks are freed
    // only by the writer, after the reader has stopped borrowing them.
    std::unique_ptr<const Bank> current;
    std::unique_ptr<const Bank> retired;
    std::atomic<const Bank*> published { nullptr }, borrowed { nullptr };

    struct Preview { int source, channel, note, velocity, zone; std::uint64_t revision; int first = -1, end = -1, status = 0; std::uint64_t bindingRevision = 0; };
    static constexpr unsigned queueSize = 128;
    std::array<Preview, queueSize> queue {};
    std::atomic<unsigned> writeIndex { 0 }, readIndex { 0 };
    std::atomic<bool> previewOverflow { false };
};

void registerPlugin (tracktion::Engine&);
}
