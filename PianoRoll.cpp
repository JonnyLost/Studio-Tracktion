// SPDX-License-Identifier: GPL-3.0-or-later
#include "PianoRoll.h"
#include "MusicalRuler.h"
#include "StudioUI.h"
#include <cmath>

using namespace juce;

StudioPianoRoll::StudioPianoRoll()
{
    setWantsKeyboardFocus (true);
    for (Component* c : std::initializer_list<Component*> { &tool, &grid, &quantize, &undo, &redo,
                                                              &zoomIn, &zoomOut, &verticalZoomIn,
                                                              &verticalZoomOut, &caption,
                                                              &horizontal, &vertical, &expression, &controllers })
        addAndMakeVisible (c);

    tool.addItem ("Select", 1);
    tool.addItem ("Draw", 2);
    tool.setSelectedId (1, dontSendNotification);
    tool.setTooltip ("Select notes to move or resize; Draw places new notes");
    grid.addItem ("Snap off", 1);
    grid.addItem ("1/4", 2);
    grid.addItem ("1/4 T", 9);
    grid.addItem ("1/8", 3);
    grid.addItem ("1/8 T", 6);
    grid.addItem ("1/16", 4);
    grid.addItem ("1/16 T", 7);
    grid.addItem ("1/32", 5);
    grid.addItem ("1/32 T", 8);
    grid.setSelectedId (4, dontSendNotification);
    grid.setTooltip ("Snap grid, in musical note values");
    caption.setColour (Label::textColourId, studio::colours::text);
    caption.setFont (FontOptions (13.0f));
    horizontal.addListener (this);
    vertical.addListener (this);

    zoomIn.onClick = [this] { setZoom (pixelsPerBeat * 1.25); };
    zoomOut.onClick = [this] { setZoom (pixelsPerBeat / 1.25); };
    verticalZoomIn.onClick = [this] { setVerticalZoom (rowHeight + 2); };
    verticalZoomOut.onClick = [this] { setVerticalZoom (rowHeight - 2); };
    zoomIn.setTooltip ("Horizontal zoom in (Command + scroll)");
    zoomOut.setTooltip ("Horizontal zoom out (Command + scroll)");
    verticalZoomIn.setTooltip ("Increase piano key height");
    verticalZoomOut.setTooltip ("Decrease piano key height");
    undo.onClick = [this] { if (clip) clip->edit.undo(); refresh(); };
    redo.onClick = [this] { if (clip) clip->edit.redo(); refresh(); };
    quantize.onClick = [this] { quantizeNotes(); };
    expression.setTooltip ("Show MIDI controller lanes");
    expression.onClick = [this] { controllers.showMenu (expression); };
    controllers.xForBeat = [this] (double beat) { return xAt (beat) - controllers.getX(); };
    controllers.beatForX = [this] (int x) { return beatAt (x + controllers.getX()); };
    controllers.snapBeat = [this] (double beat) { return snap (beat); };
    controllers.drawMode = [this] { return tool.getSelectedId() == 2; };
    controllers.onChange = [this] { refresh(); };
    controllers.onLayoutChange = [this]
    {
        if (onControllerLayout) onControllerLayout();
        resized();
    };
    controllers.setEditingEnabled (editingEnabled);
    refresh();
}

StudioPianoRoll::~StudioPianoRoll()
{
    stopAudition();
    horizontal.removeListener (this);
    vertical.removeListener (this);
}

void StudioPianoRoll::setClip (te::MidiClip::Ptr next)
{
    if (clip == next)
        return;
    stopAudition();
    gesture = Gesture::none;
    draggingKeyboard = false;
    keyboardPreviewPitch = -1;
    selectedNote = {};
    selectedNotes.clear();
    lassoInitialSelection.clear();
    lassoBounds = {};
    draggedNote = {};
    clip = std::move (next);
    controllers.setClip (clip.get());
    comparisonClips.clear();
    transportWithinClip = false;
    horizontal.setCurrentRangeStart (0, dontSendNotification);
    refresh();
}

void StudioPianoRoll::setComparisonClips (const Array<te::MidiClip*>& selectedClips)
{
    Array<te::MidiClip::Ptr> next;
    for (auto* candidate : selectedClips)
        if (candidate != nullptr && candidate != clip.get())
            next.add (candidate);

    if (next == comparisonClips)
        return;
    comparisonClips = std::move (next);
    refresh();
}

void StudioPianoRoll::setEditingEnabled (bool enabled)
{
    if (editingEnabled == enabled)
        return;
    if (! enabled)
    {
        stopAudition();
        gesture = Gesture::none;
        draggingKeyboard = false;
        keyboardPreviewPitch = -1;
        draggedNote = {};
        lassoBounds = {};
    }
    editingEnabled = enabled;
    controllers.setEditingEnabled (enabled);
    for (Component* c : std::initializer_list<Component*> { &tool, &grid, &quantize, &undo, &redo })
        c->setEnabled (enabled && clip != nullptr);
    repaint();
}

void StudioPianoRoll::refresh()
{
    const bool hasClip = clip != nullptr && clip->hasValidSequence();
    if (hasClip)
    {
        for (int i = selectedNotes.size(); --i >= 0;)
            if (clip->getSequence().getNoteFor (selectedNotes.getReference (i)) == nullptr)
                selectedNotes.remove (i);
        if (! selectedNotes.contains (selectedNote))
            selectedNote = selectedNotes.isEmpty() ? ValueTree() : selectedNotes.getLast();
    }
    caption.setText (hasClip ? clip->getName() + "  |  " + String (clip->getSequence().getNumNotes())
                                 + " notes" + (comparisonClips.isEmpty() ? String()
                                      : "  |  " + String (comparisonClips.size()) + " other clips (read-only)" )
                                 + (selectedNotes.size() > 1 ? "  |  " + String (selectedNotes.size()) + " selected" : String())
                             : "Select a MIDI clip in the arrangement", dontSendNotification);
    for (Component* c : std::initializer_list<Component*> { &tool, &grid, &quantize, &undo, &redo,
                                                              &zoomIn, &zoomOut, &verticalZoomIn,
                                                              &verticalZoomOut })
        c->setEnabled (hasClip && (editingEnabled || c == &zoomIn || c == &zoomOut));
    verticalZoomIn.setEnabled (hasClip);
    verticalZoomOut.setEnabled (hasClip);
    updateScrollBars();
    controllers.repaint();
    repaint();
    if (onMidiChanged)
        onMidiChanged();
}

