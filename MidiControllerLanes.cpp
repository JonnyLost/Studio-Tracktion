// SPDX-License-Identifier: GPL-3.0-or-later
#include "MidiControllerLanes.h"
#include "StudioUI.h"
#include <cmath>
#include <limits>

using namespace juce;
namespace te = tracktion;

namespace studio
{
MidiControllerLanes::MidiControllerLanes()
{
    setTitle ("MIDI controller lanes");
    setDescription ("Select an event or choose Draw in the Piano Roll. Shift-click adds to the selection.");
    setWantsKeyboardFocus (true);
}

void MidiControllerLanes::setClip (te::MidiClip* next)
{
    clip = next;
    selection.clear();
    cursorBeat.reset();
    feedback.clear();
    clearGesture();
    repaint();
}

void MidiControllerLanes::setEditingEnabled (bool enabled)
{
    if (allowed == enabled) return;
    allowed = enabled;
    if (! allowed) clearGesture();
    repaint();
}

void MidiControllerLanes::showMenu (Component& anchor)
{
    PopupMenu menu;
    menu.addSectionHeader ("Visible controller lanes");
    for (size_t i = 0; i < specs.size(); ++i)
        menu.addItem (specs[i].label, true, visible[i], [safe = SafePointer<MidiControllerLanes> (this), i]
        {
            if (! safe) return;
            safe->visible[i] = ! safe->visible[i];
            safe->clearGesture();
            if (safe->active == (int) i && ! safe->visible[i]) safe->selectLane (-1);
            if (safe->onLayoutChange) safe->onLayoutChange();
        });
    menu.showMenuAsync (PopupMenu::Options().withTargetComponent (&anchor));
}

int MidiControllerLanes::midiChannel() const
{
    return clip != nullptr && clip->hasValidSequence() ? clip->getMidiChannel().getChannelNumber() : 0;
}

bool MidiControllerLanes::canEdit() const
{
    return allowed && clip != nullptr && clip->hasValidSequence() && ! clip->getMPEMode()
        && matchingClipChannel (midiChannel(), midiChannel());
}

int MidiControllerLanes::laneHeight() const
{
    return jmax (appearance.px (67), 3 * appearance.line (11, 2));
}

int MidiControllerLanes::preferredHeight() const
{
    return (int) std::count (visible.begin(), visible.end(), true) * laneHeight();
}

Rectangle<int> MidiControllerLanes::laneBounds (int index) const
{
    const int row = (int) std::count (visible.begin(), visible.begin() + index, true);
    return { 0, row * laneHeight(), getWidth(), laneHeight() };
}

int MidiControllerLanes::laneAt (int y) const
{
    for (int i = 0; i < (int) specs.size(); ++i)
        if (visible[(size_t) i] && laneBounds (i).contains (0, y)) return i;
    return -1;
}

int MidiControllerLanes::yFor (Rectangle<int> row, int lane, int stored) const
{
    const auto range = specs[(size_t) lane].format;
    int value = decodeExpressionValue (range, stored);
    if (range == ExpressionValue::switch64) value = value >= 64 ? 127 : 0;
    row = row.reduced (0, appearance.px (7));
    return row.getBottom() - roundToInt ((float) row.getHeight() * (float) value / (float) expressionMaximum (range));
}

int MidiControllerLanes::valueAt (Rectangle<int> row, int lane, int y) const
{
    const auto range = specs[(size_t) lane].format;
    row = row.reduced (0, appearance.px (7));
    return encodeExpressionValue (range, roundToInt ((float) (row.getBottom() - y)
                       * (float) expressionMaximum (range) / (float) jmax (1, row.getHeight())));
}

bool MidiControllerLanes::beatInClip (double beat) const
{
    if (clip == nullptr || ! std::isfinite (beat) || beat < 0.0) return false;
    const auto pos = te::BeatPosition::fromBeats (beat);
    if (clip->isLooping())
        return pos >= clip->getLoopStartBeats() && pos < clip->getLoopStartBeats() + clip->getLoopLengthBeats();
    const auto time = clip->getTimeOfContentBeat (pos);
    return time >= clip->getPosition().getStart() && time < clip->getPosition().getEnd();
}

te::MidiControllerEvent* MidiControllerLanes::find (const ValueTree& state) const
{
    if (clip != nullptr && clip->hasValidSequence() && state.isValid())
        for (auto* event : clip->getSequence().getControllerEvents())
            if (event->state == state) return event;
    return nullptr;
}

void MidiControllerLanes::setPlayhead (std::optional<int> x)
{
    if (playheadX != x)
    {
        const auto old = playheadX;
        playheadX = x;
        if (old) repaint (*old - 1, 0, 3, getHeight());
        if (x) repaint (*x - 1, 0, 3, getHeight());
    }
}

String MidiControllerLanes::describe (int lane, int stored) const
{
    const auto range = specs[(size_t) lane].format;
    const int value = decodeExpressionValue (range, stored);
    if (range == ExpressionValue::switch64)
        return String (value >= 64 ? "On" : "Off") + " (" + String (value) + ")";
    return String (range == ExpressionValue::pitch14 ? value - 8192 : value);
}

void MidiControllerLanes::paint (Graphics& g)
{
    const auto& m = appearance;
    for (int i = 0; i < (int) specs.size(); ++i)
    {
        if (! visible[(size_t) i]) continue;
        const auto row = laneBounds (i);
        if (! g.clipRegionIntersects (row)) continue;
        g.setColour (i == active ? colours::selected : colours::control);
        g.fillRect (row.withWidth (gutter));
        g.setColour (colours::canvas);
        const auto plot = row.withTrimmedLeft (gutter);
        g.fillRect (plot);
        g.setColour (colours::divider);
        g.drawHorizontalLine (row.getBottom() - 1, 0.0f, (float) row.getRight());
        if (i == active)
        {
            g.setColour (colours::focus);
            g.fillRect (row.withWidth (m.px (2)));
        }
        g.setFont (uiFont (11.0f, Font::bold));
        auto label = row.withWidth (gutter).reduced (m.px (4), m.px (3));
        g.setColour (colours::text);
        g.drawFittedText (specs[(size_t) i].label, label.removeFromTop (2 * m.line (11, 0)),
                          Justification::centredLeft, 2, 1.0f);
        g.setFont (uiFont (11.0f));
        auto value = String ("Ch ") + (midiChannel() > 0 ? String (midiChannel()) : String ("--"));
        if (i == active)
        {
            if (moving || drawing) value = describe (i, preview.stored);
            else if (auto* event = find (selection.isEmpty() ? ValueTree() : selection.getLast()))
                value = describe (i, event->getControllerValue());
        }
        g.drawFittedText (value, label, Justification::centredLeft, 1, 1.0f);
        if (clip == nullptr || ! clip->hasValidSequence()) continue;

        Graphics::ScopedSaveState clipped (g);
        g.reduceClipRegion (plot);
        if (clip->getMPEMode())
        {
            g.setColour (colours::secondary);
            g.drawFittedText ("MPE controller data is read-only in Studio", plot.reduced (m.px (8)),
                              Justification::centredLeft, 1, 1.0f);
        }
        std::optional<Point<int>> previous;
        for (auto* event : clip->getSequence().getControllerEvents())
        {
            if (event->getType() != specs[(size_t) i].midiType) continue;
            const auto raw = event->getControllerValue();
            const int x = xForBeat (event->getBeatPosition().inBeats()), y = yFor (row, i, raw);
            if (previous)
            {
                g.setColour (colours::action.withAlpha (0.42f));
                g.drawHorizontalLine (previous->y, (float) jmax (plot.getX(), previous->x),
                                      (float) jmin (plot.getRight(), x));
                if (x >= plot.getX() && x < plot.getRight())
                    g.drawVerticalLine (x, (float) jmin (previous->y, y), (float) jmax (previous->y, y));
            }
            if (x >= plot.getRight()) break;
            previous = Point<int> { x, y };
            if (! plot.contains (x, y)) continue;
            g.setColour (selection.contains (event->state) && active == i ? colours::focus : colours::action);
            const float dot = (float) m.px (3);
            g.fillEllipse ((float) x - dot, (float) y - dot, dot * 2, dot * 2);
        }
        if (previous)
        {
            g.setColour (colours::action.withAlpha (0.42f));
            g.drawHorizontalLine (previous->y, (float) jmax (plot.getX(), previous->x), (float) plot.getRight());
        }
        if (i == active && (moving || drawing))
        {
            const auto drawPoint = [&] (DraftPoint point)
            {
                const int x = xForBeat (point.beat), y = yFor (row, i, point.stored);
                g.fillEllipse ((float) x - 3.0f, (float) y - 3.0f, 6.0f, 6.0f);
            };
            g.setColour (colours::focus);
            if (drawing)
                for (auto point : stroke) drawPoint (point);
            else
                drawPoint (preview);
        }
        if (playheadX)
        {
            g.setColour (colours::playhead);
            g.drawVerticalLine (*playheadX, (float) row.getY(), (float) row.getBottom());
        }
        if (i == active && feedback.isNotEmpty())
        {
            auto banner = plot.reduced (m.px (4), 0).withHeight (m.line (11, 2));
            banner = banner.removeFromRight (jmin (banner.getWidth(), m.span (265)));
            g.setColour (colours::panel);
            g.fillRect (banner);
            g.setFont (uiFont (11.0f));
            g.setColour (colours::text);
            g.drawFittedText (feedback, banner.reduced (m.px (4), 0), Justification::centredRight, 1, 1.0f);
        }
    }
}

void MidiControllerLanes::clearGesture()
{
    moving = drawing = false;
    dragged = {};
    stroke.clear();
}

void MidiControllerLanes::selectLane (int lane)
{
    if (active == lane) return;
    active = lane;
    selection.clear();
}

void MidiControllerLanes::mouseDown (const MouseEvent& event)
{
    if (! event.mods.isLeftButtonDown()) return;
    const int lane = laneAt (event.y);
    if (lane < 0) return;
    grabKeyboardFocus();
    feedback.clear();
    clearGesture();
    selectLane (lane);
    if (event.x < gutter || ! canEdit()) { repaint(); return; }
    origin = event.getPosition();
    cursorBeat = snapBeat (beatForX (event.x));
    auto nearest = appearance.px (9);
    te::MidiControllerEvent* hit = nullptr;
    const auto row = laneBounds (lane);
    for (auto* candidate : clip->getSequence().getControllerEvents())
    {
        if (candidate->getType() != specs[(size_t) lane].midiType) continue;
        const auto pos = Point<int> (xForBeat (candidate->getBeatPosition().inBeats()),
                                    yFor (row, lane, candidate->getControllerValue()));
        const auto distance = pos.getDistanceFrom (origin);
        if (distance < nearest) { nearest = distance; hit = candidate; }
    }
    if (hit)
    {
        if (! event.mods.isShiftDown() && ! selection.contains (hit->state)) selection.clear();
        if (event.mods.isShiftDown() && selection.contains (hit->state)) selection.removeFirstMatchingValue (hit->state);
        else selection.addIfNotAlreadyThere (hit->state);
        if (! event.mods.isShiftDown())
        {
            dragged = hit->state;
            preview = { hit->getBeatPosition().inBeats(), hit->getControllerValue() };
            moving = true;
        }
    }
    else
    {
        if (! event.mods.isShiftDown()) selection.clear();
        if (drawMode() && beatInClip (*cursorBeat))
        {
            drawing = true;
            lastSample = origin;
            addPoint (origin);
        }
    }
    repaint();
}

void MidiControllerLanes::addPoint (Point<int> point)
{
    const double beat = snapBeat (beatForX (point.x));
    if (! beatInClip (beat)) return;
    preview = { beat, valueAt (laneBounds (active), active, point.y) };
    if (auto existing = std::find_if (stroke.begin(), stroke.end(),
        [beat, this] (const DraftPoint& p) { return std::abs (xForBeat (p.beat) - xForBeat (beat)) <= appearance.px (2); });
        existing != stroke.end())
        existing->stored = preview.stored;
    else if (stroke.size() < 256)
        stroke.push_back (preview);
}

void MidiControllerLanes::mouseDrag (const MouseEvent& event)
{
    if (! canEdit()) return;
    if (moving)
    {
        auto* original = find (dragged);
        if (! original) { clearGesture(); return; }
        const auto candidate = snapBeat (original->getBeatPosition().inBeats()
                                       + beatForX (event.x) - beatForX (origin.x));
        if (std::abs (event.x - origin.x) >= appearance.px (2) && beatInClip (candidate))
            preview.beat = candidate;
        else if (std::abs (event.x - origin.x) < appearance.px (2))
            preview.beat = original->getBeatPosition().inBeats();
        preview.stored = std::abs (event.y - origin.y) < appearance.px (2) ? original->getControllerValue()
                         : valueAt (laneBounds (active), active, event.y);
    }
    else if (drawing)
    {
        const auto next = event.getPosition();
        if (specs[(size_t) active].format == ExpressionValue::switch64)
            stroke.clear();
        else
        {
            const int intervals = jlimit (1, 32, std::abs (next.x - lastSample.x) / appearance.px (6));
            for (int i = 1; i < intervals; ++i)
                addPoint (lastSample + (next - lastSample) * (i / (float) intervals));
        }
        addPoint (next);
        lastSample = next;
    }
    repaint();
}

void MidiControllerLanes::mouseUp (const MouseEvent&)
{
    if (! canEdit() || (! moving && ! drawing)) return;
    auto* um = clip->getUndoManager();
    auto& sequence = clip->getSequence();
    if (moving)
    {
        if (auto* event = find (dragged);
            event && (event->getControllerValue() != preview.stored || event->getBeatPosition().inBeats() != preview.beat))
        {
            if (um) um->beginNewTransaction ("Edit MIDI controller");
            if (event->getBeatPosition().inBeats() != preview.beat)
                event->setBeatPosition (te::BeatPosition::fromBeats (preview.beat), um);
            if (event->getControllerValue() != preview.stored)
                event->setControllerValue (preview.stored, um);
            if (um) um->beginNewTransaction();
        }
    }
    else if (! stroke.empty())
    {
        if (um) um->beginNewTransaction ("Draw MIDI controller");
        for (const auto point : stroke)
            if (auto* event = sequence.addControllerEvent (te::BeatPosition::fromBeats (point.beat),
                    specs[(size_t) active].midiType, point.stored, um))
                selection.add (event->state);
        if (um) um->beginNewTransaction();
    }
    clearGesture();
    if (onChange) onChange();
    repaint();
}

void MidiControllerLanes::editSelection (bool remove)
{
    if (! canEdit() || selection.isEmpty()) return;
    auto* um = clip->getUndoManager();
    if (remove)
    {
        if (um) um->beginNewTransaction ("Delete MIDI controllers");
        for (const auto& state : selection)
            if (auto* event = find (state)) clip->getSequence().removeControllerEvent (*event, um);
        if (um) um->beginNewTransaction();
        selection.clear();
        if (onChange) onChange();
        return;
    }
    clipboard.clear();
    clipboardChannel = midiChannel();
    double firstBeat = std::numeric_limits<double>::max();
    for (const auto& state : selection)
        if (const auto* event = find (state))
        {
            const auto beat = event->getBeatPosition().inBeats();
            firstBeat = jmin (firstBeat, beat);
            clipboard.push_back ({ state.createCopy(), beat });
        }
    for (auto& point : clipboard) point.relativeBeat -= firstBeat;
    if (! clipboard.empty()) feedback = "Copied " + String ((int) clipboard.size()) + " on channel " + String (clipboardChannel);
}

void MidiControllerLanes::paste()
{
    if (! canEdit() || clipboard.empty()) return;
    if (! matchingClipChannel (clipboardChannel, midiChannel()))
    {
        feedback = "Paste requires MIDI channel " + String (clipboardChannel);
        return;
    }
    const auto anchor = cursorBeat.value_or (snapBeat (clip->getContentBeatAtTime (clip->edit.getTransport().getPosition()).inBeats()));
    if (std::any_of (clipboard.begin(), clipboard.end(),
                     [&] (const ClipboardEvent& point) { return ! beatInClip (anchor + point.relativeBeat); }))
    {
        feedback = "Pasted points must fit inside the clip";
        return;
    }
    auto* um = clip->getUndoManager();
    if (um) um->beginNewTransaction ("Paste MIDI controllers");
    selection.clear();
    for (const auto& point : clipboard)
    {
        te::MidiControllerEvent copy (point.state.createCopy());
        copy.setBeatPosition (te::BeatPosition::fromBeats (anchor + point.relativeBeat), nullptr);
        if (auto* event = clip->getSequence().addControllerEvent (copy, um))
        {
            for (int i = 0; i < (int) specs.size(); ++i)
                if (specs[(size_t) i].midiType == event->getType())
                {
                    if (visible[(size_t) i])
                    {
                        selectLane (i);
                        selection.add (event->state);
                    }
                    break;
                }
        }
    }
    if (um) um->beginNewTransaction();
    feedback = "Pasted " + String ((int) clipboard.size()) + " on channel " + String (clipboardChannel);
    if (onChange) onChange();
}

bool MidiControllerLanes::keyPressed (const KeyPress& key)
{
    if (key.getModifiers().isAltDown()) return false;
    const auto code = key.getKeyCode();
    if (code == KeyPress::escapeKey) { clearGesture(); selection.clear(); feedback.clear(); }
    else if (code == KeyPress::deleteKey || code == KeyPress::backspaceKey) editSelection (true);
    else if (key.getModifiers().isCommandDown() && (code == 'C' || code == 'c')) editSelection (false);
    else if (key.getModifiers().isCommandDown() && (code == 'V' || code == 'v')) paste();
    else if (key.getModifiers().isCommandDown() && (code == 'A' || code == 'a'))
    {
        selection.clear();
        if (canEdit() && active >= 0)
            for (auto* event : clip->getSequence().getControllerEvents())
                if (event->getType() == specs[(size_t) active].midiType) selection.add (event->state);
    }
    else
        return false;
    repaint();
    return true;
}
}
