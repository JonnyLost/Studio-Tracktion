// SPDX-License-Identifier: GPL-3.0-or-later
#include "../SamplerEditor.h"
#include "../SpatialRouting.h"
#include "../SpatialState.h"
#include "../StudioMixing.h"
#include <cmath>
#include <cstdio>
#include <numbers>

namespace studio::sampler
{
namespace
{
namespace te = tracktion;
using namespace juce;
using namespace tracktion::literals;
constexpr int blockSize = 256, rate = 48000;

struct OfflineEngine final : te::EngineBehaviour
{
    bool autoInitialiseDeviceManager() override { return false; }
};

struct TestProperties final : te::PropertyStorage
{
    explicit TestProperties (const File& folder) : PropertyStorage ("Studio sampler test"), directory (folder) {}
    File getAppPrefsFolder() override { return directory; }
    File directory;
};

class Playback
{
public:
    explicit Playback (te::Engine& engine) : devices (engine.getDeviceManager()), device (devices.getHostedAudioDeviceInterface())
    {
        devices.setCpuLimitBeforeMuting (1000);
        device.initialise ({ .sampleRate = rate, .blockSize = blockSize, .inputChannels = 0, .outputChannels = 2 });
        device.prepareToPlay (rate, blockSize);
        devices.rescanMidiDeviceList();
        for (int attempt = 0; attempt < 10 && devices.getNumMidiInDevices() == 0; ++attempt)
            MessageManager::getInstance()->runDispatchLoopUntil (20);
        devices.dispatchPendingUpdates();
        for (auto& input : devices.getMidiInDevices())
            if (te::HostedAudioDeviceInterface::isHostedMidiInputDevice (*input))
                input->setEnabled (true);
        MessageManager::getInstance()->runDispatchLoopUntil (20);
        devices.dispatchPendingUpdates();
    }
    ~Playback()
    {
        devices.deviceManager.closeAudioDevice();
        devices.removeHostedAudioDeviceInterface();
    }

    float block (MidiBuffer midi = {})
    {
        audio.clear();
        device.processBlock (audio, midi);
        return audio.getRMSLevel (0, 0, blockSize);
    }

