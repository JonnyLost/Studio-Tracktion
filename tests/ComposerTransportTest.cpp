// SPDX-License-Identifier: GPL-3.0-or-later
#include "../MusicalTransport.h"
#include "../MusicalRuler.h"
#include "../StudioSynth.h"
#include <cstdio>

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
    const ScopeGuard cleanup { [&] { devices.deviceManager.closeAudioDevice(); devices.removeHostedAudioDeviceInterface(); } };

    auto edit = te::createEmptyEdit (engine, fixture.getFile());
    if (edit == nullptr) return fail ("create project");
    for (int index = 0; index < 3; ++index)
    {
        auto track = edit->insertNewAudioTrack (te::TrackInsertPoint::getEndOfTracks (*edit), nullptr);
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
    std::puts ("Composer musical transport, cycle, undo and persistence passed.");
    return 0;
}
}