void StudioPianoRoll::appearanceChanged()
{
    resized();
    repaint();
}

int StudioPianoRoll::minimumContentHeight (int width)
{
    const auto& m = studio::appearance;
    return studio::toolbarHeight (jmax (1, width - m.px (8)),
        { { &tool, 85 }, { &grid, 94 }, { &quantize, 83 }, { &undo, 54 }, { &redo, 54 },
          { &verticalZoomIn, 31 }, { &verticalZoomOut, 31 }, { &zoomIn, 31 }, { &zoomOut, 31 },
          { &expression, 112 } })
        + m.line (13, 2) + m.control (24) + m.scrollbar() + m.px (118) + controllers.preferredHeight();
}

void StudioPianoRoll::resized()
{
    const auto& m = studio::appearance;
    keyWidth = m.span (60);
    auto area = getLocalBounds().reduced (m.px (4), m.px (2));
    studio::layoutToolbar (area,
        { { &tool, 85 }, { &grid, 94 }, { &quantize, 83 }, { &undo, 54 }, { &redo, 54 },
          { &verticalZoomIn, 31 }, { &verticalZoomOut, 31 }, { &zoomIn, 31 }, { &zoomOut, 31 },
          { &expression, 112 } });
    caption.setBounds (area.removeFromTop (m.line (13, 2)));
    auto rulerRow = area.removeFromTop (m.control (24));
    const auto scrollbar = m.scrollbar();
    rulerArea = rulerRow.withTrimmedLeft (keyWidth).withTrimmedRight (scrollbar);
    auto controllerRow = area.removeFromBottom (controllers.preferredHeight()).withTrimmedRight (scrollbar);
    auto horizontalRow = area.removeFromBottom (scrollbar);
    vertical.setBounds (area.removeFromRight (scrollbar));
    controllers.setBounds (controllerRow);
    controllers.setGutter (keyWidth);
    auto velocityRow = area.removeFromBottom (jmin (m.px (72), area.getHeight() / 2));
    velocityArea = velocityRow.withTrimmedLeft (keyWidth);
    keyboard = area.removeFromLeft (keyWidth);
    notesArea = area;
    horizontal.setBounds (horizontalRow.withTrimmedLeft (keyWidth).withTrimmedRight (scrollbar));
    updateScrollBars();
}

double StudioPianoRoll::gridBeats() const
{
    switch (grid.getSelectedId())
    {
        case 2: return 1.0;
        case 3: return 0.5;
        case 4: return 0.25;
        case 5: return 0.125;
        case 6: return 1.0 / 3.0;
        case 7: return 1.0 / 6.0;
        case 8: return 1.0 / 12.0;
        case 9: return 2.0 / 3.0;
        default: return 0.0;
    }
}

double StudioPianoRoll::snap (double beat) const
{
    const auto step = gridBeats();
    if (step <= 0.0 || clip == nullptr)
        return jmax (0.0, beat);

    // Anchor the snap interval at the musical bar, matching the drawn grid.
    const auto& sequence = clip->edit.tempoSequence;
    const int bar = jmax (0, sequence.toBarsAndBeats (
        clip->getTimeOfContentBeat (te::BeatPosition::fromBeats (beat))).bars);
    const double barStart = toSourceBeat (sequence.toBeats (
        te::tempo::BarsAndBeats { bar, {} }).inBeats());
    const double barEnd = toSourceBeat (sequence.toBeats (
        te::tempo::BarsAndBeats { bar + 1, {} }).inBeats());
    const auto closestLine = barStart + std::round ((beat - barStart) / step) * step;
    return jmax (0.0, jmin (barEnd, closestLine));
}

double StudioPianoRoll::beatAt (int x) const
{
    return jmax (0.0, (x - notesArea.getX()) / pixelsPerBeat + horizontal.getCurrentRangeStart());
}

int StudioPianoRoll::pitchAt (int y) const
{
    return jlimit (0, 127, 127 - (int) std::floor ((y - notesArea.getY()
                                                          + vertical.getCurrentRangeStart()) / rowHeight));
}

int StudioPianoRoll::xAt (double beat) const
{
    return notesArea.getX() + roundToInt ((beat - horizontal.getCurrentRangeStart()) * pixelsPerBeat);
}

double StudioPianoRoll::toSourceBeat (double editBeat) const
{
    if (clip == nullptr)
        return editBeat;
    // Clip owns the source offset and sync mode; use its canonical mapping.
    const auto time = clip->edit.tempoSequence.toTime (te::BeatPosition::fromBeats (editBeat));
    return clip->getContentBeatAtTime (time).inBeats();
}

double StudioPianoRoll::toEditBeat (double sourceBeat) const
{
    if (clip == nullptr)
        return sourceBeat;
    const auto time = clip->getTimeOfContentBeat (te::BeatPosition::fromBeats (sourceBeat));
    return clip->edit.tempoSequence.toBeats (time).inBeats();
}

