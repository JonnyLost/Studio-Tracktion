// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "InstrumentWorkspace.h"

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

    tracktion::Plugin::Ptr instrument;
    tracktion::EditItemID trackId;
    tracktion::AutomatableParameter::Ptr expressionParameter;
    juce::Image blueHour, portraitImage;
    juce::String portraitName;
    juce::ComboBox portraitChoice;
    juce::ComponentBoundsConstrainer limits;
    juce::Label title, subtitle, mixHeading, volumeLabel, panLabel, expressionHeading, hint, sceneCaption;
    juce::TextButton original { "Open Original Instrument" },
                     articulation { "Articulation Mappings..." }, stage { "Open Orchestral Stage..." };
    juce::Slider volume, pan, expression;
    juce::Rectangle<int> portraitBounds, controlBounds;
    bool restoring = false;
};
}
