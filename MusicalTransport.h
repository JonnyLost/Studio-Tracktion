// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <JuceHeader.h>

namespace studio
{
bool transportSettled (tracktion::Edit&);
bool setCycle (tracktion::Edit&, tracktion::TimeRange);
bool setTempoEvent (tracktion::Edit&, tracktion::BeatPosition, double bpm);
bool setMeterEvent (tracktion::Edit&, tracktion::BeatPosition, int numerator, int denominator);
tracktion::MarkerClip::Ptr addCueMarker (tracktion::Edit&, tracktion::TimePosition, const juce::String&);

class MusicalTransport final : public juce::TextButton
{
public:
    MusicalTransport();
    void setEdit (tracktion::Edit*);
    void refresh (bool available);
    void showTimelineMenu (tracktion::TimePosition);
    void toggleCycle();
    void toggleClick();
    void promptTempoAtPlayhead();
    void promptMeterAtPlayhead();
    std::function<void()> onTimelineChanged;

private:
    void showMenu (tracktion::TimePosition, bool includeTransport);
    void editTempo (tracktion::BeatPosition, bool existing);
    void editMeter (tracktion::BeatPosition, bool existing);
    void editMarker (tracktion::TimePosition, tracktion::MarkerClip::Ptr);
    void prompt (const juce::String& title, const juce::String& label, const juce::String& value,
                 bool removable, std::function<void (const juce::String&, bool)>);

    tracktion::Edit* edit = nullptr;
    unsigned session = 0;
};
}
