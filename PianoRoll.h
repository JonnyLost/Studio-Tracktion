// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <JuceHeader.h>
#include <functional>

namespace te = tracktion;

/** A message-thread MIDI editor for the active take of one Tracktion MIDI clip. */
class StudioPianoRoll final : public juce::Component,
                              private juce::ScrollBar::Listener
{
public:
    StudioPianoRoll();
    ~StudioPianoRoll() override;

    void setClip (te::MidiClip::Ptr);
    te::MidiClip* getClip() const noexcept { return clip.get(); }
    void refresh();
    void setEditingEnabled (bool);
    std::function<void()> onMidiChanged;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    enum class Gesture { none, draw, move, resize, velocity };

    te::MidiClip::Ptr clip;
    juce::ValueTree selectedNote;
    juce::ComboBox tool, grid;
    juce::TextButton quantize { "Quantize" }, undo { "Undo" }, redo { "Redo" },
                     zoomIn { "+" }, zoomOut { "−" };
    juce::Label caption;
    juce::ScrollBar horizontal { false }, vertical { true };

    juce::Rectangle<int> keyboard, notesArea, velocityArea;
    Gesture gesture = Gesture::none;
    juce::ValueTree draggedNote;
    double originalBeat = 0.0, originalLength = 1.0;
    double previewBeat = 0.0, previewLength = 1.0;
    int originalPitch = 60, previewPitch = 60;
    int originalVelocity = 100, previewVelocity = 100;
    int startX = 0, startY = 0;
    double pixelsPerBeat = 56.0;
    static constexpr int keyWidth = 54, rowHeight = 14, velocityHeight = 72;
    bool editingEnabled = true;
    bool initialViewport = true;

    void scrollBarMoved (juce::ScrollBar*, double) override;
    void updateScrollBars();
    void setZoom (double);
    double gridBeats() const;
    double snap (double) const;
    double beatAt (int x) const;
    int pitchAt (int y) const;
    int xAt (double beat) const;
    int yAt (int pitch) const;
    int velocityAt (int y) const;
    juce::Rectangle<int> noteBounds (const te::MidiNote&) const;
    te::MidiNote* noteAt (juce::Point<int>) const;
    te::MidiNote* selected() const;
    void quantizeNotes();
    void deleteSelected();
    void paintGrid (juce::Graphics&);
    void paintNotes (juce::Graphics&);
    void paintVelocity (juce::Graphics&);
};
