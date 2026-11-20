// SPDX-License-Identifier: GPL-3.0-or-later
#include "PianoRoll.h"
#include <cmath>

using namespace juce;

StudioPianoRoll::StudioPianoRoll()
{
    setWantsKeyboardFocus (true);
    for (Component* c : std::initializer_list<Component*> { &tool, &grid, &quantize, &undo, &redo,
                                                              &zoomIn, &zoomOut, &verticalZoomIn,
                                                              &verticalZoomOut, &caption,
                                                              &horizontal, &vertical })
        addAndMakeVisible (c);

    tool.addItem ("Select", 1);
    tool.addItem ("Draw", 2);
    tool.setSelectedId (1, dontSendNotification);
    tool.setTooltip ("Select notes to move or resize; Draw places new notes");
    grid.addItem ("Snap off", 1);
    grid.addItem ("1/4", 2);
    grid.addItem ("1/8", 3);
    grid.addItem ("1/16", 4);
    grid.addItem ("1/32", 5);
    grid.addItem ("1/8 T", 6);
    grid.addItem ("1/16 T", 7);
    grid.addItem ("1/32 T", 8);
    grid.setSelectedId (4, dontSendNotification);
    grid.setTooltip ("Snap grid, in musical note values");
    caption.setColour (Label::textColourId, Colour (0xffdce5ed));
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
    draggedNote = {};
    clip = std::move (next);
    horizontal.setCurrentRangeStart (0, dontSendNotification);
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
    }
    editingEnabled = enabled;
    for (Component* c : std::initializer_list<Component*> { &tool, &grid, &quantize, &undo, &redo })
        c->setEnabled (enabled && clip != nullptr);
    repaint();
}

void StudioPianoRoll::refresh()
{
    const bool hasClip = clip != nullptr && clip->hasValidSequence();
    caption.setText (hasClip ? clip->getName() + "  •  " + String (clip->getSequence().getNumNotes()) + " notes"
                             : "Select a MIDI clip in the arrangement", dontSendNotification);
    for (Component* c : std::initializer_list<Component*> { &tool, &grid, &quantize, &undo, &redo,
                                                              &zoomIn, &zoomOut, &verticalZoomIn,
                                                              &verticalZoomOut })
        c->setEnabled (hasClip && (editingEnabled || c == &zoomIn || c == &zoomOut));
    verticalZoomIn.setEnabled (hasClip);
    verticalZoomOut.setEnabled (hasClip);
    updateScrollBars();
    repaint();
    if (onMidiChanged)
        onMidiChanged();
}

void StudioPianoRoll::resized()
{
    auto area = getLocalBounds().reduced (4, 2);
    auto controls = area.removeFromTop (27);
    tool.setBounds (controls.removeFromLeft (90).reduced (1));
    grid.setBounds (controls.removeFromLeft (102).reduced (1));
    quantize.setBounds (controls.removeFromLeft (85).reduced (1));
    undo.setBounds (controls.removeFromLeft (58).reduced (1));
    redo.setBounds (controls.removeFromLeft (58).reduced (1));
    zoomOut.setBounds (controls.removeFromRight (30).reduced (1));
    zoomIn.setBounds (controls.removeFromRight (30).reduced (1));
    verticalZoomOut.setBounds (controls.removeFromRight (30).reduced (1));
    verticalZoomIn.setBounds (controls.removeFromRight (30).reduced (1));
    caption.setBounds (controls);

    area.removeFromBottom (14);
    vertical.setBounds (area.removeFromRight (14));
    auto velocityRow = area.removeFromBottom (velocityHeight);
    velocityArea = velocityRow.withTrimmedLeft (keyWidth);
    auto keyboardAndGrid = area;
    keyboard = keyboardAndGrid.removeFromLeft (keyWidth);
    notesArea = keyboardAndGrid;
    horizontal.setBounds (getWidth() > keyWidth + 24
        ? Rectangle<int> (notesArea.getX(), getHeight() - 16, notesArea.getWidth(), 14)
        : Rectangle<int>());
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
        default: return 0.0;
    }
}