void StudioPianoRoll::setTransportPosition (te::TimePosition time)
{
    const auto oldBeat = transportSourceBeat;
    const bool oldVisible = transportWithinClip;
    transportWithinClip = clip != nullptr && time >= clip->getPosition().getStart()
                                             && time < clip->getPosition().getEnd();
    if (transportWithinClip)
    {
        transportSourceBeat = clip->getContentBeatAtTime (time).inBeats();
        if (clip->isLooping())
        {
            const auto length = clip->getLoopLengthBeats().inBeats();
            const auto start = clip->getLoopStartBeats().inBeats();
            if (length > 0.0 && transportSourceBeat >= start)
                transportSourceBeat = start + std::fmod (transportSourceBeat - start, length);
        }
    }
    updateControllerPlayhead();
    // The arrangement and piano roll read the same transport clock. Avoid
    // repainting if neither the visible state nor screen position changed.
    if (oldVisible == transportWithinClip
        && (! transportWithinClip || xAt (oldBeat) == xAt (transportSourceBeat)))
        return;
    if (oldVisible)
        repaint (xAt (oldBeat) - 3, rulerArea.getY(), 7, velocityArea.getBottom() - rulerArea.getY());
    if (transportWithinClip)
        repaint (xAt (transportSourceBeat) - 3, rulerArea.getY(), 7,
                 velocityArea.getBottom() - rulerArea.getY());
}

int StudioPianoRoll::yAt (int pitch) const
{
    return notesArea.getY() + (127 - pitch) * rowHeight
           - roundToInt (vertical.getCurrentRangeStart());
}

int StudioPianoRoll::velocityAt (int y) const
{
    return jlimit (1, 127, roundToInt ((velocityArea.getBottom() - y) * 127.0
                                      / jmax (1, velocityArea.getHeight() - 8)));
}

Rectangle<int> StudioPianoRoll::noteBounds (const te::MidiNote& note) const
{
    return { xAt (note.getStartBeat().inBeats()), yAt (note.getNoteNumber()) + 1,
             jmax (3, roundToInt (note.getLengthBeats().inBeats() * pixelsPerBeat)), rowHeight - 2 };
}

te::MidiNote* StudioPianoRoll::selected() const
{
    return clip != nullptr && selectedNote.isValid() ? clip->getSequence().getNoteFor (selectedNote) : nullptr;
}

void StudioPianoRoll::selectOnlyNote (ValueTree state)
{
    selectedNotes.clear();
    if (state.isValid())
        selectedNotes.add (state);
    selectedNote = state;
}

te::MidiNote* StudioPianoRoll::noteAt (Point<int> point) const
{
    if (clip == nullptr || ! notesArea.contains (point))
        return nullptr;
    // Iterate backwards so a later overlapping note receives the click.
    const auto& notes = clip->getSequence().getNotes();
    for (int i = notes.size(); --i >= 0;)
        if (noteBounds (*notes[i]).contains (point))
            return notes[i];
    return nullptr;
}

void StudioPianoRoll::updateControllerPlayhead()
{
    const int x = xAt (transportSourceBeat);
    controllers.setPlayhead (transportWithinClip && x >= notesArea.getX() && x < notesArea.getRight()
        ? std::optional<int> (x - controllers.getX()) : std::nullopt);
}

void StudioPianoRoll::updateScrollBars()
{
    double endBeat = 16.0;
    if (clip != nullptr && clip->hasValidSequence())
    {
        const auto bounds = clip->getPosition();
        endBeat = jmax (endBeat, clip->isLooping()
            ? (clip->getLoopStartBeats() + clip->getLoopLengthBeats()).inBeats()
            : clip->getContentBeatAtTime (bounds.getEnd()).inBeats());
        for (auto* note : clip->getSequence().getNotes())
            endBeat = jmax (endBeat, note->getEndBeat().inBeats() + 4.0);
        const auto& events = clip->getSequence().getControllerEvents();
        if (auto* tail = events.getLast())
            endBeat = jmax (endBeat, tail->getBeatPosition().inBeats() + 1.0);
    }
    if (clip != nullptr)
        for (const auto& other : comparisonClips)
            if (other != nullptr && other->hasValidSequence())
                for (auto* note : other->getSequence().getNotes())
                    endBeat = jmax (endBeat, clip->getContentBeatAtTime (
                        other->getTimeOfContentBeat (note->getEndBeat())).inBeats() + 4.0);

    const auto visibleBeats = notesArea.getWidth() / pixelsPerBeat;
    horizontal.setRangeLimits (0.0, jmax (endBeat, visibleBeats));
    horizontal.setCurrentRange (horizontal.getCurrentRangeStart(), visibleBeats, dontSendNotification);
    horizontal.setSingleStepSize (jmax (1.0 / 48.0, gridBeats()));

    vertical.setRangeLimits (0.0, 128.0 * rowHeight);
    vertical.setCurrentRange (vertical.getCurrentRangeStart(), notesArea.getHeight(), dontSendNotification);
    vertical.setSingleStepSize (rowHeight);
    updateControllerPlayhead();
    controllers.repaint();
    if (initialViewport && notesArea.getHeight() > 0)
    {
        vertical.setCurrentRangeStart (57.0 * rowHeight, dontSendNotification);
        initialViewport = false;
    }
}

void StudioPianoRoll::setZoom (double value)
{
    pixelsPerBeat = jlimit (20.0, 240.0, value);
    updateScrollBars();
    repaint();
}

void StudioPianoRoll::setVerticalZoom (int value)
{
    const auto next = jlimit (9, 32, value);
    if (next == rowHeight)
        return;
    const double centreRow = (vertical.getCurrentRangeStart() + notesArea.getHeight() / 2.0) / rowHeight;
    rowHeight = next;
    updateScrollBars();
    vertical.setCurrentRangeStart (centreRow * rowHeight - notesArea.getHeight() / 2.0);
    repaint();
}