    float peak (int blocks)
    {
        float peak = 0;
        for (int i = 0; i < blocks; ++i) peak = jmax (peak, block());
        return peak;
    }
private:
    te::DeviceManager& devices;
    te::HostedAudioDeviceInterface& device;
    AudioBuffer<float> audio { 2, blockSize };
};

bool testSample (const File& file, AudioFormat& format, double frequency, int channels)
{
    std::unique_ptr<OutputStream> stream = file.createOutputStream();
    if (! stream) return false;
    const auto options = AudioFormatWriterOptions().withSampleRate (rate / 2).withNumChannels (channels).withBitsPerSample (24);
    auto writer = format.createWriterFor (stream, options);
    if (! writer) return false;
    AudioBuffer<float> data (channels, rate / 2 * 3);
    for (int ch = 0; ch < channels; ++ch)
        for (int i = 0; i < data.getNumSamples(); ++i)
        {
            const auto time = static_cast<double> (i) / (rate / 2);
            const auto signal = 0.53 * std::exp (-time * 0.8) * (std::sin (2 * std::numbers::pi * frequency * time)
                         + 0.29 * std::sin (4 * std::numbers::pi * frequency * time + ch * 0.6));
            data.setSample (ch, i, static_cast<float> (signal));
        }
    return writer->writeFromAudioSampleBuffer (data, 0, data.getNumSamples());
}

Plugin* addSampler (te::Edit& edit, te::AudioTrack& track)
{
    auto instrument = edit.getPluginCache().createNewPlugin (Plugin::xmlTypeName, {});
    if (! instrument) return nullptr;
    track.pluginList.insertPlugin (instrument, 0, nullptr);
    return dynamic_cast<Plugin*> (findInstrument (track));
}

Result recordMidi (te::Edit& edit, te::AudioTrack& track, Playback& playback)
{
    auto& transport = edit.getTransport();
    transport.setPosition (3_tp);
    transport.ensureContextAllocated();

    bool routed = false;
    for (auto* instance : edit.getAllInputDevices())
        if (auto* input = dynamic_cast<te::MidiInputDevice*> (&instance->getInputDevice());
            input && te::HostedAudioDeviceInterface::isHostedMidiInputDevice (*input))
        {
            input->setMonitorMode (te::InputDevice::MonitorMode::automatic);
            if (! instance->setTarget (track.itemID, true, nullptr, 0))
                return Result::fail ("route hosted input for sampler recording");
            instance->setRecordingEnabled (track.itemID, true);
            routed = true;
            break;
        }
    if (! routed) return Result::fail ("hosted MIDI input");

    edit.dispatchPendingUpdatesSynchronously();
    transport.record (false);
    if (! transport.isRecording()) return Result::fail ("start sampler MIDI take");
    playback.peak (16);
    MidiBuffer events;
    events.addEvent (MidiMessage::controllerEvent (3, 11, 90), 0);
    events.addEvent (MidiMessage::noteOn (3, 60, static_cast<uint8> (90)), 0);
    playback.block (events);
    if (playback.peak (20) < .01f) return Result::fail ("monitor live MIDI during recording");
    events.clear();
    events.addEvent (MidiMessage::noteOff (3, 60), 0);
    playback.block (events);
    playback.peak (20);
    transport.stop (false, false);
    edit.dispatchPendingUpdatesSynchronously();

    for (auto* item : track.getClips())
        if (auto* take = dynamic_cast<te::MidiClip*> (item); take && take->getPosition().getStart() >= 2_tp)
        {
            const auto& sequence = take->getSequence();
            if (take->getMidiChannel().getChannelNumber() != 3 || sequence.getNumNotes() != 1
                || sequence.getNotes().getFirst()->getNoteNumber() != 60)
                return Result::fail ("retain recorded note on MIDI channel 3");
            const auto& controllers = sequence.getControllerEvents();
            const auto hasExpression = std::any_of (controllers.begin(), controllers.end(), [] (const auto* event)
            {
                return event->getType() == 11 && event->getControllerValue() == (90 << 7);
            });
            if (! hasExpression) return Result::fail ("retain recorded CC11 after Stop");

            transport.setPosition (take->getPosition().getStart());
            transport.play (false);
            const auto signal = playback.peak (45);
            transport.stop (false, false);
            transport.freePlaybackContext();
            return signal > .01f ? Result::ok() : Result::fail ("play recorded sampler take");
        }
    return Result::fail ("MIDI take was not retained after Stop");
}

Result run (const File& screenshots)
{
    const auto directory = File::getSpecialLocation (File::tempDirectory)
        .getNonexistentChildFile ("studio-sampler-test", {}, false);
    if (directory.createDirectory().failed()) return Result::fail ("create fixture directory");
    const ScopeGuard cleanup { [directory] { directory.deleteRecursively(); } };
    te::Engine engine (std::make_unique<TestProperties> (directory), {}, std::make_unique<OfflineEngine>());
    registerPlugin (engine);
    spatial::registerPlugins (engine);
    Playback playback (engine);
    const auto project = directory.getChildFile ("Sampler Test.tracktionedit");
    const auto wavFile = directory.getChildFile ("Piano C4.wav");
    const auto aiffFile = directory.getChildFile ("Piano G4.aiff");
    const auto preset = directory.getChildFile ("Sampler Test.studiosampler");
    WavAudioFormat wav;
    AiffAudioFormat aiff;
    if (! testSample (wavFile, wav, 261.626, 2)
        || ! testSample (aiffFile, aiff, 391.995, 1))
        return Result::fail ("write 24-bit WAV and AIFF fixtures");

    auto edit = te::createEmptyEdit (engine, project);
    if (! edit) return Result::fail ("create Tracktion project");
    auto tracks = te::getAudioTracks (*edit);
    auto first = tracks.isEmpty() ? edit->insertNewAudioTrack (te::TrackInsertPoint::getEndOfTracks (*edit), nullptr)
                                  : te::AudioTrack::Ptr (tracks.getFirst());
    auto second = edit->insertNewAudioTrack (te::TrackInsertPoint::getEndOfTracks (*edit), nullptr);
    if (! first || ! second) return Result::fail ("create tracks");
    first->getOutput().setOutputToDefaultDevice (false);
    second->getOutput().setOutputToDefaultDevice (false);
    first->setName ("Sampler test");
    second->setName ("Second sampler");
    auto* sampler = addSampler (*edit, *first);
    auto* other = addSampler (*edit, *second);
    if (! sampler || ! other) return Result::fail ("register, insert and identify two native instruments");
    edit->playInStopEnabled = true;
    auto& transport = edit->getTransport();
    edit->dispatchPendingUpdatesSynchronously();
    transport.ensureContextAllocated (true);
    playback.peak (3);
    {
        Editor editor (*sampler);
        DocumentWindow window ("Studio Sampler", colours::panel, 0);
        window.setUsingNativeTitleBar (true);
        window.setContentNonOwned (&editor, true);
        window.centreWithSize (window.getWidth(), window.getHeight());
        window.setVisible (true);
        if (! editor.runReview (wavFile, aiffFile, screenshots, [&] { return playback.peak (8) > 0.01f; }))
            return Result::fail ("editor interactions or screenshots");
    }

    if (sampler->instrument().zones.size() != 2 || sampler->savePreset (preset).failed()
        || other->loadPreset (preset).failed() || other->instrument().zones.size() != 2
        || other->instrument().settings.attack != sampler->instrument().settings.attack
        || other->instrument().zones[0].mapping.loop != sampler->instrument().zones[0].mapping.loop)
        return Result::fail ("preserve instrument parameters through preset");
    other->removeZone (1);
    if (sampler->instrument().zones.size() != 2 || other->instrument().zones.size() != 1)
        return Result::fail ("instances must be editable independently");

    auto clip = first->insertMIDIClip ("Sampler notes", { 0_tp, 2_tp }, nullptr);
    if (! clip) return Result::fail ("create sampler MIDI clip");
    clip->setMidiChannel (te::MidiChannel (3));
    clip->getSequence().addNote (60, 0_bp, 1_bd, 96, 0, nullptr);
    clip->getSequence().addControllerEvent (0_bp, 11, 110 << 7, nullptr);
    spatial::Room room;
    if (auto send = spatial::setRoomSend (*first, true, { 0, 0 }, spatial::Seat().sendTrim, room); send.failed())
        return send;
    edit->dispatchPendingUpdatesSynchronously();
    transport.setPosition (0_tp);
    transport.play (false);
    const auto signal = playback.peak (30);
    if (signal < .01f || ! std::isfinite (signal)) return Result::fail ("clip playback through Studio master");
    const auto wet = spatial::roomSignal (*edit);
    if (! wet || wet->wetPeak <= 0 || ! wet->reachesMaster) return Result::fail ("sampler reaches shared Room Return");
    transport.setPosition (1.5_tp, 1.5_tp);
    playback.peak (12);
    if (sampler->resourceSnapshot().activeVoices.value_or (1) != 0)
        return Result::fail ("transport seek must clear sampler voices");
    transport.setPosition (0_tp, 0_tp);
    if (playback.peak (20) < .01f) return Result::fail ("note retriggers after seek");
    transport.stop (false, false);
    playback.peak (200);
    if (sampler->resourceSnapshot().activeVoices.value_or (1) != 0)
        return Result::fail ("transport stop must clear sampler voices");
    transport.freePlaybackContext();

    if (auto recorded = recordMidi (*edit, *second, playback); recorded.failed()) return recorded;

    if (! te::EditFileOperations (*edit).writeToFile (project, false))
        return Result::fail ("write Tracktion edit");
    clip = nullptr;
    first = nullptr;
    second = nullptr;
    edit.reset();

    if (! aiffFile.deleteFile()) return Result::fail ("remove AIFF to exercise missing-sample recovery");
    edit = te::loadEditFromFile (engine, project);
    if (! edit) return Result::fail ("load Tracktion edit");
    sampler = other = nullptr;
    for (auto* track : te::getAudioTracks (*edit))
        if (track->getName() == "Sampler test") sampler = dynamic_cast<Plugin*> (findInstrument (*track));
        else if (track->getName() == "Second sampler") other = dynamic_cast<Plugin*> (findInstrument (*track));
    if (! sampler || ! other || sampler->instrument().zones.size() != 2 || other->instrument().zones.size() != 1)
        return Result::fail ("restore both instances");
    auto firstZone = sampler->instrument().zones[0];
    auto missing = sampler->instrument().zones[1];
    if (! firstZone.mapping.sample || ! firstZone.mapping.loop || firstZone.mapping.high != 64
        || std::abs (sampler->instrument().settings.attack - .023f) > .0001f
        || missing.mapping.sample || missing.file != aiffFile || missing.problem.isEmpty())
        return Result::fail ("restored parameters and missing sample");
    if (sampler->loadSample (wavFile, 1).failed()
        || sampler->instrument().zones[1].mapping.low != missing.mapping.low)
        return Result::fail ("relink sample and retain mapping");
    edit->playInStopEnabled = true;
    auto& restoredTransport = edit->getTransport();
    restoredTransport.ensureContextAllocated();
    restoredTransport.setPosition (0_tp);
    restoredTransport.play (false);
    if (playback.peak (30) < .01f) return Result::fail ("reopened project plays through the master");
    restoredTransport.stop (false, false);
    restoredTransport.freePlaybackContext();
    return Result::ok();
}
}

int runSamplerIntegrationTest (const juce::File& screenshots)
{
    const auto result = run (screenshots);
    if (result.failed())
    {
        std::fprintf (stderr, "Studio Sampler integration failed: %s\n", result.getErrorMessage().toRawUTF8());
        return 1;
    }
    std::puts ("Studio Sampler editor, hosted MIDI recording and playback, Room Return and restoration passed.");
    return 0;
}
}
