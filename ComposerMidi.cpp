// SPDX-License-Identifier: GPL-3.0-or-later
#include "ComposerMidi.h"
#include "InstrumentPerformanceSequence.h"
#include "ComposerFiles.h"
#include "MusicalTransport.h"

#include <deque>
#include <map>

namespace studio::midi
{
namespace te = tracktion;
using namespace juce;

namespace
{
struct Part
{
    String name;
    int channel;
    MidiMessageSequence events;
};

struct Tempo
{
    std::optional<double> bpm;
    std::optional<std::pair<int, int>> meter;
};

constexpr int ppq = 9600;

te::tempo::Sequence::Position positionOnMap (te::TempoSequence& sequence)
{
    sequence.toBeats (te::TimePosition()); // Flush pending changes before reading the internal map.
    return te::tempo::Sequence::Position (sequence.getInternalSequence());
}

bool channelEvent (const MidiMessage& message)
{
    return message.isNoteOnOrOff() || message.isController() || message.isPitchWheel()
        || message.isAftertouch() || message.isChannelPressure() || message.isProgramChange();
}

void insertController (te::MidiList& sequence, te::BeatPosition beat, const MidiMessage& message,
                       UndoManager* undo)
{
    using E = te::MidiControllerEvent;
    if (message.isController())
        sequence.addControllerEvent (beat, message.getControllerNumber(), message.getControllerValue() << 7, undo);
    else if (message.isPitchWheel())
        sequence.addControllerEvent (beat, E::pitchWheelType, message.getPitchWheelValue(), undo);
    else if (message.isChannelPressure())
        sequence.addControllerEvent (beat, E::channelPressureType, message.getChannelPressureValue() << 7, undo);
    else if (message.isAftertouch())
        sequence.addControllerEvent (beat, E::aftertouchType, message.getAfterTouchValue() << 7, message.getNoteNumber(), undo);
    else if (message.isProgramChange())
        sequence.addControllerEvent (beat, E::programChangeType, message.getProgramChangeNumber() << 7, undo);
}

MidiMessage controllerMessage (const te::MidiControllerEvent& event, int channel)
{
    using E = te::MidiControllerEvent;
    const int value = event.getControllerValue() >> 7;
    switch (event.getType())
    {
        case E::pitchWheelType:      return MidiMessage::pitchWheel (channel, event.getControllerValue());
        case E::aftertouchType:      return MidiMessage::aftertouchChange (channel, event.getMetadata(), value);
        case E::channelPressureType: return MidiMessage::channelPressureChange (channel, value);
        case E::programChangeType:   return MidiMessage::programChange (channel, value);
        default:                     return MidiMessage::controllerEvent (channel, event.getType(), value);
    }
}

bool supportedController (int type)
{
    using E = te::MidiControllerEvent;
    return (type >= 0 && type < 128) || type == E::pitchWheelType || type == E::aftertouchType
        || type == E::channelPressureType || type == E::programChangeType;
}

Result replaceTempoMap (te::Edit& edit, const std::map<double, Tempo>& events)
{
    auto& sequence = edit.tempoSequence;
    auto& undo = edit.getUndoManager();
    auto tree = edit.state.getChildWithName (te::IDs::TEMPOSEQUENCE);
    const bool legacyBeats = edit.engine.getEngineBehaviour().lengthOfOneBeatDependsOnTimeSignature();
    double lastQuarter = 0, lastBeat = 0, lastMeterBeat = 0, bpm = 120;
    int denominator = 4;
    std::vector<ValueTree> tempos, meters;
    for (const auto& [quarter, event] : events)
    {
        const double beat = lastBeat + (quarter - lastQuarter) * (legacyBeats ? denominator / 4.0 : 1.0);
        if (event.bpm) bpm = *event.bpm;
        if (event.meter)
        {
            const auto [numerator, nextDenominator] = *event.meter;
            if (numerator < 1 || numerator > 32 || nextDenominator < 1 || nextDenominator > 32
                || (beat > 0 && beat - lastMeterBeat < 1.0))
                return Result::fail ("This MIDI file has a meter change outside Studio's supported range.");
            denominator = nextDenominator;
            lastMeterBeat = beat;
            meters.push_back (te::createValueTree (te::IDs::TIMESIG, te::IDs::startBeat, beat,
                                 te::IDs::numerator, numerator, te::IDs::denominator, denominator));
        }
        if (event.bpm || quarter == 0)
        {
            if (bpm < te::TempoSetting::minBPM - 0.001 || bpm > te::TempoSetting::maxBPM + 0.001)
                return Result::fail ("File tempo is outside 20–300 BPM. Import with the project tempo instead.");
            tempos.push_back (te::TempoSetting::create (te::BeatPosition::fromBeats (beat),
                jlimit (te::TempoSetting::minBPM, te::TempoSetting::maxBPM, bpm), 1.0f));
        }
        lastQuarter = quarter;
        lastBeat = beat;
    }

    te::EditTimecodeRemapperSnapshot snapshot;
    snapshot.savePreChangeState (edit);
    const auto cycle = edit.getTransport().getLoopRange();
    // Keep an initial tempo and meter in the tree throughout the edit.
    for (int i = tree.getNumChildren(); --i >= 0;)
    {
        auto child = tree.getChild (i);
        if ((child.hasType (te::IDs::TEMPO) || child.hasType (te::IDs::TIMESIG))
            && (double) child.getProperty (te::IDs::startBeat) > 0)
            tree.removeChild (i, &undo);
    }
    auto initialTempo = sequence.getTempo (0)->state;
    initialTempo.copyPropertiesFrom (tempos.front(), &undo);
    auto initialMeter = sequence.getTimeSig (0)->state;
    initialMeter.copyPropertiesFrom (meters.front(), &undo);
    initialMeter.setProperty (te::IDs::triplets, false, &undo);
    for (size_t i = 1; i < tempos.size(); ++i) tree.appendChild (tempos[i], &undo);
    for (size_t i = 1; i < meters.size(); ++i) tree.appendChild (meters[i], &undo);
    snapshot.remapEdit (edit);
    edit.getTransport().setLoopRange (cycle);
    return Result::ok();
}

struct VisibleRange
{
    te::BeatPosition source, start, end;
};

VisibleRange visibleRange (te::MidiClip& clip)
{
    const auto range = clip.edit.tempoSequence.toBeats (clip.getPosition().time);
    return { clip.getContentStartBeat(), range.getStart(), range.getEnd() };
}

template<typename Notes, typename Controllers>
void visitVisible (te::MidiClip& clip, Notes&& note, Controllers&& controller)
{
    const auto [source, start, end] = visibleRange (clip);
    const auto offset = te::toDuration (source);
    const auto& sequence = clip.getSequence();
    for (const auto* n : sequence.getNotes())
    {
        const auto first = std::max (start, n->getStartBeat() + offset);
        const auto last = std::min (end, n->getEndBeat() + offset);
        if (first < last)
            note (*n, first, last);
    }
    // Chase the last controller of each type at the left trim, including per-key pressure.
    std::map<std::pair<int, int>, const te::MidiControllerEvent*> chase;
    for (const auto* event : sequence.getControllerEvents())
    {
        const auto beat = event->getBeatPosition() + offset;
        const auto key = std::pair (event->getType(), event->getMetadata());
        if (beat < start)
            chase[key] = event;
        else if (beat == start)
            chase.erase (key);
        else
            break;
    }
    for (const auto& item : chase)
        controller (*item.second, start);
    for (const auto* event : sequence.getControllerEvents())
    {
        const auto beat = event->getBeatPosition() + offset;
        if (beat >= start && beat < end)
            controller (*event, beat);
    }
}

bool supportedClip (te::MidiClip& clip)
{
    // Flattening clip looping, MPE and comp takes needs a lossless policy.
    return ! clip.isLooping() && ! clip.getSequence().containsController (te::MidiControllerEvent::sysExType)
        && clip.getSequence().getNumSysExEvents() == 0 && ! clip.getMPEMode();
}

bool validTickSpacing (const MidiMessageSequence& sequence)
{
    double prior = 0;
    for (const auto* event : sequence)
    {
        const double tick = event->message.getTimeStamp();
        if (tick < prior || tick > std::numeric_limits<int>::max() || tick - prior > 0x0fffffff)
            return false;
        prior = tick;
    }
    return true;
}
}

Result read (const File& source, Smf& smf)
{
    auto input = source.createInputStream();
    if (input == nullptr || source.getSize() > 16 * 1024 * 1024
        || ! smf.file.readFrom (*input, false, &smf.type)
        || (smf.type != 0 && smf.type != 1) || smf.file.getTimeFormat() <= 0)
        return Result::fail ("Choose a valid type 0 or type 1 PPQ MIDI file up to 16 MB.");
    return Result::ok();
}

Result import (te::Edit& edit, const Smf& smf, te::TimePosition at, te::AudioTrack* destination, bool useFileTempo)
{
    if (! transportSettled (edit) || edit.getTransport().isPlaying()
        || (destination != nullptr && (destination->isFrozen (te::Track::anyFreeze) || &destination->edit != &edit)))
        return Result::fail ("Stop playback and select an editable MIDI destination.");
    if (smf.file.getTimeFormat() <= 0)
        return Result::fail ("MIDI file does not use PPQ timing.");

    std::map<double, Tempo> map;
    map[0] = { 120.0, std::pair (4, 4) };
    std::vector<Part> parts;
    for (int index = 0; index < smf.file.getNumTracks(); ++index)
    {
        const auto& source = *smf.file.getTrack (index);
        String name = "MIDI " + String (index + 1);
        std::map<int, MidiMessageSequence> channels;
        for (const auto* holder : source)
        {
            const auto& event = holder->message;
            if (event.isTrackNameEvent()) name = event.getTextFromTextMetaEvent().trim();
            else if (event.isTempoMetaEvent())
                map[event.getTimeStamp() / smf.file.getTimeFormat()].bpm = 60.0 / event.getTempoSecondsPerQuarterNote();
            else if (event.isTimeSignatureMetaEvent())
            {
                int numerator, denominator;
                event.getTimeSignatureInfo (numerator, denominator);
                map[event.getTimeStamp() / smf.file.getTimeFormat()].meter = { numerator, denominator };
            }
            else if (channelEvent (event))
                channels[event.getChannel()].addEvent (event);
        }
        for (auto& [channel, events] : channels)
            parts.push_back ({ name.isEmpty() ? "MIDI" : name, channel, std::move (events) });
    }
    if (parts.empty() || parts.size() > 128 || map.size() > 4096)
        return Result::fail ("Import requires MIDI channel events on at most 128 track/channel parts and 4096 tempo or meter positions.");

    auto& undo = edit.getUndoManager();
    edit.getUndoManager().beginNewTransaction ("Import MIDI file");
    if (useFileTempo)
    {
        auto replaced = replaceTempoMap (edit, map);
        if (replaced.failed()) return replaced;
        at = {};
    }
    auto& sequence = edit.tempoSequence;
    auto position = positionOnMap (sequence);
    position.set (at);
    const auto baseQuarter = position.getPPQTime();
    const auto startBeat = position.getBeats();
    auto fromTicks = [&] (double ticks)
    {
        position.setPPQTime (baseQuarter + ticks / smf.file.getTimeFormat());
        return position.getBeats();
    };
    const auto abort = [&undo]
    {
        undo.undoCurrentTransactionOnly();
        return Result::fail ("Could not insert the MIDI parts. The import was rolled back.");
    };

    for (const auto& part : parts)
    {
        auto* track = destination;
        const auto name = part.name + " (Ch " + String (part.channel) + ")";
        if (track == nullptr)
        {
            auto inserted = edit.insertNewAudioTrack (te::TrackInsertPoint::getEndOfTracks (edit), nullptr);
            if (inserted == nullptr) return abort();
            track = inserted.get();
            track->setName (name);
        }
        const auto endBeat = fromTicks (part.events.getEndTime()) + te::BeatDuration::fromBeats (0.125);
        if (sequence.toTime (endBeat) > te::Edit::getMaximumEditEnd()) return abort();
        auto clip = track->insertMIDIClip (name, { at, sequence.toTime (endBeat) }, nullptr);
        if (clip == nullptr) return abort();
        clip->setMidiChannel (te::MidiChannel (static_cast<uint8> (part.channel)));
        auto& midi = clip->getSequence();
        // The wire format supplies no note ID. Pair same-channel, same-pitch overlaps FIFO.
        std::array<std::deque<const MidiMessage*>, 128> held;
        for (const auto* holder : part.events)
        {
            const auto& message = holder->message;
            const auto beat = te::toPosition (fromTicks (message.getTimeStamp()) - startBeat);
            if (message.isNoteOn())
                held[(size_t) message.getNoteNumber()].push_back (&message);
            else if (message.isNoteOff())
            {
                auto& keys = held[(size_t) message.getNoteNumber()];
                if (keys.empty()) continue;
                const auto* on = keys.front();
                keys.pop_front();
                const auto start = te::toPosition (fromTicks (on->getTimeStamp()) - startBeat);
                midi.addNote (message.getNoteNumber(), start, std::max (te::BeatDuration::fromBeats (1.0 / ppq), beat - start),
                              on->getVelocity(), 0, &undo);
            }
            else
                insertController (midi, beat, message, &undo);
        }
        // A malformed file may omit a note-off. Close held notes at the end of the clip.
        for (size_t key = 0; key < held.size(); ++key)
            for (const auto* message : held[key])
            {
                const auto start = fromTicks (message->getTimeStamp());
                midi.addNote ((int) key, te::toPosition (start - startBeat), endBeat - start,
                              message->getVelocity(), 0, &undo);
            }
    }
    undo.beginNewTransaction();
    return Result::ok();
}

Result write (te::Edit& edit, const Array<te::MidiClip*>& clips, const File& destination)
{
    if (clips.isEmpty()) return Result::fail ("Select MIDI clips or tracks containing MIDI clips.");
    if (destination.exists()) return Result::fail ("Choose a new MIDI filename.");
    auto& tempo = edit.tempoSequence;
    auto position = positionOnMap (tempo);
    auto ticks = [&] (te::BeatPosition beat)
    {
        position.set (beat);
        return std::round (position.getPPQTime() * ppq);
    };
    MidiFile file;
    file.setTicksPerQuarterNote (ppq);
    MidiMessageSequence meta;
    for (const auto* event : tempo.getTempos())
        meta.addEvent (MidiMessage::tempoMetaEvent (roundToInt (60000000.0 / event->getBpm()))
                           .withTimeStamp (ticks (event->getStartBeat())));
    for (const auto* event : tempo.getTimeSigs())
        meta.addEvent (MidiMessage::timeSignatureMetaEvent (event->numerator, event->denominator)
                           .withTimeStamp (ticks (event->getStartBeat())));
    if (! validTickSpacing (meta))
        return Result::fail ("The tempo map exceeds Standard MIDI File tick limits.");
    file.addTrack (meta);

    for (auto* track : te::getAudioTracks (edit))
    {
        MidiMessageSequence events;
        struct NoteSpan { int key; double start, end; };
        std::vector<NoteSpan> notes;
        for (auto* clip : track->getClips())
        {
            auto* midiClip = dynamic_cast<te::MidiClip*> (clip);
            if (midiClip == nullptr || ! clips.contains (midiClip)) continue;
            if (! supportedClip (*midiClip))
                return Result::fail ("MIDI export currently requires linear clips without MPE or SysEx.");
            const int channel = midiClip->getMidiChannel().getChannelNumber();
            visitVisible (*midiClip,
                [&] (const te::MidiNote& note, te::BeatPosition first, te::BeatPosition last)
                {
                    const auto start = ticks (first);
                    const auto end = std::max (start + 1, ticks (last));
                    notes.push_back ({ channel * 128 + note.getNoteNumber(), start, end });
                    events.addEvent (MidiMessage::noteOn (channel, note.getNoteNumber(), (uint8) note.getVelocity()).withTimeStamp (start));
                    events.addEvent (MidiMessage::noteOff (channel, note.getNoteNumber()).withTimeStamp (end));
                },
                [&] (const te::MidiControllerEvent& event, te::BeatPosition beat)
                {
                    if (supportedController (event.getType()))
                        events.addEvent (controllerMessage (event, channel).withTimeStamp (ticks (beat)));
                });
        }
        std::stable_sort (notes.begin(), notes.end(), [] (const auto& a, const auto& b)
        {
            return std::pair (a.key, a.start) < std::pair (b.key, b.start);
        });
        if (std::adjacent_find (notes.begin(), notes.end(), [] (const auto& a, const auto& b)
            {
                return a.key == b.key && a.end > b.end;
            }) != notes.end())
            return Result::fail ("Nested notes on the same key and channel cannot retain their individual lengths in one MIDI track.");
        if (events.getNumEvents() > 0)
        {
            events.addEvent (MidiMessage::textMetaEvent (3, track->getName()));
            if (! validTickSpacing (events))
                return Result::fail ("The selected regions exceed Standard MIDI File tick limits.");
            file.addTrack (events);
        }
    }
    if (file.getNumTracks() < 2) return Result::fail ("The selection contains no exportable MIDI events.");
    TemporaryFile output (destination);
    auto stream = output.getFile().createOutputStream();
    if (! stream || ! file.writeTo (*stream, 1))
        return Result::fail ("Could not write MIDI to this folder.");
    stream->flush();
    if (stream->getStatus().failed()) return stream->getStatus();
    stream.reset();
    return publishNewFile (output.getFile(), destination);
}

Result join (const Array<te::MidiClip*>& clips, te::MidiClip::Ptr& result)
{
    if (clips.size() < 2) return Result::fail ("Select two or more MIDI regions.");
    auto* first = *std::min_element (clips.begin(), clips.end(), [] (auto* a, auto* b)
    {
        return a->getPosition().getStart() < b->getPosition().getStart();
    });
    auto& edit = first->edit;
    auto* track = dynamic_cast<te::AudioTrack*> (first->getClipTrack());
    if (track == nullptr || track->isFrozen (te::Track::anyFreeze)
        || ! transportSettled (edit) || edit.getTransport().isPlaying())
        return Result::fail ("Stop playback and select regions on an editable track.");

    auto range = first->getPosition().time;
    for (auto* clip : clips)
    {
        if (clip->getClipTrack() != track || clip->getMidiChannel() != first->getMidiChannel()
            || clip->isMuted() || clip->hasAnyTakes() || ! supportedClip (*clip))
            return Result::fail ("Join requires unmuted linear regions on the same track and channel, without alternate takes, MPE or SysEx.");
        if (clip->getQuantisation().isEnabled() || clip->getGrooveTemplate().isNotEmpty()
            || clip->getLinkGroupID().isNotEmpty() || clip->getVolumeDb() != first->getVolumeDb())
            return Result::fail ("Join requires unlinked regions with matching gain and no playback quantisation or groove.");
        range = range.getUnionWith (clip->getPosition().time);
    }

    auto& undo = edit.getUndoManager();
    undo.beginNewTransaction ("Join MIDI regions");
    result = track->insertMIDIClip (first->getName() + " (joined)", range, nullptr);
    if (result == nullptr) return Result::fail ("Could not create the joined region.");
    result->setMidiChannel (first->getMidiChannel());
    result->setColour (first->getColour());
    result->setVolumeDb (first->getVolumeDb());
    const auto origin = te::toDuration (edit.tempoSequence.toBeats (range.getStart()));
    auto& merged = result->getSequence();
    std::vector<te::MidiClip::Ptr> originals (clips.begin(), clips.end());
    std::stable_sort (originals.begin(), originals.end(), [] (const auto& a, const auto& b)
    {
        return a->getPosition().getStart() < b->getPosition().getStart();
    });
    for (const auto& clip : originals)
    {
        visitVisible (*clip,
            [&] (const te::MidiNote& note, te::BeatPosition start, te::BeatPosition end)
            {
                if(auto* added=merged.addNote (note.getNoteNumber(), start - origin, end - start, note.getVelocity(), note.getColour(), &undo))
                    studio::designer::copyTag(note.state,added->state,&undo);
            },
            [&] (const te::MidiControllerEvent& event, te::BeatPosition beat)
            {
                if(auto* added=merged.addControllerEvent (beat - origin, event.getType(), event.getControllerValue(), event.getMetadata(), &undo))
                    studio::designer::copyTag(event.state,added->state,&undo);
            });
        clip->removeFromParent();
    }
    undo.beginNewTransaction();
    return Result::ok();
}
}