void StudioPianoRoll::audition (int pitch, int velocity)
{
    stopAudition();
    if (clip == nullptr || clip->edit.getTransport().isRecording())
        return;

    // Inject only into a MIDI input ALREADY assigned to this clip's track.
    // We must not arm a track, change device routing, record preview notes,
    // or alter the clip's stored MIDI channel to audition a note.
    auto* target = dynamic_cast<te::AudioTrack*> (clip->getTrack());
    if (target == nullptr)
        return;

    for (auto* instance : clip->edit.getAllInputDevices())
    {
        const auto destinations = te::getTargetTracks (*instance);
        // A shared MIDI input would also sound other instruments: do not
        // preview through it until Studio has a dedicated audition route.
        if (destinations.size() != 1 || ! destinations.contains (target))
            continue;
        auto* midi = dynamic_cast<te::MidiInputDevice*> (&instance->getInputDevice());
        if (midi == nullptr || ! midi->isEnabled())
            continue;
        for (const auto& device : clip->edit.engine.getDeviceManager().getMidiInDevices())
            if (device.get() == midi)
            {
                auditionDevice = device;
                break;
            }
        if (auditionDevice != nullptr)
            break;
    }

    if (auditionDevice == nullptr)
        return; // No input route: never send a preview to an unrelated track.

    auditionPitch = jlimit (0, 127, pitch);
    auditionChannel = jlimit (1, 16, clip->getMidiChannel().getChannelNumber());
    auditionDevice->handleIncomingMidiMessage (nullptr,
        MidiMessage::noteOn (auditionChannel, auditionPitch, (uint8) jlimit (1, 127, velocity)));
    startTimer (450); // Audibly preview slow-attack sounds even on quick clicks.
    repaint (keyboard);
}

void StudioPianoRoll::stopAudition()
{
    stopTimer();
    if (auditionPitch >= 0 && auditionDevice != nullptr)
        auditionDevice->handleIncomingMidiMessage (nullptr,
            MidiMessage::noteOff (auditionChannel, auditionPitch));
    auditionPitch = -1;
    auditionDevice.reset();
    repaint (keyboard);
}

void StudioPianoRoll::silencePreview()
{
    stopAudition();
}

void StudioPianoRoll::timerCallback()
{
    stopAudition();
}

void StudioPianoRoll::scrollBarMoved (ScrollBar*, double)
{
    updateControllerPlayhead();
    controllers.repaint();
    repaint();
}

void StudioPianoRoll::paint (Graphics& g)
{
    g.fillAll (studio::colours::timeline);
    if (notesArea.isEmpty())
        return;
    paintGrid (g);
    paintNotes (g);
    if (gesture == Gesture::lasso && ! lassoBounds.isEmpty())
    {
        g.saveState();
        g.reduceClipRegion (notesArea);
        g.setColour (studio::colours::focus.withAlpha (0.20f));
        g.fillRect (lassoBounds);
        g.setColour (studio::colours::focus);
        g.drawRect (lassoBounds, 1);
        g.restoreState();
    }
    paintVelocity (g);
    paintRuler (g);
    paintPlayhead (g);
}

void StudioPianoRoll::paintGrid (Graphics& g)
{
    g.setColour (studio::colours::panel);
    g.fillRect (keyboard);
    g.fillRect (notesArea);

    const auto& metrics = studio::appearance;
    const auto topPitch = pitchAt (notesArea.getY());
    const auto bottomPitch = pitchAt (notesArea.getBottom());
    for (int pitch = topPitch; pitch >= bottomPitch; --pitch)
    {
        const int y = yAt (pitch);
        if (y + rowHeight < notesArea.getY() || y >= notesArea.getBottom())
            continue;
        const int semitone = pitch % 12;
        const bool black = semitone == 1 || semitone == 3 || semitone == 6 || semitone == 8 || semitone == 10;
        g.setColour (black ? studio::colours::canvas : studio::colours::timeline);
        g.fillRect (notesArea.getX(), y, notesArea.getWidth(), rowHeight - 1);
        g.setColour (black ? studio::colours::well : studio::colours::action);
        g.fillRect (keyboard.getX(), y, keyboard.getWidth() - 1, rowHeight - 1);
        if (pitch == auditionPitch)
        {
            g.setColour (studio::colours::brass.withAlpha (0.85f));
            g.fillRect (keyboard.getX(), y, keyboard.getWidth() - 1, rowHeight - 1);
        }
        if (pitch % 12 == 0)
        {
            g.setColour (studio::colours::grid);
            g.drawHorizontalLine (y, (float) notesArea.getX(), (float) notesArea.getRight());
            g.setColour (black ? studio::colours::text : studio::colours::well);
            g.setFont (FontOptions (jmin (metrics.fontSize (10.0f), (float) rowHeight - 1.0f)));
            g.drawText ("C" + String (pitch / 12 - 1), keyboard.getX() + metrics.px (4), y, keyWidth - metrics.px (7),
                        rowHeight, Justification::centredLeft);
        }
    }

    g.saveState();
    g.reduceClipRegion (notesArea);
    if (clip != nullptr)
    {
        const auto& sequence = clip->edit.tempoSequence;
        const auto from = sequence.toTime (te::BeatPosition::fromBeats (
            jmax (0.0, toEditBeat (horizontal.getCurrentRangeStart()))));
        const auto to = sequence.toTime (te::BeatPosition::fromBeats (
            jmax (0.0, toEditBeat (horizontal.getCurrentRangeStart()
                                   + notesArea.getWidth() / pixelsPerBeat))));
        const auto subdivision = gridBeats();
        // Lines and note-snapping now share bar-relative origins. At wide zoom
        // only their ink is suppressed; the snap precision stays unchanged.
        if (subdivision > 0.0 && subdivision * pixelsPerBeat >= 11.0)
        {
            g.setColour (studio::colours::grid.withAlpha (0.65f));
            studio::forEachMusicalSubdivision (sequence, from, to, subdivision,
                [&] (double editBeat)
                {
                    const int x = xAt (toSourceBeat (editBeat));
                    if (x >= notesArea.getX() && x < notesArea.getRight())
                        g.drawVerticalLine (x, (float) notesArea.getY(), (float) notesArea.getBottom());
                });
        }
        int lastBeatLineX = notesArea.getX() - 16;
        studio::forEachMusicalTick (sequence, from, to, [&] (const studio::MusicalTick& tick)
        {
            const int x = xAt (toSourceBeat (sequence.toBeats (tick.time).inBeats()));
            if (x < notesArea.getX() || x >= notesArea.getRight())
                return;
            if (tick.beatIndex == 0)
            {
                g.setColour (studio::colours::action);
                g.fillRect (x, notesArea.getY(), 2, notesArea.getHeight());
                lastBeatLineX = x;
            }
            else if (x - lastBeatLineX >= 14)
            {
                g.setColour (studio::colours::divider);
                g.drawVerticalLine (x, (float) notesArea.getY(), (float) notesArea.getBottom());
                lastBeatLineX = x;
            }
        });
    }
    g.restoreState();
}

