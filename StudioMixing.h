// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <JuceHeader.h>
#include <array>
#include <functional>
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
    std::function<void(tracktion::EditItemID, bool solo)> toggleTrackState;
    std::function<void(tracktion::EditItemID)> toggleArm;
    std::function<void(tracktion::EditItemID)> toggleMonitoring;
};

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
    void refresh (tracktion::AudioTrack*, bool ready, bool canRoute, const juce::String& recovery);
    int minimumContentHeight (int width) const;
    void appearanceChanged();
    void resized() override;
    void paint (juce::Graphics&) override;

private:
    TrackActions& actions;
    tracktion::Edit* edit = nullptr;
    tracktion::EditItemID id;
    tracktion::AudioTrack* track() const;
    juce::Label identity, inputSummary, instrument, sends, output, ariaStatus;
    juce::TextButton audioInput { "Audio Input..." }, midiInput { "MIDI Input..." }, arm { "Arm" },
                     monitor { "Monitor" }, editor { "Open Instrument" }, inserts { "Inserts..." },
                     routing { "Output..." };
    MixControls mix { false };
    std::array<juce::Rectangle<int>, 5> columns;
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
