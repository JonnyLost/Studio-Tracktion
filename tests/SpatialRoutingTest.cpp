// SPDX-License-Identifier: GPL-3.0-or-later
#include "../SpatialRouting.h"
#include "../SpatialState.h"
#include <limits>

namespace studio::spatial
{
namespace
{
struct OfflineEngine final : tracktion::EngineBehaviour
{
    bool autoInitialiseDeviceManager() override { return false; }
};

namespace te = tracktion;
using juce::AudioBuffer;
using juce::Result;

constexpr int testRate = 48000, blockSize = 256;
constexpr int captureBlocks = 288;

// A deterministic MIDI-driven instrument in the same post-instrument signal
// path as ARIA. The test never loads a device driver or a third-party AU.
class DryPiano final : public te::Plugin
{
public:
    static constexpr const char* xmlTypeName = "studioTestDryPiano";

    explicit DryPiano (te::PluginCreationInfo info) : Plugin (info) {}
    ~DryPiano() override { notifyListenersOfDeletion(); }

    juce::String getName() const override { return "Dry test piano"; }
    juce::String getPluginType() override { return xmlTypeName; }
    juce::String getSelectableDescription() override { return getName(); }
    bool isSynth() override { return true; }
    bool takesMidiInput() override { return true; }
    bool takesAudioInput() override { return false; }
    bool producesAudioWhenNoAudioInput() override { return true; }
    BusLayout getBusses() const override
    {
        BusLayout layout;
        layout.outputs.push_back (te::ChannelConfiguration::stereo());
        return layout;
    }
    void initialise (const te::PluginInitialisationInfo&) override { age = length; }
    void deinitialise() override {}

    void applyToBuffer (const te::PluginRenderContext& context) override
    {
        auto* audio = context.destBuffer;
        if (audio == nullptr) return;
        if (auto* midi = context.bufferForMidiMessages)
            for (const auto& event : *midi)
                if (event.isNoteOn()) age = 0;
        for (int frame = 0; frame < context.bufferNumSamples; ++frame)
        {
            float value = 0;
            if (age < length)
            {
                const float phase = (float) age * (2.0f * juce::MathConstants<float>::pi * 261.626f / testRate);
                const float window = 1.0f - (float) age++ / length;
                value = 0.35f * window * window * (std::sin (phase) + 0.4f * std::sin (2 * phase) + 0.2f * std::sin (3 * phase));
            }
            for (int ch = 0; ch < audio->getNumChannels(); ++ch)
                audio->setSample (ch, context.bufferStartSample + frame, value);
        }
    }

private:
    static constexpr int length = testRate / 7;
    int age = length;
};

class HostedDevice
{
public:
    explicit HostedDevice (te::Engine& engine)
        : devices (engine.getDeviceManager()), audio (devices.getHostedAudioDeviceInterface())
    {
        devices.setCpuLimitBeforeMuting (1000.0);
        audio.initialise ({ .sampleRate = testRate, .blockSize = blockSize, .inputChannels = 0, .outputChannels = 2 });
        audio.prepareToPlay (testRate, blockSize);
        devices.dispatchPendingUpdates();
    }

    ~HostedDevice()
    {
        devices.deviceManager.closeAudioDevice();
        devices.removeHostedAudioDeviceInterface();
    }