void StudioPianoRoll::paintRuler (Graphics& g)
{
    g.setColour (studio::colours::control);
    g.fillRect (rulerArea);
    if (clip == nullptr || rulerArea.isEmpty())
        return;

    const auto& sequence = clip->edit.tempoSequence;
    const auto from = sequence.toTime (te::BeatPosition::fromBeats (
        jmax (0.0, toEditBeat (horizontal.getCurrentRangeStart()))));
    const auto to = sequence.toTime (te::BeatPosition::fromBeats (
        jmax (0.0, toEditBeat (horizontal.getCurrentRangeStart()
                               + rulerArea.getWidth() / pixelsPerBeat))));
    const auto& m = studio::appearance;
    int lastLabelRight = rulerArea.getX() - m.span (50);
    int lastBeatTickX = rulerArea.getX() - 14;
    g.saveState();
    g.reduceClipRegion (rulerArea);
    if (const auto subdivision = gridBeats(); subdivision > 0.0 && subdivision * pixelsPerBeat >= 11.0)
    {
        g.setColour (studio::colours::grid);
        studio::forEachMusicalSubdivision (sequence, from, to, subdivision,
            [&] (double editBeat)
            {
                const int x = xAt (toSourceBeat (editBeat));
                if (x >= rulerArea.getX() && x < rulerArea.getRight())
                    g.drawVerticalLine (x, (float) rulerArea.getBottom() - 4.0f,
                                        (float) rulerArea.getBottom());
            });
    }
    studio::forEachMusicalTick (sequence, from, to, [&] (const studio::MusicalTick& tick)
    {
        const int x = xAt (toSourceBeat (sequence.toBeats (tick.time).inBeats()));
        if (x < rulerArea.getX() || x >= rulerArea.getRight())
            return;
        const bool bar = tick.beatIndex == 0;
        if (! bar && x - lastBeatTickX < 14)
            return;
        g.setColour (bar ? studio::colours::action : studio::colours::secondary);
        g.drawVerticalLine (x, (float) (rulerArea.getBottom() - (bar ? 13 : 7)),
                            (float) rulerArea.getBottom());
        lastBeatTickX = x;
        if (bar && x >= lastLabelRight)
        {
            g.setFont (studio::uiFont (11.0f));
            g.drawText (String (tick.barIndex + 1), x + m.px (3), rulerArea.getY(), m.span (43),
                        rulerArea.getHeight() - m.px (5), Justification::centredLeft);
            lastLabelRight = x + m.span (42);
        }
    });
    g.restoreState();
}

void StudioPianoRoll::paintPlayhead (Graphics& g)
{
    if (! transportWithinClip || clip == nullptr)
        return;
    const int x = xAt (transportSourceBeat);
    if (x < notesArea.getX() || x >= notesArea.getRight())
        return;
    g.setColour (studio::colours::playhead);
    g.drawVerticalLine (x, (float) rulerArea.getY(), (float) velocityArea.getBottom());
    g.fillEllipse ((float) x - 3.0f, (float) rulerArea.getY() + 1.0f, 6.0f, 6.0f);
}

