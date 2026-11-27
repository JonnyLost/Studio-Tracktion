// SPDX-License-Identifier: GPL-3.0-or-later
#include "MusicalTransport.h"
#include <cmath>

namespace studio
{
namespace te = tracktion;
using namespace juce;

// Small mono PCM WAV, generated entirely on the message thread. Smooth
// onset and decay prevent unwanted discontinuities while allowing 1 ms clicks.
bool writeMetronomeWave (const File& file, int frequency, int milliseconds, float gain)
{
    constexpr int rate = 48000;
    if (frequency < 200 || frequency > 3000 || milliseconds < 1 || milliseconds > 150
        || ! std::isfinite (gain) || gain < 0.0f || gain > 1.0f) return false;
    const int n = rate * milliseconds / 1000;
    MemoryOutputStream wave;
    wave.write ("RIFF", 4); wave.writeInt (36 + n * 2);
    wave.write ("WAVEfmt ", 8); wave.writeInt (16); wave.writeShort (1); wave.writeShort (1);
    wave.writeInt (rate); wave.writeInt (rate * 2); wave.writeShort (2); wave.writeShort (16);
    wave.write ("data", 4); wave.writeInt (n * 2);
    for (int i = 0; i < n; ++i)
    {
        const double x = (double) i / (n - 1);
        const double env = std::min (1.0, x * 12.0) * std::pow (1.0 - x, 2.0);
        const double phase = MathConstants<double>::twoPi * frequency * i / rate;
        wave.writeShort ((short) std::lround (32767.0 * gain * env * std::sin (phase)));
    }
    return file.replaceWithData (wave.getData(), wave.getDataSize());
}

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
    auto& sequence = edit.tempoSequence;
    auto& current = sequence.getTimeSigAt (beat);
    if (current.getStartBeat() == beat)
    {
        current.numerator = numerator;
        current.denominator = denominator;
        return true;
    }

    // Tracktion's convenience insertion rounds to a whole engine beat.
    // A bar following an imported meter event can have a fractional start.
    const auto next = std::find_if (sequence.getTimeSigs().begin(), sequence.getTimeSigs().end(),
        [beat] (auto* event) { return event->getStartBeat() > beat; });
    if ((beat - current.getStartBeat()).inBeats() < 1.0
        || (next != sequence.getTimeSigs().end() && ((*next)->getStartBeat() - beat).inBeats() < 1.0))
        return false;
    auto state = current.state.getParent();
    auto signature = current.state.createCopy();
    signature.setProperty (te::IDs::startBeat, beat.inBeats(), nullptr);
    signature.setProperty (te::IDs::numerator, numerator, nullptr);
    signature.setProperty (te::IDs::denominator, denominator, nullptr);
    state.addChild (signature, state.indexOf (current.state) + 1, &edit.getUndoManager());
    return true;
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

MusicalTransport::MusicalTransport() : TextButton ("Timeline")
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
}

void MusicalTransport::toggleCycle()
{
    if (! isEnabled() || edit == nullptr || ! transportSettled (*edit)) return;
    auto& transport = edit->getTransport();
    if (transport.looping)
        transport.looping = false;
    else if (transport.getLoopRange().getLength().inSeconds() >= .12)
        transport.looping = true;
    else
    {
        auto& tempo = edit->tempoSequence;
        const auto bar = tempo.toBarsAndBeats (transport.getPosition()).bars;
        setCycle (*edit, { tempo.toTime (te::tempo::BarsAndBeats { bar, {} }),
                           tempo.toTime (te::tempo::BarsAndBeats { bar + 1, {} }) });
    }
}

void MusicalTransport::toggleClick()
{
    if (isEnabled() && edit != nullptr && transportSettled (*edit))
        edit->clickTrackEnabled = ! edit->clickTrackEnabled.get();
}

