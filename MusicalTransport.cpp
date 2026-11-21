// SPDX-License-Identifier: GPL-3.0-or-later
#include "MusicalTransport.h"
#include <cmath>

namespace studio
{
namespace te = tracktion;
using namespace juce;

bool transportSettled (te::Edit& edit)
{
    const auto& transport = edit.getTransport();
    return ! transport.isRecording() && ! transport.isStopping() && ! transport.isRecordingStopping();
}

bool setCycle (te::Edit& edit, te::TimeRange range)
{
    if (! transportSettled (edit) || range.getLength().inSeconds() < 0.12)
        return false;
    edit.getTransport().setLoopRange (range);
    edit.getTransport().looping = true;
    return true;
}

bool setTempoEvent (te::Edit& edit, te::BeatPosition beat, double bpm)
{
    if (! transportSettled (edit) || edit.getTransport().isPlaying()
        || ! std::isfinite (bpm) || bpm < te::TempoSetting::minBPM || bpm > te::TempoSetting::maxBPM)
        return false;

    auto& sequence = edit.tempoSequence;
    auto& previous = sequence.getTempoAt (beat);
    edit.getUndoManager().beginNewTransaction ("Set tempo event");
    const auto cycle = edit.getTransport().getLoopRange();
    if (previous.getStartBeat() == beat)
        previous.setBpm (bpm);
    else
    {
        te::EditTimecodeRemapperSnapshot snapshot;
        snapshot.savePreChangeState (edit);
        sequence.insertTempo (beat, bpm, 1.0f);
        snapshot.remapEdit (edit);
    }
    // Tracktion stores cycle endpoints in seconds outside project undo.
    edit.getTransport().setLoopRange (cycle);
    return true;
}

bool setMeterEvent (te::Edit& edit, te::BeatPosition beat, int numerator, int denominator)
{
    if (! transportSettled (edit) || edit.getTransport().isPlaying()
        || numerator < 1 || numerator > 32
        || (denominator != 2 && denominator != 4 && denominator != 8 && denominator != 16))
        return false;
    edit.getUndoManager().beginNewTransaction ("Set meter event");
    if (auto signature = edit.tempoSequence.insertTimeSig (beat))
    {
        signature->numerator = numerator;
        signature->denominator = denominator;
        return true;
    }
    return false;
}

te::MarkerClip::Ptr addCueMarker (te::Edit& edit, te::TimePosition time, const String& name)
{
    if (! transportSettled (edit) || name.trim().isEmpty())
        return {};
    edit.getUndoManager().beginNewTransaction ("Add cue marker");
    auto marker = edit.getMarkerManager().createMarker (-1, time, te::TimeDuration::fromSeconds (0.001),
                                                        te::Clip::syncBarsBeats, nullptr);
    if (marker != nullptr)
        marker->setName (name.trim());
    return marker;
}

MusicalTransport::MusicalTransport() : TextButton ("Music...")
{
    setTitle ("Musical transport and timeline");
    setTooltip ("Cycle, metronome, count-in, tempo, meter and cue markers");
    onClick = [this]
    {
        if (edit != nullptr)
            showMenu (edit->getTransport().getPosition(), true);
    };
}

void MusicalTransport::setEdit (te::Edit* next)
{
    ++session;
    edit = next;
    refresh (next != nullptr);
}

void MusicalTransport::refresh (bool available)
{
    setEnabled (available && edit != nullptr && transportSettled (*edit));
    const auto label = edit != nullptr && edit->getTransport().looping ? "Cycle on..."
                     : edit != nullptr && edit->clickTrackEnabled ? "Click on..." : "Music...";
    if (getButtonText() != label)
        setButtonText (label);
}

void MusicalTransport::showTimelineMenu (te::TimePosition time)
{
    showMenu (time, false);
}

void MusicalTransport::showMenu (te::TimePosition time, bool includeTransport)
{
    if (edit == nullptr || ! isEnabled() || ! transportSettled (*edit))
        return;

    SafePointer<MusicalTransport> safe (this);
    const auto generation = session;
    auto withEdit = [safe, generation] (auto action)
    {
        return [safe, generation, action]
        {
            if (safe != nullptr && safe->session == generation && safe->isEnabled()
                && transportSettled (*safe->edit))
                action (*safe, *safe->edit);
        };
    };

    PopupMenu menu;
    auto& transport = edit->getTransport();
    auto& sequence = edit->tempoSequence;
    const auto beat = te::roundToNearestBeat (sequence.toBeats (time));
    const auto bar = sequence.toBeats (te::tempo::BarsAndBeats { sequence.toBarsAndBeats (time).bars, {} });
    const bool stopped = ! transport.isPlaying();

    if (includeTransport)
    {
        menu.addItem ("Cycle playback", true, transport.looping, withEdit ([] (auto&, auto& project)
        {
            auto& t = project.getTransport();
            if (t.looping)
                t.looping = false;
            else if (t.getLoopRange().getLength().inSeconds() >= 0.12)
                t.looping = true;
            else
            {
                auto& tempo = project.tempoSequence;
                const auto currentBar = tempo.toBarsAndBeats (t.getPosition()).bars;
                setCycle (project, { tempo.toTime (te::tempo::BarsAndBeats { currentBar, {} }),
                                     tempo.toTime (te::tempo::BarsAndBeats { currentBar + 1, {} }) });
            }
        }));
        menu.addItem ("Metronome", true, edit->clickTrackEnabled, withEdit ([] (auto&, auto& project)
        {
            project.clickTrackEnabled = ! project.clickTrackEnabled.get();
        }));

        PopupMenu levels, counts;
        for (const int percent : { 20, 40, 60, 80, 100 })
            levels.addItem (String (percent) + "%", true, roundToInt (edit->getClickTrackVolume() * 100) == percent,
                            withEdit ([percent] (auto&, auto& project) { project.setClickTrackVolume (percent / 100.0f); }));
        using CountIn = te::Edit::CountIn;
        for (const auto& [label, count] : std::initializer_list<std::pair<const char*, CountIn>> {
                 { "Off", CountIn::none }, { "One beat", CountIn::oneBeat }, { "Two beats", CountIn::twoBeat },
                 { "One bar", CountIn::oneBar }, { "Two bars", CountIn::twoBar } })
            counts.addItem (label, stopped, edit->getCountInMode() == count,
                            withEdit ([count] (auto&, auto& project) { project.setCountInMode (count); }));
        menu.addSubMenu ("Metronome level", levels);
        menu.addSubMenu ("Recording count-in", counts);
        menu.addSeparator();
    }

    menu.addItem ("Set tempo at beat...", stopped, false,
                  withEdit ([beat] (auto& self, auto&) { self.editTempo (beat, false); }));
    menu.addItem ("Set meter at bar start...", stopped, false,
                  withEdit ([bar] (auto& self, auto&) { self.editMeter (bar, false); }));
    menu.addItem ("Add cue marker...", true, false,
                  withEdit ([time] (auto& self, auto&) { self.editMarker (time, {}); }));

    auto at = [&sequence] (te::BeatPosition position)
    {
        const auto bars = sequence.toBarsAndBeats (sequence.toTime (position));
        return String (bars.bars + 1) + "." + String (bars.getWholeBeats() + 1);
    };
    PopupMenu tempos, meters, markers;
    for (const auto* event : sequence.getTempos())
    {
        const auto position = event->getStartBeat();
        tempos.addItem (at (position) + "  " + String (event->getBpm(), 2) + " BPM", stopped, false,
                        withEdit ([position] (auto& self, auto&) { self.editTempo (position, true); }));
    }
    for (const auto* event : sequence.getTimeSigs())
    {
        const auto position = event->getStartBeat();
        meters.addItem (at (position) + "  " + event->getStringTimeSig(), stopped, false,
                        withEdit ([position] (auto& self, auto&) { self.editMeter (position, true); }));
    }
    for (const auto* marker : edit->getMarkerManager().getMarkers())
    {
        const auto id = marker->itemID;
        PopupMenu cue;
        cue.addItem ("Go to marker", withEdit ([id] (auto&, auto& project)
        {
            if (auto item = te::findClipForID (project, id))
                project.getTransport().jumpTo (item->getPosition().getStart());
        }));
        cue.addItem ("Rename or delete...", withEdit ([id] (auto& self, auto& project)
        {
            if (auto item = te::findClipForID (project, id))
                if (auto* found = dynamic_cast<te::MarkerClip*> (item))
                    self.editMarker (found->getPosition().getStart(), found);
        }));
        markers.addSubMenu (marker->getName(), cue);
    }
    menu.addSubMenu ("Tempo events", tempos);
    menu.addSubMenu ("Meter events", meters);
    menu.addSubMenu ("Cue markers", markers, markers.getNumItems() != 0);
    menu.showMenuAsync (includeTransport ? PopupMenu::Options().withTargetComponent (this) : PopupMenu::Options());
}

void MusicalTransport::prompt (const String& title, const String& label, const String& value,
                               bool removable, std::function<void (const String&, bool)> apply)
{
    auto* window = new AlertWindow (title, {}, MessageBoxIconType::NoIcon, this);
    window->addTextEditor ("value", value, label);
    window->addButton ("Apply", 1, KeyPress (KeyPress::returnKey));
    window->addButton ("Cancel", 0, KeyPress (KeyPress::escapeKey));
    if (removable)
        window->addButton ("Delete", 2);
    SafePointer<MusicalTransport> safe (this);
    const auto generation = session;
    window->enterModalState (true, ModalCallbackFunction::create (
        [safe, generation, window, apply = std::move (apply)] (int result)
        {
            if (result != 0 && safe != nullptr && safe->session == generation
                && safe->isEnabled() && transportSettled (*safe->edit))
            {
                apply (window->getTextEditorContents ("value").trim(), result == 2);
                if (safe->onTimelineChanged)
                    safe->onTimelineChanged();
            }
        }), true);
}

void MusicalTransport::editTempo (te::BeatPosition beat, bool existing)
{
    auto& sequence = edit->tempoSequence;
    prompt ("Tempo event", "BPM (20–300)", String (sequence.getTempoAt (beat).getBpm(), 2),
            existing && beat > te::BeatPosition(), [this, beat] (const String& value, bool remove)
    {
        if (edit->getTransport().isPlaying())
            return;
        auto& sequence = edit->tempoSequence;
        auto& event = sequence.getTempoAt (beat);
        if (remove && event.getStartBeat() == beat)
        {
            const auto cycle = edit->getTransport().getLoopRange();
            edit->getUndoManager().beginNewTransaction ("Delete tempo event");
            event.removeFromEdit();
            edit->getTransport().setLoopRange (cycle);
        }
        else if (! remove && (! value.containsOnly ("0123456789.") || value.indexOfChar ('.') != value.lastIndexOfChar ('.')
                              || ! setTempoEvent (*edit, beat, value.getDoubleValue())))
            AlertWindow::showMessageBoxAsync (MessageBoxIconType::WarningIcon, "Invalid tempo", "Enter a BPM between 20 and 300.");
    });
}

void MusicalTransport::editMeter (te::BeatPosition beat, bool existing)
{
    prompt ("Meter event", "Beats / note value", edit->tempoSequence.getTimeSigAt (beat).getStringTimeSig(),
            existing && beat > te::BeatPosition(), [this, beat] (const String& value, bool remove)
    {
        if (edit->getTransport().isPlaying())
            return;
        auto& event = edit->tempoSequence.getTimeSigAt (beat);
        if (remove && event.getStartBeat() == beat)
        {
            edit->getUndoManager().beginNewTransaction ("Delete meter event");
            event.removeFromEdit();
        }
        else if (! remove)
        {
            const auto numerator = value.upToFirstOccurrenceOf ("/", false, false).trim();
            const auto denominator = value.fromFirstOccurrenceOf ("/", false, false).trim();
            if (! numerator.containsOnly ("0123456789") || ! denominator.containsOnly ("0123456789")
                || ! setMeterEvent (*edit, beat, numerator.getIntValue(), denominator.getIntValue()))
                AlertWindow::showMessageBoxAsync (MessageBoxIconType::WarningIcon, "Invalid meter",
                                                   "Enter 1–32 beats over 2, 4, 8 or 16.");
        }
    });
}

void MusicalTransport::editMarker (te::TimePosition time, te::MarkerClip::Ptr marker)
{
    const auto id = marker != nullptr ? marker->itemID : te::EditItemID();
    prompt ("Cue marker", "Name", marker != nullptr ? marker->getName() : String(), id.isValid(),
            [this, time, id] (const String& value, bool remove)
    {
        if (! remove && value.isEmpty())
        {
            AlertWindow::showMessageBoxAsync (MessageBoxIconType::WarningIcon, "Cue marker", "Enter a marker name.");
            return;
        }
        if (! id.isValid())
            addCueMarker (*edit, time, value);
        else if (auto found = te::findClipForID (*edit, id))
        {
            edit->getUndoManager().beginNewTransaction (remove ? "Delete cue marker" : "Rename cue marker");
            if (remove)
                found->removeFromParent();
            else
                found->setName (value);
        }
    });
}
}