void StudioPianoRoll::paintNotes (Graphics& g)
{
    if (clip == nullptr || ! clip->hasValidSequence())
        return;
    g.saveState();
    g.reduceClipRegion (notesArea);
    // Selected neighbouring MIDI regions are shown in the active clip's
    // musical time coordinates. Ghost notes are intentionally read-only.
    for (const auto& other : comparisonClips)
        if (other != nullptr && other->hasValidSequence())
            for (auto* note : other->getSequence().getNotes())
            {
                const double start = clip->getContentBeatAtTime (
                    other->getTimeOfContentBeat (note->getStartBeat())).inBeats();
                const double end = clip->getContentBeatAtTime (
                    other->getTimeOfContentBeat (note->getEndBeat())).inBeats();
                const Rectangle<int> box { xAt (start), yAt (note->getNoteNumber()) + 2,
                    jmax (3, xAt (end) - xAt (start)), rowHeight - 4 };
                if (box.intersects (notesArea))
                {
                    g.setColour (studio::colours::strings.withAlpha (0.36f));
                    g.fillRoundedRectangle (box.toFloat(), 2.0f);
                    g.setColour (studio::colours::action.withAlpha (0.7f));
                    g.drawRoundedRectangle (box.toFloat(), 2.0f, 1.0f);
                }
            }
    for (auto* note : clip->getSequence().getNotes())
    {
        auto bounds = noteBounds (*note);
        if ((gesture == Gesture::draw || gesture == Gesture::move
             || gesture == Gesture::resizeStart || gesture == Gesture::resizeEnd)
            && draggedNote == note->state)
            bounds = { xAt (previewBeat), yAt (previewPitch) + 1,
                       jmax (3, roundToInt (previewLength * pixelsPerBeat)), rowHeight - 2 };
        else if (gesture == Gesture::move && selectedNotes.contains (note->state))
            bounds.translate (xAt (previewBeat) - xAt (originalBeat),
                              (originalPitch - previewPitch) * rowHeight);
        if (! bounds.intersects (notesArea))
            continue;
        const bool active = selectedNotes.contains (note->state);
        const auto velocity = gesture == Gesture::velocity && active
                            ? jlimit (1, 127, note->getVelocity() + previewVelocity - originalVelocity)
                            : note->getVelocity();
        const auto fraction = jlimit (0.0f, 1.0f, velocity / 127.0f);
        const auto noteColour = studio::colours::midiClip.interpolatedWith (studio::colours::strings,
                                                                          0.34f + 0.66f * fraction);
        g.setColour (noteColour);
        g.fillRoundedRectangle (bounds.toFloat(), 2.0f);
        g.setColour (active ? studio::colours::focus : noteColour.brighter (0.16f));
        g.drawRoundedRectangle (bounds.toFloat().reduced (0.5f), 2.0f, active ? 1.5f : 1.0f);
        if (active && bounds.getWidth() >= 18)
        {
            // Edge markers make the two trim targets discoverable without a manual.
            g.setColour (Colours::white.withAlpha (0.75f));
            g.fillRect (bounds.getX() + 2, bounds.getY() + 2, 2, bounds.getHeight() - 4);
            g.fillRect (bounds.getRight() - 4, bounds.getY() + 2, 2, bounds.getHeight() - 4);
        }
        if (bounds.getWidth() >= studio::appearance.span (28)
            && bounds.getHeight() >= studio::appearance.fontSize (10.0f))
        {
            g.setColour (fraction < 0.48f ? studio::colours::text : studio::colours::well);
            g.setFont (studio::uiFont (10.0f));
            g.drawText (MidiMessage::getMidiNoteName (note->getNoteNumber(), true, true, 4),
                        bounds.reduced (3, 0), Justification::centredLeft);
        }
    }
    g.restoreState();
}

void StudioPianoRoll::paintVelocity (Graphics& g)
{
    g.setColour (studio::colours::well);
    g.fillRect (velocityArea);
    g.setColour (studio::colours::secondary);
    g.setFont (studio::uiFont (11.0f));
    g.drawText ("Velocity", velocityArea.withX (0).withWidth (keyWidth), Justification::centred);
    if (clip == nullptr)
        return;
    g.saveState();
    g.reduceClipRegion (velocityArea);
    for (auto* note : clip->getSequence().getNotes())
    {
        const int x = xAt (note->getStartBeat().inBeats());
        if (x < velocityArea.getX() || x >= velocityArea.getRight())
            continue;
        const int vel = gesture == Gesture::velocity && selectedNotes.contains (note->state)
                      ? jlimit (1, 127, note->getVelocity() + previewVelocity - originalVelocity)
                      : note->getVelocity();
        const int h = roundToInt ((velocityArea.getHeight() - 8) * vel / 127.0);
        g.setColour (selectedNotes.contains (note->state) ? studio::colours::focus
                    : studio::colours::midiClip.interpolatedWith (studio::colours::strings, (float) vel / 127.0f));
        g.fillRect (x, velocityArea.getBottom() - h, 5, h);
    }
    g.restoreState();
}

void StudioPianoRoll::mouseDown (const MouseEvent& event)
{
    if (clip == nullptr || ! editingEnabled || ! clip->hasValidSequence())
        return;
    grabKeyboardFocus();
    const auto point = event.getPosition();
    if (rulerArea.contains (point))
    {
        if (clip->edit.getTransport().isRecording())
            return;
        // Clicking an actual subdivision in the piano ruler seeks transport
        // to that musical grid point; it does not modify MIDI note data.
        const auto position = clip->getTimeOfContentBeat (
            te::BeatPosition::fromBeats (snap (beatAt (point.x))));
        if (position >= clip->getPosition().getStart()
            && position <= clip->getPosition().getEnd())
            clip->edit.getTransport().setPosition (position);
        return;
    }
    if (keyboard.contains (point))
    {
        draggingKeyboard = true;
        keyboardPreviewPitch = pitchAt (point.y);
        audition (keyboardPreviewPitch, 100);
        return;
    }
    startX = point.x;
    startY = point.y;
    gesture = Gesture::none;
    draggedNote = {};

    if (velocityArea.contains (point))
    {
        auto* hit = selected();
        if (hit == nullptr || std::abs (xAt (hit->getStartBeat().inBeats()) - point.x) > 8)
        {
            for (auto* note : clip->getSequence().getNotes())
                if (std::abs (xAt (note->getStartBeat().inBeats()) - point.x) <= 5)
                {
                    hit = note;
                    break;
                }
        }
        if (hit != nullptr)
        {
            if (! selectedNotes.contains (hit->state))
                selectOnlyNote (hit->state);
            selectedNote = draggedNote = hit->state;
            originalVelocity = hit->getVelocity();
            previewVelocity = velocityAt (point.y);
            gesture = Gesture::velocity;
        }
    }
    else if (notesArea.contains (point))
    {
        if (auto* hit = noteAt (point))
        {
            if (event.mods.isShiftDown())
            {
                if (selectedNotes.contains (hit->state))
                    selectedNotes.removeFirstMatchingValue (hit->state);
                else
                    selectedNotes.add (hit->state);
                selectedNote = selectedNotes.isEmpty() ? ValueTree() : selectedNotes.getLast();
                draggedNote = {};
                repaint();
                return;
            }
            if (! selectedNotes.contains (hit->state))
                selectOnlyNote (hit->state);
            selectedNote = draggedNote = hit->state;
            originalBeat = previewBeat = hit->getStartBeat().inBeats();
            originalLength = previewLength = hit->getLengthBeats().inBeats();
            originalPitch = previewPitch = hit->getNoteNumber();
            const auto bounds = noteBounds (*hit);
            const int handle = jmin (7, jmax (2, bounds.getWidth() / 3));
            const bool trimLeft = point.x < bounds.getX() + handle;
            const bool trimRight = point.x >= bounds.getRight() - handle;
            gesture = trimLeft ? Gesture::resizeStart : trimRight ? Gesture::resizeEnd : Gesture::move;
            audition (hit->getNoteNumber(), hit->getVelocity());
        }
        else if (tool.getSelectedId() == 2)
        {
            originalBeat = previewBeat = snap (beatAt (point.x));
            originalLength = previewLength = jmax (1.0 / 48.0, gridBeats());
            originalPitch = previewPitch = pitchAt (point.y);
            gesture = Gesture::draw;
            selectOnlyNote ({});
        }
        else
        {
            if (! event.mods.isShiftDown())
                selectOnlyNote ({});
            lassoInitialSelection = selectedNotes;
            lassoBounds = {};
            gesture = Gesture::lasso;
        }
    }
    repaint();
}

