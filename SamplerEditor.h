// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "StudioSampler.h"
#include "StudioUI.h"

namespace studio::sampler
{
#if STUDIO_SPATIAL_TESTS
int runSamplerIntegrationTest (const juce::File& screenshots);
#endif

class Editor final : public tracktion::Plugin::EditorComponent,
                     private juce::Timer,
                     private juce::MidiKeyboardState::Listener
{
public:
    explicit Editor (Plugin&);
    ~Editor() override;
    bool allowWindowResizing() override { return true; }
    bool isEditorResizable() override { return true; }
    juce::ComponentBoundsConstrainer* getBoundsConstrainer() override { return &limits; }
    void resized() override;
    void paint (juce::Graphics&) override;
    void appearanceChanged();

#if STUDIO_SPATIAL_TESTS
    bool runReview (const juce::File& first, const juce::File& second,
                    const juce::File& output, const std::function<bool()>& auditionCheck);
#endif

private:
    enum class Page { basic, mapping, loops, advanced };
    struct Control
    {
        juce::Label label;
        juce::Slider slider;
    };

    class Display final : public juce::Component, public juce::FileDragAndDropTarget
    {
    public:
        explicit Display (Editor&);
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        bool isInterestedInFileDrag (const juce::StringArray&) override;
        void fileDragEnter (const juce::StringArray&, int, int) override;
        void fileDragExit (const juce::StringArray&) override;
        void filesDropped (const juce::StringArray&, int, int) override;
    private:
        Editor& owner;
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
    Page page = Page::basic;
    int selection = 0, auditionNote = -1;
    unsigned seen = 0;
    bool updating = false;
    bool clearAudition = false;

    juce::Viewport viewport;
    juce::Component body;
    Display display { *this };
    juce::Component envelope;
    juce::MidiKeyboardState keys;
    Keyboard keyboard { *this, keys };
    juce::Label name, preset, filename, status, meter;
    juce::ComboBox samples, mode;
    juce::ToggleButton loopEnabled { "Enable loop" };
    juce::TextButton loadPreset { "Load Preset" }, savePreset { "Save Preset" };
    juce::TextButton basic { "Basic" }, mapping { "Mapping" }, loops { "Loops" }, advanced { "Advanced" };
    juce::TextButton load { "Load Sample" }, locate { "Locate Sample" }, remove { "Remove Zone" }, audition { "Audition" };
    Control attack, decay, sustain, release, gain, tune, root, low, high, soft, hard, zoneGain, cents,
            loopStart, loopEnd, voices, bend, group;
    juce::OwnedArray<juce::Component> owned;
    std::unique_ptr<juce::FileChooser> chooser;

    const SampleZone* zone() const;
    void setPage (Page);
    void refresh();
    void choose (bool presetFile, bool save, bool replace);
    void import (const juce::File&, int replace);
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
