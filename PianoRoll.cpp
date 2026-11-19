// SPDX-License-Identifier: GPL-3.0-or-later
#include "PianoRoll.h"
#include <cmath>

using namespace juce;

StudioPianoRoll::StudioPianoRoll()
{
    setWantsKeyboardFocus (true);
    for (Component* c : std::initializer_list<Component*> { &tool, &grid, &quantize, &undo, &redo,
                                                              &zoomIn, &zoomOut, &caption,
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
    grid.setSelectedId (4, dontSendNotification);
    grid.setTooltip ("Snap grid, in musical note values");
    caption.setColour (Label::textColourId, Colour (0xffdce5ed));
    caption.setFont (FontOptions (13.0f));
    horizontal.addListener (this);
    vertical.addListener (this);

    zoomIn.onClick = [this] { setZoom (pixelsPerBeat * 1.25); };
    zoomOut.onClick = [this] { setZoom (pixelsPerBeat / 1.25); };
    undo.onClick = [this] { if (clip) clip->edit.undo(); refresh(); };
    redo.onClick = [this] { if (clip) clip->edit.redo(); refresh(); };
    quantize.onClick = [this] { quantizeNotes(); };
    refresh();
}

StudioPianoRoll::~StudioPianoRoll()
{
    horizontal.removeListener (this);
    vertical.removeListener (this);
}

void StudioPianoRoll::setClip (te::MidiClip::Ptr next)
{
    if (clip == next)
        return;
    gesture = Gesture::none;
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
                                                              &zoomIn, &zoomOut })
        c->setEnabled (hasClip && (editingEnabled || c == &zoomIn || c == &zoomOut));
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
    grid.setBounds (controls.removeFromLeft (95).reduced (1));
    quantize.setBounds (controls.removeFromLeft (85).reduced (1));
    undo.setBounds (controls.removeFromLeft (58).reduced (1));
    redo.setBounds (controls.removeFromLeft (58).reduced (1));
    zoomOut.setBounds (controls.removeFromRight (30).reduced (1));
    zoomIn.setBounds (controls.removeFromRight (30).reduced (1));
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
    horizontal.setSingleStepSize (jmax (0.125, gridBeats()));

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
    const auto step = jmax (0.125, gridBeats());
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
        g.setColour (active ? Colour (0xffedc37b) : Colour (0xff75b9c8));
        g.fillRoundedRectangle (bounds.toFloat(), 2.0f);
        g.setColour (active ? Colour (0xfffbe6b9) : Colour (0xff9cdee5));
        g.drawRoundedRectangle (bounds.toFloat().reduced (0.5f), 2.0f, 1.0f);
        if (bounds.getWidth() >= 28)
        {
            g.setColour (Colour (0xff1c2832));
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
        g.setColour (note->state == selectedNote ? Colour (0xffedc37b) : Colour (0xff75b9c8));
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
            const bool resizeHandle = point.x >= noteBounds (*hit).getRight() - 7;
            gesture = resizeHandle ? Gesture::resize : Gesture::move;
        }
        else if (tool.getSelectedId() == 2)
        {
            originalBeat = previewBeat = snap (beatAt (point.x));
            originalLength = previewLength = jmax (0.125, gridBeats());
            originalPitch = previewPitch = pitchAt (point.y);
            gesture = Gesture::draw;
            selectedNote = {};
        }
        else
            selectedNote = {};
    }
    repaint();
}

void StudioPianoRoll::mouseDrag (const MouseEvent& event)
{
    if (gesture == Gesture::none)
        return;
    const int x = event.getPosition().x, y = event.getPosition().y;
    const auto delta = (x - startX) / pixelsPerBeat;
    const auto step = jmax (0.125, gridBeats());
    switch (gesture)
    {
        case Gesture::draw:
            previewLength = jmax (step, snap (beatAt (x)) - originalBeat + step);
            break;
        case Gesture::move:
            previewBeat = snap (jmax (0.0, originalBeat + delta));
            previewPitch = jlimit (0, 127, originalPitch + roundToInt ((startY - y) / (double) rowHeight));
            break;
        case Gesture::resize:
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
    if (gesture == Gesture::none || clip == nullptr || ! editingEnabled)
        return;
    const bool dragged = std::abs (event.getPosition().x - startX) > 2
                      || std::abs (event.getPosition().y - startY) > 2;
    if (dragged || gesture == Gesture::draw || gesture == Gesture::velocity)
        mouseDrag (event);
    if (! dragged && (gesture == Gesture::move || gesture == Gesture::resize))
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
        if (gesture == Gesture::move || gesture == Gesture::resize)
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