void StudioPianoRoll::mouseMove (const MouseEvent& event)
{
    if (! editingEnabled || clip == nullptr)
    {
        setMouseCursor (MouseCursor::NormalCursor);
        return;
    }
    if (rulerArea.contains (event.getPosition()))
    {
        setMouseCursor (MouseCursor::PointingHandCursor);
        return;
    }
    if (auto* hit = noteAt (event.getPosition()))
    {
        const auto bounds = noteBounds (*hit);
        const int handle = jmin (7, jmax (2, bounds.getWidth() / 3));
        if (event.x < bounds.getX() + handle || event.x >= bounds.getRight() - handle)
        {
            setMouseCursor (MouseCursor::LeftRightResizeCursor);
            return;
        }
    }
    setMouseCursor (MouseCursor::NormalCursor);
}

void StudioPianoRoll::mouseExit (const MouseEvent&)
{
    setMouseCursor (MouseCursor::NormalCursor);
}

void StudioPianoRoll::mouseDrag (const MouseEvent& event)
{
    if (draggingKeyboard)
    {
        const int nextPitch = pitchAt (event.getPosition().y);
        if (nextPitch != keyboardPreviewPitch)
        {
            keyboardPreviewPitch = nextPitch;
            audition (nextPitch, 100);
        }
        return;
    }
    if (gesture == Gesture::none)
        return;
    const int x = event.getPosition().x, y = event.getPosition().y;
    if (gesture == Gesture::lasso)
    {
        lassoBounds = { jmin (startX, x), jmin (startY, y),
                        std::abs (x - startX), std::abs (y - startY) };
        selectedNotes = lassoInitialSelection;
        if (clip != nullptr)
            for (auto* note : clip->getSequence().getNotes())
                if (noteBounds (*note).intersects (lassoBounds))
                    selectedNotes.addIfNotAlreadyThere (note->state);
        selectedNote = selectedNotes.isEmpty() ? ValueTree() : selectedNotes.getLast();
        repaint();
        return;
    }
    // A mostly vertical drag near a trim handle means "change pitch".
    // This makes short notes easy to transpose without accidental trimming.
    if ((gesture == Gesture::resizeStart || gesture == Gesture::resizeEnd)
        && std::abs (y - startY) > rowHeight / 2
        && std::abs (y - startY) > 2 * std::abs (x - startX))
    {
        gesture = Gesture::move;
        previewLength = originalLength;
    }
    const auto delta = (x - startX) / pixelsPerBeat;
    const auto step = jmax (1.0 / 48.0, gridBeats());
    switch (gesture)
    {
        case Gesture::draw:
            previewLength = jmax (step, snap (beatAt (x)) - originalBeat + step);
            break;
        case Gesture::move:
            previewBeat = snap (jmax (0.0, originalBeat + delta));
            if (selectedNotes.size() > 1 && clip != nullptr)
            {
                double earliest = originalBeat;
                int lowest = originalPitch, highest = originalPitch;
                for (auto state : selectedNotes)
                    if (auto* note = clip->getSequence().getNoteFor (state))
                    {
                        earliest = jmin (earliest, note->getStartBeat().inBeats());
                        lowest = jmin (lowest, note->getNoteNumber());
                        highest = jmax (highest, note->getNoteNumber());
                    }
                previewBeat = jmax (previewBeat, originalBeat - earliest);
                const int pitchDelta = jlimit (-lowest, 127 - highest,
                                              roundToInt ((startY - y) / (double) rowHeight));
                if (const int newPitch = originalPitch + pitchDelta; previewPitch != newPitch)
                {
                    previewPitch = newPitch;
                    audition (newPitch, selected() != nullptr ? selected()->getVelocity() : 100);
                }
                break;
            }
            if (const auto pitch = jlimit (0, 127,
                    originalPitch + roundToInt ((startY - y) / (double) rowHeight));
                pitch != previewPitch)
            {
                previewPitch = pitch;
                audition (pitch, selected() != nullptr ? selected()->getVelocity() : 100);
            }
            break;
        case Gesture::resizeStart:
            previewBeat = jlimit (0.0, jmax (0.0, originalBeat + originalLength - step),
                                  snap (originalBeat + delta));
            previewLength = originalLength + originalBeat - previewBeat;
            break;
        case Gesture::resizeEnd:
            previewLength = jmax (step, snap (originalBeat + originalLength + delta) - originalBeat);
            break;
        case Gesture::velocity:
            previewVelocity = velocityAt (y);
            break;
        default: break;
    }
    repaint();
}

