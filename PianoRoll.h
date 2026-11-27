// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <JuceHeader.h>
#include <functional>
#include <memory>
#include "MidiControllerLanes.h"
#include "InstrumentPerformanceSequence.h"

namespace te = tracktion;

/** A message-thread MIDI editor for the active take of one Tracktion MIDI clip. */
class StudioPianoRoll final : public juce::Component,
                              private juce::ScrollBar::Listener,
                              private juce::Timer
{
public:
    StudioPianoRoll();
    ~StudioPianoRoll() override;

    void setClip (te::MidiClip::Ptr);
    void setComparisonClips (const juce::Array<te::MidiClip*>& selectedClips);
    te::MidiClip* getClip() const noexcept { return clip.get(); }
    void refresh();
    void setEditingEnabled (bool);
    void setTransportPosition (tracktion::TimePosition);
    void silencePreview();
    std::function<void()> onMidiChanged, onControllerLayout;
    std::function<std::optional<studio::designer::Profile>(te::MidiClip&)> performanceProfile;
    std::function<bool()> canWritePerformance;
    void refreshPerformanceLane();
    std::function<void (tracktion::MidiClip&, tracktion::BeatPosition, int)> onNoteDrawn;
   #if STUDIO_SPATIAL_TESTS
    bool drawForReview (double sourceBeat);
   #endif

    void paint (juce::Graphics&) override;
    void appearanceChanged();
    int minimumContentHeight (int width);
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    enum class Gesture { none, draw, move, resizeStart, resizeEnd, velocity, lasso };

    te::MidiClip::Ptr clip;
    juce::Array<te::MidiClip::Ptr> comparisonClips;
    juce::ValueTree selectedNote;
    juce::Array<juce::ValueTree> selectedNotes;
    juce::Array<juce::ValueTree> lassoInitialSelection;
    juce::Rectangle<int> lassoBounds;
    juce::ComboBox tool, grid;
    juce::TextButton quantize { "Quantize" }, undo { "Undo" }, redo { "Redo" },
                     zoomIn { "H+" }, zoomOut { "H-" },
                     verticalZoomIn { "V+" }, verticalZoomOut { "V-" },
                     expression { "Expression..." };
    studio::MidiControllerLanes controllers;
    juce::Label caption;
    juce::ScrollBar horizontal { false }, vertical { true };

    juce::Rectangle<int> keyboard, notesArea, velocityArea, rulerArea, performanceArea;
    bool performanceLaneVisible=false;
    void paintPerformance(juce::Graphics&);
    void performanceMenu(const juce::MouseEvent&);
    Gesture gesture = Gesture::none;
    juce::ValueTree draggedNote;
    double originalBeat = 0.0, originalLength = 1.0;
    double previewBeat = 0.0, previewLength = 1.0;
    int originalPitch = 60, previewPitch = 60;
    int originalVelocity = 100, previewVelocity = 100;
    int startX = 0, startY = 0;
    double pixelsPerBeat = 56.0;
    int keyWidth = 54;
    int rowHeight = 14;
    bool editingEnabled = true;
    bool initialViewport = true;
    bool draggingKeyboard = false;
    int keyboardPreviewPitch = -1;
    int auditionPitch = -1, auditionChannel = 1;
    std::shared_ptr<te::MidiInputDevice> auditionDevice;
    te::Plugin::Ptr auditionSampler;
    double transportSourceBeat = 0.0;
    bool transportWithinClip = false;

    void scrollBarMoved (juce::ScrollBar*, double) override;
    void updateScrollBars();
    void updateControllerPlayhead();
    void setZoom (double);
    void setVerticalZoom (int);
    void audition (int pitch, int velocity);
    void stopAudition();
    void timerCallback() override;
    double gridBeats() const;
    double snap (double) const;
    double beatAt (int x) const;
    int pitchAt (int y) const;
    int xAt (double beat) const;
    double toSourceBeat (double editBeat) const;
    double toEditBeat (double sourceBeat) const;
    int yAt (int pitch) const;
    int velocityAt (int y) const;
    juce::Rectangle<int> noteBounds (const te::MidiNote&) const;
    te::MidiNote* noteAt (juce::Point<int>) const;
    te::MidiNote* selected() const;
    void quantizeNotes();
    void deleteSelected();
    void selectOnlyNote (juce::ValueTree);
    void paintGrid (juce::Graphics&);
    void paintRuler (juce::Graphics&);
    void paintPlayhead (juce::Graphics&);
    void paintNotes (juce::Graphics&);
    void paintVelocity (juce::Graphics&);
};
