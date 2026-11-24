// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "StudioMixing.h"
#include "ArticulationMappings.h"
#include <array>
#include <vector>

namespace studio
{
// Single selected-track performance surface. It never hosts, duplicates, or
// reinitialises the Audio Unit; all controls target the existing track.
class AtelierInspectorPanel final : public juce::Component
{
public:
    explicit AtelierInspectorPanel (TrackActions&);
    ~AtelierInspectorPanel() override;
    void setEdit (tracktion::Edit*);
    void refresh (tracktion::AudioTrack*, bool editable);
    void appearanceChanged();
    void suspend() { releaseSustain(); }
    int desiredHeight (int width) const;
    void resized() override;
    void paint (juce::Graphics&) override;
   #if STUDIO_SPATIAL_TESTS
    bool verifyInspectorBindings();
   #endif
private:
    tracktion::AudioTrack* track() const;
    tracktion::Plugin* hosted() const;
    tracktion::VolumeAndPanPlugin* fader() const;
    bool canSendMidi() const;
    void changeArtwork();
    void loadArtwork();
    void refreshMappings();
    void selectMapping (int);
    void sendCC (int);
    void releaseSustain();
    void refreshMix();
    void writeCCState();

    TrackActions& actions;
    tracktion::Edit* edit = nullptr;
    tracktion::EditItemID trackId;
    juce::Image portraitImage;
    juce::String portraitName, cachedMappingXml, cachedCCState;
    juce::ComboBox artworkPicker, midiChannel;
    juce::Label heading, instrumentName, artCaption, mappingsHeading, ccHeading, mixHeading, midiStatus, micStatus;
    std::array<juce::TextButton, 8> articulationPads;
    std::array<juce::Slider, 8> ccKnobs;
    std::array<juce::Label, 8> ccLabels;
    std::vector<ArticulationMapping> mappings;
    juce::String activeMapping;
    juce::TextButton mappingsButton { "Edit Articulations" }, sendValues { "Send CC" },
                     fullInstrument { "Full Instrument" }, stageButton { "Stage & Mics" };
    juce::Slider volume, pan, roomSend;
    juce::Label volumeLabel, panLabel, roomLabel;
    juce::Rectangle<int> imageBounds, articulationBounds, ccBounds, mixerBounds;
    bool updating = false, editing = false;
    int pedalChannel = -1;
};
}
