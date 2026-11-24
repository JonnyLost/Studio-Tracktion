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

    std::function<void (tracktion::EditItemID)> onArticulations, onStage;
    std::function<void()> onFullView;

   #if STUDIO_SPATIAL_TESTS
    bool verifyBindings();
    bool essentialControlsFit() const;
   #endif

private:
    void timerCallback() override;
    tracktion::AudioTrack* track() const;
    tracktion::VolumeAndPanPlugin* volumePlugin() const;
    void refreshControls();
    void loadPortrait();
    void changePortrait();
    void refreshArticulations();
    void selectArticulation (int);
    void setPage (int);
    void paintKeyboardReference (juce::Graphics&) const;
    void updatePageVisibility();
    void layoutSliders (juce::Rectangle<int> area, bool horizontal);
    void drawAtelierSurface (juce::Graphics&, juce::Rectangle<int>, const juce::String&) const;

    tracktion::Plugin::Ptr instrument;
    tracktion::EditItemID trackId;
    tracktion::AutomatableParameter::Ptr expressionParameter;
    juce::Image blueHour, portraitImage;
    juce::String portraitName;
    juce::ComboBox portraitChoice;
    juce::ComponentBoundsConstrainer limits;
    std::unique_ptr<juce::LookAndFeel_V4> atelierStyle;
    juce::Label title, subtitle, mixHeading, volumeLabel, panLabel, expressionHeading, hint, sceneCaption;
    juce::Label stageHeading, stageSummary, routingSummary, mappingSummary, keyboardSummary;
    juce::TextButton original { "Open Original Instrument" },
                     articulation { "Articulation Mappings..." }, stage { "Open Orchestral Stage..." },
                     mainTab { "Main" }, articulationTab { "Articulations" },
                     mixerTab { "Mixer" }, advancedTab { "Advanced" }, fullView { "Full View" };
    juce::Slider volume, pan, expression;
    std::array<juce::TextButton, 8> mappingPads;
    std::vector<ArticulationMapping> currentMappings;
    juce::String activeMappingId, mappingStamp;
    juce::Rectangle<int> portraitBounds, controlBounds, stageBounds, mappingBounds, keyboardBounds, instrumentHeaderBounds;
    juce::Rectangle<int> mixerBounds, routingBounds;
    int page = 0; // Main, Articulations, Mixer, Advanced. Saved per instrument.
    bool fullPresentation = false, restoring = false;
};
}
