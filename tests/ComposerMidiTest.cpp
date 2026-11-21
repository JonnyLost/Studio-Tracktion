// SPDX-License-Identifier: GPL-3.0-or-later
#include "../ComposerMidi.h"
#include "../ComposerTracks.h"
#include "../SpatialRouting.h"
#include "../SpatialState.h"
#include "../StudioSynth.h"

namespace studio
{
namespace
{
namespace te = tracktion;
using namespace te::literals;
using namespace juce;

Result verifyMidi (te::Engine& engine, const File& folder)
{
    MidiMessageSequence conductor, part;
    conductor.addEvent (MidiMessage::tempoMetaEvent (500000));
    conductor.addEvent (MidiMessage::timeSignatureMetaEvent (4, 4));
    conductor.addEvent (MidiMessage::tempoMetaEvent (750000).withTimeStamp (1920));
    conductor.addEvent (MidiMessage::timeSignatureMetaEvent (6, 8).withTimeStamp (1920));
    constexpr int channel = 7;
    part.addEvent (MidiMessage::noteOn (channel, 61, (uint8) 88).withTimeStamp (1680));
    part.addEvent (MidiMessage::noteOn (channel, 61, (uint8) 73).withTimeStamp (1920));
    part.addEvent (MidiMessage::noteOff (channel, 61).withTimeStamp (2040));
    part.addEvent (MidiMessage::noteOff (channel, 61).withTimeStamp (2160));
    for (const int cc : { 1, 7, 11, 64 })
        part.addEvent (MidiMessage::controllerEvent (channel, cc, cc + 12).withTimeStamp (1800));
    part.addEvent (MidiMessage::pitchWheel (channel, 9333).withTimeStamp (2040));
    part.addEvent (MidiMessage::aftertouchChange (channel, 61, 60).withTimeStamp (2100));
    part.addEvent (MidiMessage::channelPressureChange (channel, 32).withTimeStamp (2100));

    for (const int type : { 0, 1 })
    {
        MidiFile fixture;
        fixture.setTicksPerQuarterNote (480);
        if (type == 1)
        {
            fixture.addTrack (conductor);
            fixture.addTrack (part);
        }
        else
        {
            auto merged = conductor;
            merged.addSequence (part, 0);
            fixture.addTrack (merged);
        }

        auto file = folder.getChildFile ("source-" + String (type) + ".mid");
        auto stream = file.createOutputStream();
        if (stream == nullptr || ! fixture.writeTo (*stream, type)) return Result::fail ("write SMF fixture");
        stream.reset();
        midi::Smf input;
        if (auto loaded = midi::read (file, input); loaded.failed()) return loaded;
        if (input.type != type) return Result::fail ("MIDI type");
        auto project = folder.getChildFile ("midi-" + String (type) + ".tracktionedit");
        auto edit = te::createEmptyEdit (engine, project);
        if (auto imported = midi::import (*edit, input, 0_tp, nullptr, true); imported.failed()) return imported;
        auto& tempo = edit->tempoSequence;
        if (tempo.getTimeSigAt (4_bp).denominator != 8 || tempo.getTimeSigAt (4_bp).numerator != 6
            || tempo.getTempoAt (4_bp).getBpm() != 80)
            return Result::fail ("imported conductor positions");
        auto* track = te::getAudioTracks (*edit).getLast();
        auto* clip = dynamic_cast<te::MidiClip*> (track->getClips().getFirst());
        if (clip == nullptr || clip->getMidiChannel().getChannelNumber() != channel)
            return Result::fail ("imported channel");
        auto& seq = clip->getSequence();
        if (seq.getNumNotes() != 2 || seq.getNote (0)->getVelocity() != 88 || seq.getNote (1)->getVelocity() != 73
            || seq.getNote (0)->getStartBeat() != 3.5_bp || seq.getNote (0)->getEndBeat() != 4.5_bp
            || seq.getNote (1)->getStartBeat() != 4_bp || seq.getNote (1)->getEndBeat() != 5_bp
            || seq.getNumControllerEvents() != 7)
            return Result::fail ("overlapping notes, expression and 6/8 PPQ conversion");

        const auto exported = folder.getChildFile ("out-" + String (type) + ".mid");
        if (auto result = midi::write (*edit, { clip }, exported); result.failed()) return result;
        if (midi::write (*edit, { clip }, exported).wasOk()) return Result::fail ("refuse to replace MIDI");
        midi::Smf output;
        if (auto loaded = midi::read (exported, output); loaded.failed()) return loaded;
        if (output.type != 1 || output.file.getNumTracks() != 2) return Result::fail ("type 1 export tracks");
        MidiMessageSequence actual = *output.file.getTrack (1);
        const auto tickRatio = output.file.getTimeFormat() / 480;
        int events = 0;
        for (const auto* found : actual)
        {
            const auto& message = found->message;
            if (message.getChannel() == 0) continue;
            bool matched = false;
            for (const auto* expected : part)
                if (expected->message.getTimeStamp() * tickRatio == message.getTimeStamp()
                    && expected->message.getRawDataSize() == message.getRawDataSize()
                    && std::memcmp (expected->message.getRawData(), message.getRawData(),
                                    (size_t) message.getRawDataSize()) == 0)
                    matched = true;
            if (! matched) return Result::fail ("MIDI channel event bytes or timing changed");
            ++events;
        }
        if (events != part.getNumEvents()) return Result::fail ("MIDI channel event count changed");

        auto second = track->insertMIDIClip ("Second", { 3_tp, 4_tp }, nullptr);
        second->setMidiChannel (clip->getMidiChannel());
        second->getSequence().addNote (67, 0_bp, 0.5_bd, 101, 0, nullptr);
        second->getSequence().addControllerEvent (0.25_bp, 64, 0, nullptr);
        second->getSequence().addControllerEvent (0.5_bp, te::MidiControllerEvent::pitchWheelType, 8192, nullptr);
        te::MidiClip::Ptr joined;
        if (auto result = midi::join ({ clip, second.get() }, joined); result.failed()) return result;
        if (track->getClips().size() != 1 || joined->getSequence().getNumNotes() != 3
            || joined->getSequence().getNumControllerEvents() != 9)
            return Result::fail ("join notes and expression");
        if (! edit->getUndoManager().undo() || track->getClips().size() != 2
            || ! edit->getUndoManager().redo() || track->getClips().size() != 1)
            return Result::fail ("join undo and redo");
        joined.reset();
        second.reset();
        if (! te::EditFileOperations (*edit).writeToFile (project, false)) return Result::fail ("save joined regions");
        edit = nullptr;
        edit = te::loadEditFromFile (engine, project);
        auto* reopened = dynamic_cast<te::MidiClip*> (te::getAudioTracks (*edit).getLast()->getClips().getFirst());
        if (reopened == nullptr || reopened->getSequence().getNumControllerEvents() != 9)
            return Result::fail ("reopen joined expressions");
    }
    return Result::ok();
}

Result verifyTracks (te::Engine& engine, const File& folder)
{
    auto edit = te::createEmptyEdit (engine, folder.getChildFile ("tracks.tracktionedit"));
    auto original = edit->insertNewAudioTrack (te::TrackInsertPoint::getEndOfTracks (*edit), nullptr);
    original->setName ("Original");
    auto synth = edit->getPluginCache().createNewPlugin (studio::synth::Plugin::xmlTypeName, {});
    original->pluginList.insertPlugin (synth, 0, nullptr);
    auto seat = spatial::writeSeat (original->itemID.toString(), { { 0.6f, 0.2f }, spatial::Section::other, -12 });
    original->state.appendChild (seat, nullptr);
    if (auto result = spatial::setRoomSend (*original, true, { 0.6f, 0.2f }, -12, {}); result.failed()) return result;
    auto* room = spatial::roomReturnTrack (*edit);
    auto copy = tracks::duplicate (*original);
    auto* copiedAudio = dynamic_cast<te::AudioTrack*> (copy.get());
    if (copiedAudio == nullptr || ! copy->isMuted (false) || copy->itemID == original->itemID
        || ! spatial::hasRoomSend (*copiedAudio) || spatial::roomReturnTrack (*edit) != room
        || copy->state.getChildWithName (spatial::seatType)["trackKey"].toString() != copy->itemID.toString()
        || dynamic_cast<studio::synth::Plugin*> (copiedAudio->pluginList.getPlugins()[0].get()) == nullptr)
        return Result::fail ("duplicate instrument, Stage binding and room routing");
    const auto copiedId = copy->itemID;
    if (! edit->getUndoManager().undo() || te::findTrackForID (*edit, copiedId) != nullptr
        || ! edit->getUndoManager().redo())
        return Result::fail ("duplicate undo and redo");
    copy.reset();
    copiedAudio = dynamic_cast<te::AudioTrack*> (te::findTrackForID (*edit, copiedId));
    if (copiedAudio == nullptr) return Result::fail ("duplicate restored");
    tracks::move (*copiedAudio, -1);
    if (te::getAudioTracks (*edit).indexOf (copiedAudio) >= te::getAudioTracks (*edit).indexOf (original.get()))
        return Result::fail ("track reorder");
    if (! edit->getUndoManager().undo()) return Result::fail ("undo reorder");
    tracks::remove (*copiedAudio);
    if (te::findTrackForID (*edit, copiedId) != nullptr || ! edit->getUndoManager().undo()
        || te::findTrackForID (*edit, copiedId) == nullptr || spatial::roomReturnTrack (*edit) != room)
        return Result::fail ("track deletion and undo");
    return Result::ok();
}
}

int runComposerMidiTest()
{
    juce::TemporaryFile scratch (".folder");
    const auto folder = scratch.getFile();
    if (folder.createDirectory().failed()) return 1;
    const juce::ScopeGuard cleanup { [&] { folder.deleteRecursively(); } };
    tracktion::Engine engine ("Studio Composer MIDI Test");
    synth::registerPlugin (engine);
    spatial::registerPlugins (engine);
    for (const auto& result : { verifyMidi (engine, folder), verifyTracks (engine, folder) })
        if (result.failed())
        {
            std::fprintf (stderr, "Composer MIDI: %s\n", result.getErrorMessage().toRawUTF8());
            return 1;
        }
    std::puts ("Composer MIDI type 0/1, join, undo, persistence and track editing passed.");
    return 0;
}
}
