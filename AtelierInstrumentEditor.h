// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "InstrumentWorkspace.h"
#include "ArticulationMappings.h"
#include <array>
#include <vector>

namespace studio
{
// A capability-driven, non-destructive presentation for hosted Audio Unit
// instruments. It shares the existing Edit, track fader and plug-in instance;
// it does not duplicate an instrument, its audio graph or its MIDI routing.
class AtelierInstrumentEditor final : public NativeInstrumentEditor, private juce::Timer
{
public:
    AtelierInstrumentEditor (tracktion::Plugin&, tracktion::AudioTrack&);
    ~AtelierInstrumentEditor() override;

    bool allowWindowResizing() override { return true; }
    juce::ComponentBoundsConstrainer* getBoundsConstrainer() override { return &limits; }
    void appearanceChanged() override;
    int minimumDockHeight() const override;
    juce::String viewState() const override;
    void restoreView (const juce::String&) override;
    void resized() override;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void focusLost (FocusChangeType) override;

    std::function<void (tracktion::EditItemID)> onArticulations, onStage;
    std::function<void()> onFullView;
    std::function<bool()> isFloatingWindow;

   #if STUDIO_SPATIAL_TESTS
    bool verifyBindings();
    bool essentialControlsFit() const;
    bool reviewPage (int requested, bool shouldBeFull);
    bool reviewSelectMappedArticulation();
    bool reviewStageEditing();
    bool reviewKeyboardHitMap() const;
    bool reviewMIDIControllers();
   #endif

private:
    void timerCallback() override;
    tracktion::AudioTrack* track() const;
    tracktion::VolumeAndPanPlugin* volumePlugin() const;
    void refreshControls();
    void loadPortrait();
    void changePortrait();
    void syncInspectorControls();
    void refreshArticulations();
    void selectArticulation (int);
    void editStage (int field); // Pan, depth, send trim on an existing verified seat.
    void setPage (int);
    void paintKeyboardReference (juce::Graphics&) const;
    int keyboardNoteAt (juce::Point<int>) const;
    bool canPreviewKeyboard() const;
    void previewKeyboardNote (int);
    void stopKeyboardNote();
    void sendLiveCC (int slot);
    void sendAllLiveCC();
    void releaseSustain();
    void updateMIDIControls();
    void updatePageVisibility();
    void layoutSliders (juce::Rectangle<int> area, bool horizontal);
    void drawAtelierSurface (juce::Graphics&, juce::Rectangle<int>, const juce::String&) const;

    tracktion::Plugin::Ptr instrument;
    tracktion::EditItemID trackId;
    tracktion::AutomatableParameter::Ptr expressionParameter;
    juce::Image blueHour, portraitImage;
    juce::String portraitName;
    juce::ComboBox portraitChoice, previewChannel;
    juce::ComponentBoundsConstrainer limits;
    std::unique_ptr<juce::LookAndFeel_V4> atelierStyle;
    juce::Label title, subtitle, mixHeading, volumeLabel, panLabel, expressionHeading, hint, sceneCaption;
    juce::Label stageHeading, stageSummary, routingSummary, mappingSummary, keyboardSummary;
    juce::Label stagePanLabel, stageDepthLabel, sendTrimLabel;
    juce::Label ccStatus;
    juce::TextButton original { "Open Original Instrument" },
                     articulation { "Articulation Mappings..." }, stage { "Open Orchestral Stage..." },
                     mainTab { "Main" }, articulationTab { "Articulations" },
                     mixerTab { "Mixer" }, advancedTab { "Advanced" },
                     ccTab { "MIDI CC" }, fullView { "Full View" }, sendAllCC { "Send values" };
    juce::Slider volume, pan, expression, stagePan, stageDepth, sendTrim;
    std::array<juce::TextButton, 8> mappingPads;
    std::array<juce::Slider, 8> ccKnobs;
    std::array<juce::Label, 8> ccLabels;
    std::vector<ArticulationMapping> currentMappings;
    juce::String activeMappingId, mappingStamp, sharedCCStamp;
    juce::Rectangle<int> portraitBounds, controlBounds, stageBounds, mappingBounds, keyboardBounds, instrumentHeaderBounds;
    juce::Rectangle<int> mixerBounds, routingBounds, controllerBounds;
    int page = 0; // Main, Articulations, Mixer, Advanced, MIDI CC.
    int heldPreviewNote = -1, heldPreviewChannel = 1, heldKeyswitchNote = -1;
    int activeSustainChannel = -1; // -1: no CC64-on sent by Atelier.
    bool fullPresentation = false, restoring = false;
};
}