double StudioPianoRoll::snap (double beat) const
{
    const auto step = gridBeats();
    return jmax (0.0, step > 0.0 ? std::round (beat / step) * step : beat);
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

void StudioPianoRoll::updateScrollBars()
{
    double endBeat = 16.0;
    if (clip != nullptr && clip->hasValidSequence())
        for (auto* note : clip->getSequence().getNotes())
            endBeat = jmax (endBeat, note->getEndBeat().inBeats() + 4.0);

    const auto visibleBeats = notesArea.getWidth() / pixelsPerBeat;
    horizontal.setRangeLimits (0.0, jmax (endBeat, visibleBeats));
    horizontal.setCurrentRange (horizontal.getCurrentRangeStart(), visibleBeats, dontSendNotification);
    horizontal.setSingleStepSize (jmax (1.0 / 48.0, gridBeats()));

    vertical.setRangeLimits (0.0, 128.0 * rowHeight);
    vertical.setCurrentRange (vertical.getCurrentRangeStart(), notesArea.getHeight(), dontSendNotification);
    vertical.setSingleStepSize (rowHeight);
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
    repaint();
}

void StudioPianoRoll::paint (Graphics& g)
{
    g.fillAll (Colour (0xff1a2430));
    if (notesArea.isEmpty())
        return;
    paintGrid (g);
    paintNotes (g);
    paintVelocity (g);
}

void StudioPianoRoll::paintGrid (Graphics& g)
{
    g.setColour (Colour (0xff27343e));
    g.fillRect (keyboard);
    g.fillRect (notesArea);

    const auto topPitch = pitchAt (notesArea.getY());
    const auto bottomPitch = pitchAt (notesArea.getBottom());
    for (int pitch = topPitch; pitch >= bottomPitch; --pitch)
    {
        const int y = yAt (pitch);
        if (y + rowHeight < notesArea.getY() || y >= notesArea.getBottom())
            continue;
        const int semitone = pitch % 12;
        const bool black = semitone == 1 || semitone == 3 || semitone == 6 || semitone == 8 || semitone == 10;
        g.setColour (black ? Colour (0xff202b36) : Colour (0xff2a3742));
        g.fillRect (notesArea.getX(), y, notesArea.getWidth(), rowHeight - 1);
        g.setColour (black ? Colour (0xff111923) : Colour (0xffd5dde4));
        g.fillRect (keyboard.getX(), y, keyboard.getWidth() - 1, rowHeight - 1);
        if (pitch == auditionPitch)
        {
            g.setColour (Colour (0xffc5a56b).withAlpha (0.85f));
            g.fillRect (keyboard.getX(), y, keyboard.getWidth() - 1, rowHeight - 1);
        }
        if (pitch % 12 == 0)
        {
            g.setColour (Colour (0xff384957));
            g.drawHorizontalLine (y, (float) notesArea.getX(), (float) notesArea.getRight());
            g.setColour (black ? Colour (0xfff5f8fb) : Colour (0xff26333c));
            g.setFont (FontOptions (10.0f));
            g.drawText ("C" + String (pitch / 12 - 1), keyboard.getX() + 4, y, keyWidth - 7,
                        rowHeight, Justification::centredLeft);
        }
    }

    g.saveState();
    g.reduceClipRegion (notesArea);
    const auto step = gridBeats() > 0.0 ? gridBeats() : 0.25;
    const auto first = std::floor (horizontal.getCurrentRangeStart() / step) * step;
    const auto last = horizontal.getCurrentRangeStart() + notesArea.getWidth() / pixelsPerBeat;
    for (double beat = first; beat <= last; beat += step)
    {
        const int x = xAt (beat);
        const bool bar = std::abs (std::remainder (beat, 4.0)) < 0.0001;
        const bool quarter = std::abs (std::remainder (beat, 1.0)) < 0.0001;
        g.setColour (bar ? Colour (0xff7e8c9a) : quarter ? Colour (0xff50616f) : Colour (0xff394954));
        g.drawVerticalLine (x, (float) notesArea.getY(), (float) notesArea.getBottom());
    }
    g.restoreState();
}

void StudioPianoRoll::paintNotes (Graphics& g)
{
    if (clip == nullptr || ! clip->hasValidSequence())
        return;
    g.saveState();
    g.reduceClipRegion (notesArea);
    for (auto* note : clip->getSequence().getNotes())
    {
        auto bounds = noteBounds (*note);
        if (! bounds.intersects (notesArea))
            continue;
        if (gesture != Gesture::none && draggedNote == note->state)
            bounds = { xAt (previewBeat), yAt (previewPitch) + 1,
                       jmax (3, roundToInt (previewLength * pixelsPerBeat)), rowHeight - 2 };
        const bool active = note->state == selectedNote;
        const auto velocity = gesture == Gesture::velocity && draggedNote == note->state
                            ? previewVelocity : note->getVelocity();
        const auto fraction = jlimit (0.0f, 1.0f, velocity / 127.0f);
        // Walk the visible colour wheel: soft blue -> cyan -> green -> yellow -> hard red.
        const auto noteColour = Colour::fromHSV (0.64f * (1.0f - fraction), 0.86f, 0.9f, 1.0f);
        g.setColour (noteColour);
        g.fillRoundedRectangle (bounds.toFloat(), 2.0f);
        g.setColour (active ? Colours::white : noteColour.brighter (0.2f));
        g.drawRoundedRectangle (bounds.toFloat().reduced (0.5f), 2.0f, active ? 1.5f : 1.0f);
        if (active && bounds.getWidth() >= 18)
        {
            // Edge markers make the two trim targets discoverable without a manual.
            g.setColour (Colours::white.withAlpha (0.75f));
            g.fillRect (bounds.getX() + 2, bounds.getY() + 2, 2, bounds.getHeight() - 4);
            g.fillRect (bounds.getRight() - 4, bounds.getY() + 2, 2, bounds.getHeight() - 4);
        }
        if (bounds.getWidth() >= 28)
        {
            g.setColour (fraction < 0.3f ? Colours::white : Colour (0xff142330));
            g.setFont (FontOptions (10.0f));
            g.drawText (MidiMessage::getMidiNoteName (note->getNoteNumber(), true, true, 4),
                        bounds.reduced (3, 0), Justification::centredLeft);
        }
    }
    g.restoreState();
}

void StudioPianoRoll::paintVelocity (Graphics& g)
{
    g.setColour (Colour (0xff19222b));
    g.fillRect (velocityArea);
    g.setColour (Colour (0xff9bacba));
    g.setFont (FontOptions (11.0f));
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
        const int vel = gesture == Gesture::velocity && draggedNote == note->state
                      ? previewVelocity : note->getVelocity();
        const int h = roundToInt ((velocityArea.getHeight() - 8) * vel / 127.0);
        g.setColour (Colour::fromHSV (0.64f * (1.0f - vel / 127.0f), 0.86f, 0.9f, 1.0f));
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
            selectedNote = {};
        }
        else
            selectedNote = {};
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
    // A mostly vertical drag near a trim handle means "change pitch".
    // This makes short notes easy to transpose without accidental trimming.
    if ((gesture == Gesture::resizeStart || gesture == Gesture::resizeEnd)
        && std::abs (y - startY) > rowHeight / 2
        && std::abs (y - startY) > 2 * std::abs (x - startX))
        gesture = Gesture::move;
    const auto delta = (x - startX) / pixelsPerBeat;
    const auto step = jmax (1.0 / 48.0, gridBeats());
    switch (gesture)
    {
        case Gesture::draw:
            previewLength = jmax (step, snap (beatAt (x)) - originalBeat + step);
            break;
        case Gesture::move:
            previewBeat = snap (jmax (0.0, originalBeat + delta));
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
            selectedNote = note->state;
    }
    else if (auto* note = clip->getSequence().getNoteFor (draggedNote))
    {
        if (gesture == Gesture::move || gesture == Gesture::resizeStart || gesture == Gesture::resizeEnd)
        {
            note->setStartAndLength (te::BeatPosition::fromBeats (previewBeat),
                                     te::BeatDuration::fromBeats (previewLength), um);
            if (gesture == Gesture::move)
                note->setNoteNumber (previewPitch, um);
        }
        else if (gesture == Gesture::velocity)
            note->setVelocity (previewVelocity, um);
    }
    gesture = Gesture::none;
    draggedNote = {};
    refresh();
}

void StudioPianoRoll::deleteSelected()
{
    if (clip == nullptr || ! editingEnabled)
        return;
    if (auto* note = selected())
    {
        auto* um = clip->getUndoManager();
        if (um != nullptr)
            um->beginNewTransaction ("Delete MIDI note");
        clip->getSequence().removeNote (*note, um);
        selectedNote = {};
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
    const auto* chosen = selected();
    for (auto* note : clip->getSequence().getNotes())
    {
        if (chosen != nullptr && note != chosen)
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
    if (key.getKeyCode() == KeyPress::deleteKey || key.getKeyCode() == KeyPress::backspaceKey)
    {
        deleteSelected();
        return true;
    }
    if (key.getModifiers().isCommandDown())
    {
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