void MusicalTransport::promptClickSettings()
{
    if (! isEnabled() || edit == nullptr || ! transportSettled (*edit) || edit->getTransport().isPlaying())
    {
        AlertWindow::showMessageBoxAsync (MessageBoxIconType::InfoIcon, "Metronome", "Stop playback before changing the metronome sound.");
        return;
    }
    auto& prefs = edit->engine.getPropertyStorage().getPropertiesFile();
    auto* window = new AlertWindow ("Metronome Sound", "Shorter durations produce a click; longer durations produce a tone.",
                                    MessageBoxIconType::NoIcon, this);
    window->addTextEditor ("pitch", String (prefs.getIntValue ("studio.clickPitch", 1000)), "Beat pitch (200-3000 Hz)");
    window->addTextEditor ("duration", String (prefs.getIntValue ("studio.clickDuration", 5)), "Duration (1-150 ms)");
    window->addTextEditor ("accentPitch", String (prefs.getIntValue ("studio.clickAccentPitch", 1500)), "Downbeat pitch (200-3000 Hz)");
    window->addTextEditor ("accentGain", String (prefs.getIntValue ("studio.clickAccentGain", 65)), "Downbeat boost (0-100%)");
    window->addComboBox ("accent", { "Accent downbeat", "No accent" }, "Bar accent");
    window->getComboBoxComponent ("accent")->setSelectedId (edit->clickTrackEmphasiseBars ? 1 : 2, dontSendNotification);
    window->addButton ("Apply", 1, KeyPress (KeyPress::returnKey));
    window->addButton ("Factory sound", 2);
    window->addButton ("Cancel", 0, KeyPress (KeyPress::escapeKey));
    SafePointer<MusicalTransport> safe (this);
    const auto generation = session;
    window->enterModalState (true, ModalCallbackFunction::create ([safe, generation, window] (int result)
    {
        if (result == 0 || safe == nullptr || safe->session != generation || ! safe->isEnabled()
            || safe->edit == nullptr || ! transportSettled (*safe->edit) || safe->edit->getTransport().isPlaying()) return;
        auto& project = *safe->edit;
        auto& storage = project.engine.getPropertyStorage();
        auto& prefs = storage.getPropertiesFile();
        String big, small;
        if (result == 1)
        {
            const auto read = [window] (const char* key, int low, int high)
            {
                const auto value = window->getTextEditorContents (key).trim();
                return value.isNotEmpty() && value.length() <= 4 && value.containsOnly ("0123456789")
                    && value.getIntValue() >= low && value.getIntValue() <= high ? value.getIntValue() : -1;
            };
            const int pitch = read ("pitch", 200, 3000), duration = read ("duration", 1, 150);
            const int accentPitch = read ("accentPitch", 200, 3000), boost = read ("accentGain", 0, 100);
            if (pitch < 0 || duration < 0 || accentPitch < 0 || boost < 0)
            {
                AlertWindow::showMessageBoxAsync (MessageBoxIconType::WarningIcon, "Metronome", "Enter whole numbers within the displayed ranges.");
                return;
            }
            const auto folder = prefs.getFile().getSiblingFile ("Studio Metronome");
            if (folder.createDirectory().failed())
            {
                AlertWindow::showMessageBoxAsync (MessageBoxIconType::WarningIcon, "Metronome", "Could not create the metronome sound folder.");
                return;
            }
            const auto suffix = String (pitch) + "-" + String (duration) + "-" + String (accentPitch) + "-" + String (boost) + ".wav";
            const auto beat = folder.getChildFile ("beat-" + suffix), downbeat = folder.getChildFile ("accent-" + suffix);
            if (! writeMetronomeWave (beat, pitch, duration, 0.45f)
                || ! writeMetronomeWave (downbeat, accentPitch, duration, 0.45f * (1.0f + boost / 100.0f)))
            {
                AlertWindow::showMessageBoxAsync (MessageBoxIconType::WarningIcon, "Metronome", "Could not write metronome sounds. Existing sound retained.");
                return;
            }
            small = beat.getFullPathName(); big = downbeat.getFullPathName();
            prefs.setValue ("studio.clickPitch", pitch); prefs.setValue ("studio.clickDuration", duration);
            prefs.setValue ("studio.clickAccentPitch", accentPitch); prefs.setValue ("studio.clickAccentGain", boost);
        }
        // These are the pinned Tracktion ClickGenerator's existing sample
        // settings. Update both before rebuilding playback once.
        storage.setProperty (te::SettingID::clickTrackSampleBig, big);
        storage.setProperty (te::SettingID::clickTrackSampleSmall, small);
        project.clickTrackEmphasiseBars = window->getComboBoxComponent ("accent")->getSelectedId() == 1;
        prefs.saveIfNeeded();
        te::TransportControl::restartAllTransports (project.engine, false);
        if (safe->onTimelineChanged) safe->onTimelineChanged();
    }), true);
}

void MusicalTransport::promptTempoAtPlayhead()
{
    if (isEnabled() && edit != nullptr && ! edit->getTransport().isPlaying() && transportSettled (*edit))
        editTempo (te::roundToNearestBeat (edit->tempoSequence.toBeats (edit->getTransport().getPosition())), false);
}

void MusicalTransport::promptMeterAtPlayhead()
{
    if (isEnabled() && edit != nullptr && ! edit->getTransport().isPlaying() && transportSettled (*edit))
    {
        auto& sequence = edit->tempoSequence;
        const auto bar = sequence.toBarsAndBeats (edit->getTransport().getPosition()).bars;
        editMeter (sequence.toBeats (te::tempo::BarsAndBeats { bar, {} }), false);
    }
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
        menu.addItem ("Cycle playback", true, transport.looping, withEdit ([] (auto& self, auto&) { self.toggleCycle(); }));
        menu.addItem ("Metronome", true, edit->clickTrackEnabled, withEdit ([] (auto& self, auto&) { self.toggleClick(); }));

        menu.addItem ("Accent bar starts", true, edit->clickTrackEmphasiseBars,
                      withEdit ([] (auto&, auto& project)
        {
            project.clickTrackEmphasiseBars = ! project.clickTrackEmphasiseBars.get();
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
        menu.addItem ("Metronome sound...", stopped, false, withEdit ([] (auto& self, auto&) { self.promptClickSettings(); }));
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
    prompt ("Tempo event", "BPM (20-300)", String (sequence.getTempoAt (beat).getBpm(), 2),
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
                                                   "Enter 1-32 beats over 2, 4, 8 or 16.");
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
