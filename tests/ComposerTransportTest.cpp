// SPDX-License-Identifier: GPL-3.0-or-later
#include "../MusicalTransport.h"
#include "../MusicalRuler.h"
#include "../StudioSynth.h"
#include <cstdio>
#include <thread>

namespace studio
{
int runComposerTransportTest()
{
    namespace te = tracktion;
    using namespace juce;
    using namespace te::literals;
    const auto fail = [] (const char* reason)
    {
        std::fprintf (stderr, "Composer transport: %s\n", reason);
        return 1;
    };
    TemporaryFile clickFixture (".wav");
    if (! writeMetronomeWave (clickFixture.getFile(), 1000, 5, 0.45f)
        || writeMetronomeWave (clickFixture.getFile(), 1000, 0, 0.45f))
        return fail ("write short click and reject invalid settings");
    WavAudioFormat wav;
    std::unique_ptr<AudioFormatReader> reader (wav.createReaderFor (clickFixture.getFile().createInputStream().release(), true));
    if (reader == nullptr || reader->sampleRate != 48000 || reader->lengthInSamples != 240 || reader->numChannels != 1)
        return fail ("generated click duration and format");
    reader.reset();
    TemporaryFile fixture (".tracktionedit");
    te::Engine engine ("Studio Composer Transport Test");
    synth::registerPlugin (engine);
    auto& devices = engine.getDeviceManager();
    devices.setCpuLimitBeforeMuting (1000);
    auto& audio = devices.getHostedAudioDeviceInterface();
    constexpr int rate = 48000, blockSize = 256;
    audio.initialise ({ .sampleRate = rate, .blockSize = blockSize, .inputChannels = 0, .outputChannels = 2 });
    audio.prepareToPlay (rate, blockSize);
    devices.dispatchPendingUpdates();
    // Let the hosted MIDI scan settle before holding an input instance or
    // starting a take. A device rescan rebuilds playback input instances.
    MessageManager::getInstance()->runDispatchLoopUntil (100);
    const ScopeGuard cleanup { [&] { devices.deviceManager.closeAudioDevice(); devices.removeHostedAudioDeviceInterface(); } };

    auto edit = te::createEmptyEdit (engine, fixture.getFile());
    if (edit == nullptr) return fail ("create project");
    for (int index = 0; index < 3; ++index)
    {
        te::AudioTrack::Ptr track = index == 0 ? te::AudioTrack::Ptr (te::getAudioTracks (*edit).getFirst())
            : edit->insertNewAudioTrack (te::TrackInsertPoint::getEndOfTracks (*edit), nullptr);
        if (track == nullptr) return fail ("create track");
        track->setName ("Cue " + String (index + 1));
        auto synth = edit->getPluginCache().createNewPlugin (studio::synth::Plugin::xmlTypeName, {});
        if (synth == nullptr) return fail ("create native instrument");
        track->pluginList.insertPlugin (synth, 0, nullptr);
        auto clip = track->insertMIDIClip ("Theme", { 0_tp, 4_tp }, nullptr);
        if (clip == nullptr) return fail ("create clip");
        clip->getSequence().addNote (60 + index * 4, 0_bp, 2_bd, 96, 0, nullptr);
        clip->getSequence().addControllerEvent (0_bp, 64, 127 << 7, nullptr);
        clip->getSequence().addControllerEvent (1_bp, 64, 0, nullptr);
    }
    if (! setTempoEvent (*edit, 4_bp, 90) || ! setMeterEvent (*edit, 4_bp, 3, 4))
        return fail ("insert a tempo and meter change");
    if (edit->tempoSequence.getTimeSigAt (4_bp).numerator != 3
        || edit->tempoSequence.getTempoAt (4_bp).getBpm() != 90
        || edit->tempoSequence.toBeats (te::tempo::BarsAndBeats { 2, {} }) != 7_bp)
        return fail ("tempo and meter map disagree with bars");
    if (! edit->getUndoManager().undo() || edit->tempoSequence.getTimeSigAt (4_bp).numerator != 4
        || ! edit->getUndoManager().redo() || edit->tempoSequence.getTimeSigAt (4_bp).numerator != 3)
        return fail ("meter undo and redo");
    if (addCueMarker (*edit, 2_tp, "Middle") == nullptr || ! setCycle (*edit, { 0_tp, 1_tp }))
        return fail ("create cue and cycle");
    if (setCycle (*edit, { 0_tp, 0_tp })) return fail ("reject an empty cycle");
    MusicalTransport controls;
    controls.setEdit (edit.get());
    controls.toggleCycle();
    if (edit->getTransport().looping || edit->getTransport().getLoopRange() != te::TimeRange (0_tp, 1_tp))
        return fail ("cycle off preserves range");
    controls.toggleCycle();
    if (! edit->getTransport().looping) return fail ("cycle restores previous range");
    controls.setEdit (nullptr);

    auto& transport = edit->getTransport();
    edit->dispatchPendingUpdatesSynchronously();
    transport.ensureContextAllocated (true);
    transport.setPosition (0.5_tp);
    transport.play (false);
    AudioBuffer<float> buffer (2, blockSize);
    MidiBuffer midi;
    float peak = 0;
    for (int block = 0; block < 150; ++block)
    {
        buffer.clear();
        midi.clear();
        audio.processBlock (buffer, midi);
        peak = jmax (peak, buffer.getMagnitude (0, blockSize));
    }
    auto* context = transport.getCurrentPlaybackContext();
    if (peak < 0.0005f || context == nullptr || ! te::TimeRange (0_tp, 1_tp).contains (context->getPosition()))
        return fail ("audible playback wrapping into the cycle");
    transport.stop (false, false);
    transport.freePlaybackContext();
    transport.setPosition (2_tp);
    edit->clickTrackEnabled = true;

    if (! te::EditFileOperations (*edit).writeToFile (fixture.getFile(), false)) return fail ("save project");
    edit.reset();
    edit = te::loadEditFromFile (engine, fixture.getFile());
    if (edit == nullptr || te::getAudioTracks (*edit).size() != 3
        || ! edit->getTransport().looping || edit->getTransport().getLoopRange() != te::TimeRange (0_tp, 1_tp)
        || ! edit->clickTrackEnabled || edit->tempoSequence.getTempoAt (4_bp).getBpm() != 90
        || edit->tempoSequence.getTimeSigAt (4_bp).numerator != 3)
        return fail ("reopen project with transport settings and tempo map");
    auto markers = edit->getMarkerManager().getMarkers();
    if (markers.size() != 1 || markers.getFirst()->getName() != "Middle") return fail ("reopen cue marker");
    auto& record = edit->getTransport();
    record.looping = false;
    record.ensureContextAllocated (true);
    auto* target = te::getAudioTracks (*edit).getFirst();
    bool armed = false;
    for (auto* device : record.getCurrentPlaybackContext()->getAllInputs())
        if (auto* midiInput = dynamic_cast<te::MidiInputDevice*> (&device->owner);
            midiInput != nullptr && te::HostedAudioDeviceInterface::isHostedMidiInputDevice (*midiInput))
        {
            midiInput->mergeRecordings = false;
            midiInput->replaceExistingClips = false;
            if (auto result = device->setTarget (target->itemID, true, &edit->getUndoManager(), 0); result.has_value())
            {
                device->setRecordingEnabled (target->itemID, true);
                armed = true;
            }
            break;
        }
    if (! armed) return fail ("arm hosted MIDI input");
    edit->dispatchPendingUpdatesSynchronously();
    edit->clickTrackEnabled = false;
    edit->setCountInMode (te::Edit::CountIn::oneBeat);
    record.setPosition (5_tp);
    std::atomic<int> pending { 0 };
    std::atomic<float> clickPeak { 0 };
    {
        std::atomic<bool> stopAudio { false };
        std::thread audioBlocks ([&]
        {
            AudioBuffer<float> block (2, blockSize);
            block.clear();
            MidiBuffer input;
            while (! stopAudio.load())
            {
                const auto message = pending.exchange (0);
                if (message == 1) input.addEvent (MidiMessage::noteOn (1, 72, (uint8) 101), 0);
                if (message == 2) input.addEvent (MidiMessage::noteOff (1, 72), 0);
                audio.processBlock (block, input);
                clickPeak.store (jmax (clickPeak.load(), block.getMagnitude (0, blockSize)));
                block.clear();
                input.clear();
                std::this_thread::sleep_for (std::chrono::milliseconds (5));
            }
        });
        const ScopeGuard joinAudio { [&]
        {
            stopAudio.store (true);
            audioBlocks.join();
        } };
        record.record (false);
        if (! record.isRecording()) return fail ("start recording with count-in");
        auto* playback = record.getCurrentPlaybackContext();
        const auto waitFor = [playback] (te::TimePosition position)
        {
            const auto deadline = Time::getMillisecondCounterHiRes() + 5000;
            while (playback->getUnloopedPosition() < position && Time::getMillisecondCounterHiRes() < deadline)
                MessageManager::getInstance()->runDispatchLoopUntil (5);
            return playback->getUnloopedPosition() >= position;
        };
        if (! waitFor (5.05_tp) || clickPeak.load() < 0.001f)
            return fail ("audible count-in with the regular metronome disabled");
        pending = 1;
        if (! waitFor (5.25_tp)) return fail ("advance MIDI recording");
        pending = 2;
        if (! waitFor (5.45_tp)) return fail ("release recorded note");
        record.stop (false, false);
    }
    const auto deadline = Time::getMillisecondCounterHiRes() + 5000;
    while (! transportSettled (*edit) && Time::getMillisecondCounterHiRes() < deadline)
        MessageManager::getInstance()->runDispatchLoopUntil (10);
    edit->dispatchPendingUpdatesSynchronously();
    edit->setCountInMode (te::Edit::CountIn::none);
    const auto clips = target->getClips();
    const bool recorded = std::any_of (clips.begin(), clips.end(), [] (const auto* clip)
    {
        if (auto* midi = dynamic_cast<const te::MidiClip*> (clip))
            for (const auto* note : midi->getSequence().getNotes())
                if (note->getNoteNumber() == 72 && note->getVelocity() == 101)
                    return true;
        return false;
    });
    if (! transportSettled (*edit) || ! recorded) return fail ("Stop must finalize and retain the MIDI take");
    std::puts ("Composer musical transport, cycle, undo and persistence passed.");
    return 0;
}
}
