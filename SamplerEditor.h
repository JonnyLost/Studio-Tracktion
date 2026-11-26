// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "StudioSampler.h"
#include "InstrumentWorkspace.h"
#include <set>
#include <thread>

namespace studio::sampler
{
#if STUDIO_SPATIAL_TESTS
int runSamplerIntegrationTest (const juce::File& screenshots);
#endif

class Editor final : public NativeInstrumentEditor,
                     private juce::Timer,
                     private juce::MidiKeyboardState::Listener,
                     private juce::ListBoxModel
{
public:
    explicit Editor (Plugin&);
    ~Editor() override;
    bool allowWindowResizing() override { return true; }
    juce::ComponentBoundsConstrainer* getBoundsConstrainer() override { return &limits; }
    void resized() override;
    void paint (juce::Graphics&) override;
    void appearanceChanged() override;
    int minimumDockHeight() const override;
    juce::String viewState() const override;
    void restoreView (const juce::String&) override;

#if STUDIO_SPATIAL_TESTS
    bool runReview (const juce::File& first, const juce::File& second,
                    const juce::File& output, const std::function<bool()>& auditionCheck);
#endif

private:
    // Preserve saved numeric view IDs 0–4; new instruments open in Play.
    enum class Page { basic, mapping, loops, advanced, groups, perform };
    struct Control
    {
        juce::Label label;
        juce::Slider slider;
    };

    class Display final : public juce::Component, public juce::FileDragAndDropTarget,
                          private juce::ChangeListener
    {
    public:
        explicit Display (Editor&);
        ~Display() override;
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        bool isInterestedInFileDrag (const juce::StringArray&) override;
        void fileDragEnter (const juce::StringArray&, int, int) override;
        void fileDragExit (const juce::StringArray&) override;
        void filesDropped (const juce::StringArray&, int, int) override;
    private:
        struct StreamThumbnail
        {
            juce::AudioFormatManager formats;
            juce::AudioThumbnailCache cache { 1 };
            juce::AudioThumbnail thumbnail;
            StreamThumbnail (int resolution) : thumbnail (resolution, formats, cache)
            {
                formats.registerFormat (new juce::WavAudioFormat(), true);
                formats.registerFormat (new juce::AiffAudioFormat(), false);
               #if JUCE_USE_FLAC
                formats.registerFormat (new juce::FlacAudioFormat(), false);
               #endif
               #if JUCE_MAC
                formats.registerFormat (new juce::CoreAudioFormat (juce::CoreAudioFormat::StreamKind::kCaf), false);
               #endif
            }
        };
        void changeListenerCallback (juce::ChangeBroadcaster*) override { repaint(); }
        void clearThumbnail();
        Editor& owner;
        std::unique_ptr<StreamThumbnail> streamThumbnail;
        juce::File thumbnailFile;
        std::int64_t thumbnailTime = 0, thumbnailSize = -1;
        int marker = 0;
        unsigned dragRevision = 0;
        int dragSelection = 0;
        Zone dragZone;
        bool draggingFiles = false;
        juce::Rectangle<int> plot() const;
        juce::Rectangle<float> zoneBounds (const Zone&) const;
    };

    class Keyboard final : public juce::MidiKeyboardComponent
    {
    public:
        Keyboard (Editor&, juce::MidiKeyboardState&);
        void drawWhiteNote (int, juce::Graphics&, juce::Rectangle<float>, bool, bool,
                            juce::Colour, juce::Colour) override;
        void drawBlackNote (int, juce::Graphics&, juce::Rectangle<float>, bool, bool, juce::Colour) override;
    private:
        Editor& owner;
        void drawRange (int, juce::Graphics&, juce::Rectangle<float>);
    };

    Plugin& plugin;
    StudioLookAndFeel look;
    juce::ComponentBoundsConstrainer limits;
    Page page = Page::perform, lastBuildPage = Page::mapping;
    int selection = 0, auditionNote = -1;
    unsigned seen = 0;
    bool updating = false;
    bool clearAudition = false;
    std::set<int> selectedZones;
    double waveZoom = 1, waveOffset = 0, mapScale = 1, mapOffset = 0;
    int mapSpan() const { return juce::roundToInt (128 / mapScale); }
    int mapFirst() const { return juce::roundToInt ((128 - mapSpan()) * mapOffset); }
    struct ImportTask
    {
        std::atomic<bool> cancelled { false }, done { false };
        std::atomic<int> progress { 0 };
        Instrument proposal; juce::String summary, error; unsigned revision = 0; int first = 0;
        int replace = -1, bankProgram = -1; bool preset = false, sfz = false, soundBank = false, exs = false, xmlInstrument = false;
        juce::File bankFile;
        juce::int64 bankSize = -1, bankTime = 0;
        std::vector<BankProgram> catalog;
        std::shared_ptr<BankImportMedia> bankMedia;
        std::thread worker;
        ~ImportTask() { cancelled.store (true); if (worker.joinable()) worker.join(); }
    };
    std::unique_ptr<ImportTask> importTask;
    void startImport (juce::Array<juce::File>, bool, int replace = -1, bool preset = false,
                      int bankProgram = -1, juce::int64 expectedSize = -1, juce::int64 expectedTime = 0,
                      juce::File exsRoot = {}, bool exsRootChosen = false);
    void chooseExsRoot (const juce::File&);
    void showBankCatalog (const ImportTask&);
    juce::Component::SafePointer<juce::DialogWindow> importReview;
    void finishImport();
    void editGroup (const std::function<void(Group&)>&);
    void batchEdit (const std::function<void(Zone&, double)>&, double);
    juce::ListBox groupRows { "Instrument groups", this };
    int getNumRows() override;
    void paintListBoxItem (int, juce::Graphics&, int, int, bool) override;
    void selectedRowsChanged (int) override;

    juce::Viewport viewport;
    juce::Component body;
    Display display { *this };
    juce::Component envelope;
    juce::MidiKeyboardState keys;
    Keyboard keyboard { *this, keys };
    juce::Label name, preset, filename, status, meter;
    juce::ComboBox samples, mode, groupList, triggerMode, triggerChannel;
    juce::Label triggerLabel, channelLabel;
    juce::ToggleButton groupMute { "Mute" }, groupSolo { "Solo" }, releaseTrigger { "On note release" };
    juce::TextButton editMapping { "Edit Mapping" }, cancelImport { "Cancel import" }, playView { "Play view" };
    juce::Label groupName;
    juce::TextButton groups { "Groups" }, addGroup { "New group" }, copyGroup { "Copy group" }, deleteGroup { "Delete group" }, moveGroup { "Move zone here" }, duplicate { "Duplicate zone" }, undo { "Undo" }, redo { "Redo" };
    juce::ToggleButton loopEnabled { "Enable loop" };
    juce::TextButton loadPreset { "Load" }, savePreset { "Save" };
    juce::TextButton play { "Play" }, basic { "Sound" }, build { "Build" }, mapping { "Mapping" }, loops { "Waveform" }, advanced { "Advanced" };
    juce::Label playSummary, playHelp, articulationLabel, soundScope;
    juce::ComboBox articulationChoice;
    juce::TextButton applyArticulation { "Select articulation" };
    std::vector<studio::ArticulationMapping> playableArticulations;
    void refreshPerformance();
    juce::TextButton load { "Load Sample" }, locate { "Locate Sample" }, remove { "Remove Zone" }, audition { "Audition" };
    Control attack, decay, sustain, release, gain, tune, root, low, high, soft, hard, zoneGain, cents,
            mapZoom, mapPosition, keySwitch, ccNumber, ccLow, ccHigh, fadeIn, fadeOut, loopFade, zoom, position, sampleStart, sampleEnd, loopStart, loopEnd, voices, bend, group;
    juce::OwnedArray<juce::Component> owned;
    std::unique_ptr<juce::FileChooser> chooser;

    const SampleZone* zone() const;
    void setPage (Page);
    void refresh();
    void choose (bool presetFile, bool save, bool replace);
    void import (const juce::File&, int replace);
    void importFiles (const juce::StringArray&);
    void selectZone (int);
    int initialZone() const;
    void setStatus (const juce::String&);
    void playAudition();
    void handleNoteOn (juce::MidiKeyboardState*, int, int, float) override;
    void handleNoteOff (juce::MidiKeyboardState*, int, int, float) override;
    void timerCallback() override;
    void setup (Control&, juce::String, double, double, double, juce::String,
                std::function<void(double)>, bool dial = false);
};
}