void StudioPianoRoll::mouseUp (const MouseEvent& event)
{
    if (draggingKeyboard)
    {
        draggingKeyboard = false;
        keyboardPreviewPitch = -1;
        // Let the short audition timer release this quick tap audibly.
        return;
    }
    if (gesture == Gesture::none || clip == nullptr || ! editingEnabled)
        return;
    if (gesture == Gesture::lasso)
    {
        mouseDrag (event);
        gesture = Gesture::none;
        lassoBounds = {};
        lassoInitialSelection.clear();
        repaint();
        return;
    }
    const bool dragged = std::abs (event.getPosition().x - startX) > 2
                      || std::abs (event.getPosition().y - startY) > 2;
    if (dragged || gesture == Gesture::draw || gesture == Gesture::velocity)
        mouseDrag (event);
    if (! dragged && (gesture == Gesture::move || gesture == Gesture::resizeStart || gesture == Gesture::resizeEnd))
    {
        gesture = Gesture::none;
        draggedNote = {};
        repaint();
        return;
    }
    auto* um = clip->getUndoManager();
    if (um != nullptr)
        um->beginNewTransaction ("Edit MIDI notes");

    if (gesture == Gesture::draw)
    {
        if (auto* note = clip->getSequence().addNote (previewPitch,
                te::BeatPosition::fromBeats (previewBeat),
                te::BeatDuration::fromBeats (previewLength), 100, 0, um))
            selectOnlyNote (note->state);
    }
    else if (auto* note = clip->getSequence().getNoteFor (draggedNote))
    {
        if (gesture == Gesture::move && selectedNotes.size() > 1)
        {
            const double beatDelta = previewBeat - originalBeat;
            const int pitchDelta = previewPitch - originalPitch;
            for (auto state : selectedNotes)
                if (auto* chosen = clip->getSequence().getNoteFor (state))
                {
                    chosen->setStartAndLength (te::BeatPosition::fromBeats (
                        chosen->getStartBeat().inBeats() + beatDelta), chosen->getLengthBeats(), um);
                    chosen->setNoteNumber (chosen->getNoteNumber() + pitchDelta, um);
                }
        }
        else if (gesture == Gesture::move || gesture == Gesture::resizeStart || gesture == Gesture::resizeEnd)
        {
            note->setStartAndLength (te::BeatPosition::fromBeats (previewBeat),
                                     te::BeatDuration::fromBeats (previewLength), um);
            if (gesture == Gesture::move)
                note->setNoteNumber (previewPitch, um);
        }
        else if (gesture == Gesture::velocity)
            for (auto state : selectedNotes)
                if (auto* chosen = clip->getSequence().getNoteFor (state))
                    chosen->setVelocity (jlimit (1, 127,
                        chosen->getVelocity() + previewVelocity - originalVelocity), um);
    }
    gesture = Gesture::none;
    draggedNote = {};
    refresh();
}

void StudioPianoRoll::deleteSelected()
{
    if (clip == nullptr || ! editingEnabled)
        return;
    if (! selectedNotes.isEmpty())
    {
        auto* um = clip->getUndoManager();
        if (um != nullptr)
            um->beginNewTransaction ("Delete MIDI notes");
        const auto toRemove = selectedNotes;
        selectOnlyNote ({});
        for (auto state : toRemove)
            if (auto* note = clip->getSequence().getNoteFor (state))
                clip->getSequence().removeNote (*note, um);
        refresh();
    }
}

void StudioPianoRoll::quantizeNotes()
{
    if (clip == nullptr || ! editingEnabled || gridBeats() <= 0.0)
        return;
    auto* um = clip->getUndoManager();
    if (um != nullptr)
        um->beginNewTransaction ("Quantize MIDI notes");
    const bool hasSelection = ! selectedNotes.isEmpty();
    for (auto* note : clip->getSequence().getNotes())
    {
        if (hasSelection && ! selectedNotes.contains (note->state))
            continue;
        const auto start = snap (note->getStartBeat().inBeats());
        const auto end = snap (note->getEndBeat().inBeats());
        note->setStartAndLength (te::BeatPosition::fromBeats (start),
                                 te::BeatDuration::fromBeats (jmax (gridBeats(), end - start)), um);
    }
    refresh();
}

bool StudioPianoRoll::keyPressed (const KeyPress& key)
{
    if (! editingEnabled)
        return false;
    if (key.getKeyCode() == KeyPress::escapeKey)
    {
        selectOnlyNote ({});
        repaint();
        return true;
    }
    if (key.getKeyCode() == KeyPress::deleteKey || key.getKeyCode() == KeyPress::backspaceKey)
    {
        deleteSelected();
        return true;
    }
    if (key.getModifiers().isCommandDown())
    {
        if (key.getKeyCode() == 'a' || key.getKeyCode() == 'A')
        {
            if (clip != nullptr)
            {
                selectedNotes.clear();
                for (auto* note : clip->getSequence().getNotes())
                    selectedNotes.add (note->state);
                selectedNote = selectedNotes.isEmpty() ? ValueTree() : selectedNotes.getLast();
                repaint();
            }
            return true;
        }
        if (key.getKeyCode() == 'z' || key.getKeyCode() == 'Z')
        {
            if (clip != nullptr)
            {
                if (key.getModifiers().isShiftDown()) clip->edit.redo();
                else clip->edit.undo();
                refresh();
            }
            return true;
        }
    }
    return false;
}

void StudioPianoRoll::mouseWheelMove (const MouseEvent& event, const MouseWheelDetails& wheel)
{
    if (event.mods.isCommandDown())
        setZoom (pixelsPerBeat * (wheel.deltaY > 0 ? 1.12 : 1.0 / 1.12));
    else if (event.mods.isShiftDown() || std::abs (wheel.deltaX) > std::abs (wheel.deltaY))
        horizontal.setCurrentRangeStart (horizontal.getCurrentRangeStart()
                                         - (wheel.deltaX != 0 ? wheel.deltaX : wheel.deltaY) * 4.0);
    else
        vertical.setCurrentRangeStart (vertical.getCurrentRangeStart() - wheel.deltaY * rowHeight * 5.0);
}
