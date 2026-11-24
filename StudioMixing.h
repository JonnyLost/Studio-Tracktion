// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <JuceHeader.h>
#include <array>
#include <functional>
#include <memory>
#include <vector>

namespace studio
{
enum class TrackMenu { audioInput, midiInput, inserts, output };

struct TrackActions
{
    std::function<bool()> canChangeMix;
    std::function<void(tracktion::EditItemID)> selectTrack;
    std::function<void(tracktion::EditItemID, TrackMenu, juce::Component&)> showMenu;
    std::function<void(tracktion::EditItemID)> openInstrument;
    std::function<void(tracktion::EditItemID)> openFullInstrument, openArticulations;
    std::function<void(tracktion::EditItemID, bool solo)> toggleTrackState;
    std::function<void(tracktion::EditItemID)> toggleArm;
    std::function<void(tracktion::EditItemID)> toggleMonitoring;
    std::function<void(tracktion::EditItemID)> showStage;
    std::function<void(tracktion::EditItemID, juce::Component&)> editTrackColour;
    std::function<void(tracktion::EditItemID, juce::Component&)> editTrackIcon;
};

bool isInstrumentPlugin (tracktion::Plugin&);
tracktion::Plugin* findInstrument (tracktion::AudioTrack&);
juce::String outputLabel (tracktion::AudioTrack&);

class MixControls final : public juce::Component
{
public:
    explicit MixControls (bool vertical);
    std::function<tracktion::VolumeAndPanPlugin::Ptr()> resolveVolume;
    void refresh (tracktion::VolumeAndPanPlugin*, bool enabled);
    void resized() override;

private:
    juce::Slider volume, pan;
    juce::Label volumeLabel, panLabel;
    bool vertical;
};

class ChannelInspector final : public juce::Component
{
public:
    explicit ChannelInspector (TrackActions&);
    void setEdit (tracktion::Edit*);
    void refresh (tracktion::AudioTrack*, bool ready, bool canRoute, const juce::String& recovery,
                  const tracktion::MidiClip* selectedClip = nullptr);
    int minimumContentHeight (int width) const;
    void setShowMixControls (bool show)
    {
        showMixControls = show;
        mix.setVisible (show);
        resized();
    }
    tracktion::EditItemID inspectedTrack() const { return id; }
    void appearanceChanged();
    void resized() override;
    void paint (juce::Graphics&) override;

private:
    TrackActions& actions;
    tracktion::Edit* edit = nullptr;
    tracktion::EditItemID id;
    tracktion::AudioTrack* track() const;
    juce::Label identity, inputSummary, instrument, sends, output, ariaStatus, midiStatus;
    juce::TextButton audioInput { "Audio Input..." }, midiInput { "MIDI Input..." }, arm { "Arm" },
                     monitor { "Monitor" }, editor { "Open Instrument" }, inserts { "Inserts..." },
                     routing { "Output..." }, stage { "Open Stage" };
    MixControls mix { false };
    bool showMixControls = true;
    std::array<juce::Rectangle<int>, 5> columns;
};

class InspectorChannelStrip;
class AtelierInspectorPanel;

// A lightweight, non-modal right-side drawer. It reuses the existing
// ChannelInspector controls and selected-track model, avoiding a second set
// of audio/MIDI routing implementations.
class InspectorDrawer final : public juce::Component
{
public:
    explicit InspectorDrawer (TrackActions&);
    ~InspectorDrawer() override;
    std::function<void()> onClose;
    std::function<void(int)> onWidthChanged;
    void setEdit (tracktion::Edit*);
    void refresh (tracktion::AudioTrack*, bool ready, bool canRoute, const juce::String& recovery,
                  const tracktion::MidiClip* selectedClip = nullptr);
    void appearanceChanged();
    tracktion::EditItemID inspectedTrack() const;
    void setDetailsExpanded (bool expanded);
    bool areDetailsExpanded() const noexcept { return detailsOpen; }
    bool isShowingAtelier() const noexcept { return atelierMode; }
    void suspendAtelier();
   #if STUDIO_SPATIAL_TESTS
    bool verifyAtelierInspector();
   #endif
    void resized() override;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    juce::MouseCursor getMouseCursor() override;

private:
    juce::ComponentDragger resizeDragger; // Drag grip uses a width callback, not component movement.
    int resizeStartX = 0, resizeStartWidth = 0;
    bool resizingWidth = false;
    TrackActions& actions;
    ChannelInspector inspector;
    std::unique_ptr<InspectorChannelStrip> channelStrip;
    std::unique_ptr<AtelierInspectorPanel> atelier;
    juce::Viewport scroll, atelierScroll;
    juce::Label title;
    juce::TextButton close { "Close" }, details { "Track Settings" }, quickFx { "FX..." };
    bool detailsOpen = false, atelierMode = false;
};

class StudioMixer final : public juce::Component
{
public:
    explicit StudioMixer (TrackActions&);
    ~StudioMixer() override;
    void pauseMeters();
    int minimumContentHeight() const;
    void appearanceChanged();
    void setEdit (tracktion::Edit*);
    void refresh (const juce::Array<tracktion::AudioTrack*>&, tracktion::AudioTrack* selected, bool ready, bool canRoute);
    void resized() override;
    void paint (juce::Graphics&) override;

private:
    class Strip;
    TrackActions& actions;
    tracktion::Edit* edit = nullptr;
    juce::Component stripBank;
    juce::Viewport viewport;
    std::vector<std::unique_ptr<Strip>> strips;
    std::unique_ptr<Strip> master;
    tracktion::EditItemID lastSelection;
};
}
