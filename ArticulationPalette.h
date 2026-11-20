// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <JuceHeader.h>
#include "ArticulationMappings.h"
#include <functional>
#include <memory>
#include <optional>

namespace studio
{
class ArticulationPalette final : public juce::Component, private juce::Timer
{
public:
    explicit ArticulationPalette (juce::PropertiesFile&);
    ~ArticulationPalette() override;
    std::function<void()> onToggleDock, onLayoutChange;
    void setContext (tracktion::Edit*);
    void refresh (tracktion::AudioTrack*, tracktion::MidiClip*, bool enabled);
    void setFloating (bool);
    void appearanceChanged();
    int minimumContentHeight (int);
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void stopLiveNote();

private:
    enum class Orientation { matrix = 1, vertical, horizontal };
    juce::PropertiesFile& prefs;
    tracktion::Edit* edit = nullptr;
    tracktion::EditItemID followedTrack, pinnedTrack, loadedTrack, loadedInstrument;
    tracktion::MidiClip::Ptr selectedClip;
    bool ready = false, mappingWritable = false, panelOpen = false;
    juce::String serialized, selectedId, lastSentId, actionStatus;
    std::vector<ArticulationMapping> mappings;
    std::vector<std::unique_ptr<juce::TextButton>> pads;
    juce::Viewport padViewport;
    juce::Component padBank;
    juce::Label subject, explanation;
    juce::ComboBox orientation, kind, channel, shortcut;
    juce::TextButton pin { "Pin track" }, dock { "Float" }, insert { "Insert at playhead" },
                     newMapping { "Add mapping" }, editMapping { "Edit mapping" },
                     apply { "Save mapping" }, remove { "Remove" }, cancel { "Cancel" };
    juce::TextEditor name, number, value;
    juce::String editingId;
    struct HeldNote { tracktion::EditItemID track; int channel, number; };
    std::optional<HeldNote> held;

    tracktion::AudioTrack* track (tracktion::EditItemID) const;
    tracktion::AudioTrack* target() const;
    const ArticulationMapping* selected() const;
    bool canInsert() const;
    void startMapping (bool modify);
    void saveMapping (bool removeMapping);
    void persistMappings();
    void syncTrack();
    void rebuildPads();
    void updateState();
    void setStatus (const juce::String&);
    void layoutChanged();
    void activate (size_t);
    void insertMapping();
    void timerCallback() override { stopLiveNote(); }
    int preferredPadRows (int width) const;
};
}