    AudioBuffer<float> capture (te::Edit& edit, const std::function<void()>& onPlay = {})
    {
        auto& transport = edit.getTransport();
        transport.stop (false, false);
        transport.freePlaybackContext();
        transport.setPosition (te::TimePosition());
        edit.dispatchPendingUpdatesSynchronously();
        transport.ensureContextAllocated();
        transport.play (false);
        if (onPlay) onPlay();

        AudioBuffer<float> output (2, captureBlocks * blockSize);
        AudioBuffer<float> block (2, blockSize);
        juce::MidiBuffer midi;
        for (int i = 0; i < captureBlocks; ++i)
        {
            block.clear();
            audio.processBlock (block, midi);
            for (int ch = 0; ch < 2; ++ch) output.copyFrom (ch, i * blockSize, block, ch, 0, blockSize);
            midi.clear();
        }
        transport.stop (false, false);
        transport.freePlaybackContext();
        return output;
    }

private:
    te::DeviceManager& devices;
    te::HostedAudioDeviceInterface& audio;
};

float energyDb (const AudioBuffer<float>& signal, int channel, const AudioBuffer<float>* dry = nullptr)
{
    double sum = 0;
    for (int frame = 0; frame < signal.getNumSamples(); ++frame)
    {
        const double value = signal.getSample (channel, frame) - (dry != nullptr ? dry->getSample (channel, frame) : 0.0f);
        if (! std::isfinite (value)) return std::numeric_limits<float>::quiet_NaN();
        sum += value * value;
    }
    return juce::Decibels::gainToDecibels ((float) std::sqrt (sum / signal.getNumSamples()), -160.0f);
}

Result verifySignal()
{
    te::Engine engine ("Studio spatial signal tests", {}, std::make_unique<OfflineEngine>());
    registerPlugins (engine);
    engine.getPluginManager().createBuiltInType<DryPiano>();
    HostedDevice device (engine);
    const juce::TemporaryFile temporary (".tracktionedit");
    auto edit = te::createEmptyEdit (engine, temporary.getFile());
    if (edit == nullptr) return Result::fail ("signal edit");
    auto source = edit->insertNewAudioTrack (te::TrackInsertPoint::getEndOfTracks (*edit), nullptr);
    if (source == nullptr) return Result::fail ("signal source track");
    source->getOutput().setOutputToDefaultDevice (false);
    auto instrument = edit->getPluginCache().createNewPlugin (DryPiano::xmlTypeName, {});
    if (instrument == nullptr) return Result::fail ("dry piano fixture");
    source->pluginList.insertPlugin (instrument, 0, nullptr);
    auto clip = source->insertMIDIClip ({ {}, te::TimePosition::fromSeconds (1.4) }, nullptr);
    if (clip == nullptr) return Result::fail ("dry piano MIDI fixture");
    clip->getSequence().addNote (60, te::BeatPosition::fromBeats (0.5), te::BeatDuration::fromBeats (0.2), 96, 0, nullptr);
    edit->getMasterVolumePlugin()->setVolumeDb (0.0f);
    source->getVolumePlugin()->setVolumeDb (0.0f);

    Room room;
    if (auto result = setRoomSend (*source, true, { 0, 0 }, Seat().sendTrim, room); result.failed()) return result;
    auto* returned = roomReturnTrack (*edit);
    if (returned == nullptr || returned->getVolumePlugin() == nullptr || returned->getLevelMeterPlugin() == nullptr)
        return Result::fail ("room return fader and meter");
    returned->getVolumePlugin()->setVolumeDb (0.0f);
    room.bypass = true;
    updateReturn (*edit, room);
    auto dry = device.capture (*edit);
    const auto dryL = energyDb (dry, 0);
    if (! std::isfinite (dryL) || dryL < -36.0f) return Result::fail ("live graph dry piano did not reach the master, dBFS " + juce::String (dryL));

    room.bypass = false;
    updateReturn (*edit, room);
    auto front = device.capture (*edit, [&] { updateRoomSend (*source, { 0, 0 }, Seat().sendTrim); });
    auto rear = device.capture (*edit, [&] { updateRoomSend (*source, { 0, 1 }, Seat().sendTrim); });
    const auto frontWet = energyDb (front, 0, &dry);
    const auto rearWet = energyDb (rear, 0, &dry);
    const auto rightWet = energyDb (rear, 1, &dry);
    fprintf (stdout, "Hosted device RMS dBFS: dry L %.2f, front wet L %.2f, rear wet L %.2f, rear wet R %.2f.\n",
             (double) dryL, (double) frontWet, (double) rearWet, (double) rightWet);
    if (! std::isfinite (frontWet) || ! std::isfinite (rearWet) || ! std::isfinite (rightWet) || rearWet < -125.0f)
        return Result::fail ("room audio did not reach the master");
    return Result::ok();
}

int countReturns (tracktion::Edit& edit)
{
    int count = 0;
    for (auto* track : tracktion::getAudioTracks (edit))
        if (isRoomReturn (*track)) ++count;
    return count;
}

tracktion::AuxSendPlugin* auxSend (tracktion::AudioTrack& track)
{
    for (auto* plugin : track.pluginList)
        if (auto* send = dynamic_cast<tracktion::AuxSendPlugin*> (plugin)) return send;
    return nullptr;
}

juce::Result verify()
{
    namespace te = tracktion;
    using juce::Result;
    te::Engine engine ("Studio spatial tests", {}, std::make_unique<OfflineEngine>());
    registerPlugins (engine);
    const juce::TemporaryFile temporary (".tracktionedit");
    const auto file = temporary.getFile();
    auto edit = te::createEmptyEdit (engine, file);
    if (edit == nullptr) return Result::fail ("create edit");
    auto sourceOwner = edit->insertNewAudioTrack (te::TrackInsertPoint::getEndOfTracks (*edit), nullptr);
    if (sourceOwner == nullptr) return Result::fail ("source track");
    auto* source = sourceOwner.get();
    source->getOutput().setOutputToDefaultDevice (false);
    sourceOwner = nullptr;
    auto sourceID = source->itemID;

    auto blocked = edit->insertNewAudioTrack (te::TrackInsertPoint::getEndOfTracks (*edit), nullptr);
    auto oldSend = edit->getPluginCache().createNewPlugin (te::AuxSendPlugin::xmlTypeName, {});
    if (blocked == nullptr || oldSend == nullptr) return Result::fail ("existing routing fixture");
    blocked->getOutput().setOutputToDefaultDevice (false);
    dynamic_cast<te::AuxSendPlugin&> (*oldSend).busNumber = 15;
    blocked->pluginList.insertPlugin (oldSend, blocked->pluginList.size(), nullptr);
    if (setRoomSend (*blocked, true, {}, -12, {}).wasOk()) return Result::fail ("existing aux isolation");
    blocked = nullptr;
    oldSend = nullptr;
    auto reverbTrack = edit->insertNewAudioTrack (te::TrackInsertPoint::getEndOfTracks (*edit), nullptr);
    auto insert = edit->getPluginCache().createNewPlugin (te::ReverbPlugin::xmlTypeName, {});
    if (reverbTrack == nullptr || insert == nullptr) return Result::fail ("existing reverb fixture");
    reverbTrack->getOutput().setOutputToDefaultDevice (false);
    reverbTrack->pluginList.insertPlugin (insert, reverbTrack->pluginList.size(), nullptr);
    if (setRoomSend (*reverbTrack, true, {}, -12, {}).wasOk() || hasRoomSend (*reverbTrack))
        return Result::fail ("existing reverb isolation");
    reverbTrack = nullptr;
    insert = nullptr;
    if (countReturns (*edit) != 0 || hasRoomSend (*source)) return Result::fail ("default routing");

    auto& undo = edit->getUndoManager();
    undo.beginNewTransaction ("Test enabling stage audio");
    const Position seat { -0.25f, 0.6f };
    if (auto result = setRoomSend (*source, true, seat, -12, factoryRooms[0].room); result.failed()) return result;
    auto* send = auxSend (*source);
    if (countReturns (*edit) != 1 || send == nullptr || send->getBusNumber() == 15
        || std::abs (send->getGainDb() - sendDb (seat, -12)) > 0.1f)
        return Result::fail ("allocated return and effective send");
    undo.beginNewTransaction ("Test send trim");
    updateRoomSend (*source, seat, -32);
    undo.beginNewTransaction();
    if (std::abs (send->getGainDb() - sendDb (seat, -32)) > 0.1f || ! undo.undo()
        || std::abs (send->getGainDb() - sendDb (seat, -12)) > 0.1f
        || ! undo.redo() || std::abs (send->getGainDb() - sendDb (seat, -32)) > 0.1f)
        return Result::fail ("live send gain or undo");
    const auto returnID = roomReturnTrack (*edit)->itemID;
    const auto bus = send->getBusNumber();
    auto secondSource = edit->insertNewAudioTrack (te::TrackInsertPoint::getEndOfTracks (*edit), nullptr);
    if (secondSource == nullptr) return Result::fail ("second source");
    secondSource->getOutput().setOutputToDefaultDevice (false);
    if (auto result = setRoomSend (*secondSource, true, { 0.3f, 0.3f }, -18, {}); result.failed()) return result;
    if (countReturns (*edit) != 1 || auxSend (*secondSource) == nullptr || auxSend (*secondSource)->getBusNumber() != bus
        || ! source->getOutput().usesDefaultAudioOut() || ! secondSource->getOutput().usesDefaultAudioOut())
        return Result::fail ("shared return and independent direct routes");
    const auto secondID = secondSource->itemID;
    secondSource = nullptr;
    if (! te::EditFileOperations (*edit).writeToFile (file, false)) return Result::fail ("write project");
    edit.reset();
    edit = te::loadEditFromFile (engine, file);
    if (edit == nullptr) return Result::fail ("reopen project");
    source = te::findAudioTrackForID (*edit, sourceID);
    auto* returned = roomReturnTrack (*edit);
    if (source == nullptr || returned == nullptr || returned->itemID != returnID || countReturns (*edit) != 1)
        return Result::fail ("saved identities");
    send = auxSend (*source);
    if (! hasRoomSend (*source) || send == nullptr || send->getBusNumber() != bus
        || std::abs (send->getGainDb() - sendDb (seat, -32)) > 0.1f)
        return Result::fail ("restored routing");
    auto& nextUndo = edit->getUndoManager();
    nextUndo.beginNewTransaction ("Disable stage audio");
    if (auto result = setRoomSend (*source, false, seat, -12, {}); result.failed()) return result;
    nextUndo.beginNewTransaction();
    auto* second = te::findAudioTrackForID (*edit, secondID);
    if (hasRoomSend (*source) || second == nullptr || ! hasRoomSend (*second)
        || ! nextUndo.undo() || ! hasRoomSend (*source) || countReturns (*edit) != 1)
        return Result::fail ("undo route removal");
    return Result::ok();
}
}

int runSpatialRoutingTest()
{
    auto result = verify();
    if (result.wasOk()) result = verifySignal();
    if (result.failed()) fprintf (stderr, "Studio spatial routing test failed: %s\n", result.getErrorMessage().toRawUTF8());
    else fprintf (stdout, "Tracktion room send, undo, isolation, and native project reopen passed.\n");
    return result.wasOk() ? 0 : 1;
}
}
