// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <JuceHeader.h>
#include <array>
#include <functional>
#include <optional>
#include <vector>
#include "MidiExpressionValues.h"

namespace studio
{
class MidiControllerLanes final : public juce::Component
{
public:
    MidiControllerLanes();
    std::function<int(double)> xForBeat;
    std::function<double(int)> beatForX;
    std::function<double(double)> snapBeat;
    std::function<bool()> drawMode;
    std::function<void()> onChange, onLayoutChange;
    void setClip (tracktion::MidiClip*);
    void setEditingEnabled (bool);
    void showMenu (juce::Component&);
    void setGutter (int width) { gutter = width; }
    void setPlayhead (std::optional<int> x);
    int preferredHeight() const;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    struct Spec { int midiType; const char* label; ExpressionValue format; };
    static constexpr std::array specs {
        Spec { 1, "CC1 Modulation", ExpressionValue::sevenBit },
        Spec { 11, "CC11 Expression", ExpressionValue::sevenBit },
        Spec { 7, "CC7 Volume", ExpressionValue::sevenBit },
        Spec { 64, "CC64 Sustain", ExpressionValue::switch64 },
        Spec { tracktion::MidiControllerEvent::pitchWheelType, "Pitch Bend", ExpressionValue::pitch14 },
        Spec { tracktion::MidiControllerEvent::channelPressureType, "Channel Aftertouch", ExpressionValue::sevenBit }
    };
    struct DraftPoint { double beat; int stored; };
    tracktion::MidiClip* clip = nullptr; // The owning Piano Roll retains the clip.
    std::array<bool, specs.size()> visible {};
    int gutter = 0, active = -1;
    bool allowed = false, moving = false, drawing = false;
    std::optional<int> playheadX;
    std::optional<double> cursorBeat;
    juce::Array<juce::ValueTree> selection;
    juce::ValueTree dragged;
    juce::String feedback;
    std::vector<DraftPoint> stroke;
    juce::Point<int> origin, lastSample;
    DraftPoint preview {};
    struct ClipboardEvent { juce::ValueTree state; double relativeBeat; };
    std::vector<ClipboardEvent> clipboard;
    int clipboardChannel = 0;

    bool canEdit() const;
    int laneHeight() const;
    juce::Rectangle<int> laneBounds (int) const;
    int laneAt (int y) const;
    int yFor (juce::Rectangle<int>, int lane, int stored) const;
    int valueAt (juce::Rectangle<int>, int lane, int y) const;
    bool beatInClip (double) const;
    int midiChannel() const;
    tracktion::MidiControllerEvent* find (const juce::ValueTree&) const;
    void clearGesture();
    void selectLane (int);
    void addPoint (juce::Point<int>);
    void editSelection (bool remove);
    void paste();
    juce::String describe (int lane, int stored) const;
};
}
