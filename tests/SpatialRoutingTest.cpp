// SPDX-License-Identifier: GPL-3.0-or-later
#include "../SpatialRouting.h"
#include "../SpatialState.h"
#include "../StudioSpatialEffect.h"
#include "../StudioReverbEffect.h"
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

    struct Capture
    {
        AudioBuffer<float> output;
        std::array<float, 2> returnDb { -100.0f, -100.0f };
        std::optional<RoomSignal> room;
    };

    Capture capture (te::Edit& edit, const std::function<void()>& onPlay = {}, int seekBlock = -1)
    {
        auto& transport = edit.getTransport();
        transport.stop (false, false);
        transport.freePlaybackContext();
        transport.setPosition (te::TimePosition());
        edit.dispatchPendingUpdatesSynchronously();
        transport.ensureContextAllocated();
        transport.play (false);
        if (onPlay) onPlay();

        te::LevelMeasurer::Client meter;
        auto* returned = roomReturnTrack (edit);
        auto* level = returned != nullptr ? returned->getLevelMeterPlugin() : nullptr;
        if (level != nullptr) level->measurer.addClient (meter);

        Capture result;
        result.output.setSize (2, captureBlocks * blockSize);
        AudioBuffer<float> block (2, blockSize);
        juce::MidiBuffer midi;
        for (int i = 0; i < captureBlocks; ++i)
        {
            if (i == seekBlock) transport.setPosition (te::TimePosition(), te::TimePosition());
            block.clear();
            audio.processBlock (block, midi);
            for (int ch = 0; ch < 2; ++ch) result.output.copyFrom (ch, i * blockSize, block, ch, 0, blockSize);
            midi.clear();
        }
        result.room = roomSignal (edit);
        if (level != nullptr)
        {
            for (int ch = 0; ch < 2; ++ch) result.returnDb[(size_t) ch] = meter.getAndClearAudioLevel (ch).dB;
            level->measurer.removeClient (meter);
        }
        transport.stop (false, false);
        transport.freePlaybackContext();
        return result;
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
    const auto dryL = energyDb (dry.output, 0);
    if (! std::isfinite (dryL) || dryL < -36.0f)
        return Result::fail ("live graph dry piano did not reach the master, dBFS " + juce::String (dryL));
    if (! dry.room || dry.room->wetPeak != 0.0f || dry.returnDb[0] > -90.0f)
        return Result::fail ("bypassed return must be silent");

    room.bypass = false;
    updateReturn (*edit, room);
    auto front = device.capture (*edit, [&] { updateRoomSend (*source, { 0, 0 }, Seat().sendTrim); });
    auto rear = device.capture (*edit, [&] { updateRoomSend (*source, { 0, 1 }, Seat().sendTrim); });
    const auto frontWet = energyDb (front.output, 0, &dry.output);
    const auto rearWet = energyDb (rear.output, 0, &dry.output);
    const auto rightWet = energyDb (rear.output, 1, &dry.output);
    fprintf (stdout, "Hosted device RMS dBFS: dry L %.2f, front wet L %.2f, rear wet L %.2f, rear wet R %.2f.\n",
             (double) dryL, (double) frontWet, (double) rearWet, (double) rightWet);
    if (! std::isfinite (frontWet) || ! std::isfinite (rearWet) || ! std::isfinite (rightWet)
        || std::abs (rearWet - frontWet - 10.0f) > 0.8f
        || std::min (rearWet, rightWet) < dryL - 23.0f || ! rear.room || ! rear.room->reachesMaster
        || rear.room->inputPeak <= 0.0f || rear.room->wetPeak <= 0.0f
        || std::min (rear.returnDb[0], rear.returnDb[1]) < -55.0f)
        return Result::fail ("audible stereo wet signal, room metering, or live depth gain");

    AudioBuffer<float> wetStereo (rear.output);
    wetStereo.addFrom (0, 0, dry.output, 0, 0, wetStereo.getNumSamples(), -1.0f);
    wetStereo.addFrom (1, 0, dry.output, 1, 0, wetStereo.getNumSamples(), -1.0f);
    wetStereo.addFrom (0, 0, wetStereo, 1, 0, wetStereo.getNumSamples(), -1.0f);
    if (energyDb (wetStereo, 0) < rearWet - 12.0f)
        return Result::fail ("wet left and right must carry distinct reflections");

    returned->getVolumePlugin()->setVolumeDb (-12.0f);
    auto lower = device.capture (*edit);
    if (std::abs (rearWet - energyDb (lower.output, 0, &dry.output) - 12.0f) > 0.7f
        || std::abs (rear.returnDb[0] - lower.returnDb[0] - 12.0f) > 0.7f)
        return Result::fail ("room return fader and post-fader meter");
    returned->getVolumePlugin()->setVolumeDb (0.0f);

    source->setSolo (true);
    auto solo = device.capture (*edit);
    if (! returned->isSoloIsolate (false) || std::abs (rearWet - energyDb (solo.output, 0, &dry.output)) > 0.5f)
        return Result::fail ("managed room return must remain audible when an instrument is soloed");
    source->setSolo (false);

    setRoomCheck (*edit, true);
    auto boosted = device.capture (*edit);
    const float checkDelta = energyDb (boosted.output, 0, &dry.output) - rearWet;
    if (! boosted.room || ! boosted.room->checkEnabled || std::abs (checkDelta - 12.0f) > 0.7f)
        return Result::fail ("temporary wet-only room check");
    setRoomCheck (*edit, false);
    auto stable = device.capture (*edit);
    if (energyDb (stable.output, 0, &rear.output) > -95.0f)
        return Result::fail ("room check should restore the original wet level");

    const auto stone = factoryRooms[5].room;
    auto changed = device.capture (*edit, [&] { updateReturn (*edit, stone); });
    if (energyDb (changed.output, 0, &rear.output) < rearWet - 10.0f)
        return Result::fail ("room preset did not change the audible response while playing");
    auto expected = device.capture (*edit);
    const int seekBlock = captureBlocks / 2, secondStart = (seekBlock + 30) * blockSize;
    auto seek = device.capture (*edit, {}, seekBlock);
    for (int ch = 0; ch < 2; ++ch)
        if (seek.output.getMagnitude (ch, secondStart, seek.output.getNumSamples() - secondStart) < .04f)
            return Result::fail ("seeking while playing did not retrigger the isolated MIDI phrase");
    setRoomCheck (*edit, true);
    auto bypass = stone;
    bypass.bypass = true;
    updateReturn (*edit, bypass);
    auto bypassed = device.capture (*edit);
    if (! bypassed.room || bypassed.room->checkEnabled || energyDb (bypassed.output, 0, &dry.output) > -95.0f)
        return Result::fail ("bypass must clear the diagnostic and preserve the dry signal");

    updateReturn (*edit, stone);
    setRoomCheck (*edit, true);
    // Exercise migration of a saved pre-correction return, which did not mark
    // itself as solo-safe. The runtime check should never become saved state.
    returned->state.removeProperty (te::IDs::soloIsolate, nullptr);
    if (! te::EditFileOperations (*edit).writeToFile (temporary.getFile(), false))
        return Result::fail ("save signal fixture");
    clip = nullptr;
    instrument = nullptr;
    source = nullptr;
    edit.reset();
    edit = te::loadEditFromFile (engine, temporary.getFile());
    if (edit == nullptr) return Result::fail ("reopen signal fixture");
    prepareRoomPlayback (*edit);
    auto restored = device.capture (*edit);
    if (! restored.room || restored.room->checkEnabled || restored.room->bypassed
        || ! roomReturnTrack (*edit)->isSoloIsolate (false)
        || energyDb (restored.output, 0, &expected.output) > -90.0f)
        return Result::fail ("restored preset, wet level, solo safety, or diagnostic persistence");

    return Result::ok();
}

Result verifySourceInsert()
{
    if(valuesOf(decodeSource("1 0 1 0 0 1 1 0 0 0 1 1 1 0"))!=valuesOf(SourceParameters{}))
        return Result::fail("P3 state migration keeps reflections off");
    if (valuesOf(decodeSource("2 1 2")) != valuesOf(SourceParameters{})
        || valuesOf(decodeSource("1 0 1 0 0 1 1 0 0 0 1 1 1 0 trailing")) != valuesOf(SourceParameters{}))
        return Result::fail("invalid source settings must fall back to neutral");
    for(const auto& preset:sourcePresets())
        if(valuesOf(decodeSource(encodeSource(preset.parameters)))!=valuesOf(preset.parameters))
            return Result::fail("source preset serialization");

    te::Engine engine("Studio source insert tests",{},std::make_unique<OfflineEngine>());
    registerPlugins(engine);
    engine.getPluginManager().createBuiltInType<DryPiano>();
    HostedDevice device(engine);
    const juce::TemporaryFile temporary(".tracktionedit");
    auto edit=te::createEmptyEdit(engine,temporary.getFile());
    if(!edit)return Result::fail("source insert edit");
    auto track=edit->insertNewAudioTrack(te::TrackInsertPoint::getEndOfTracks(*edit),nullptr);
    if(!track)return Result::fail("source insert track");
    const auto trackId=track->itemID;
    track->getOutput().setOutputToDefaultDevice(false);
    auto instrument=edit->getPluginCache().createNewPlugin(DryPiano::xmlTypeName,{});
    auto effect=edit->getPluginCache().createNewPlugin(SourcePlugin::xmlTypeName,{});
    auto* spatial=dynamic_cast<SourcePlugin*>(effect.get());
    if(!instrument||!spatial)return Result::fail("source insert registration");
    track->pluginList.insertPlugin(instrument,0,nullptr);
    auto clip=track->insertMIDIClip({{},te::TimePosition::fromSeconds(1.4)},nullptr);
    if(!clip)return Result::fail("source insert MIDI fixture");
    clip->getSequence().addNote(60,te::BeatPosition::fromBeats(.5),te::BeatDuration::fromBeats(.2),96,0,nullptr);
    edit->getMasterVolumePlugin()->setVolumeDb(0);
    track->getVolumePlugin()->setVolumeDb(0);
    auto dry=device.capture(*edit);
    track->pluginList.insertPlugin(effect,track->pluginList.indexOf(track->getVolumePlugin()),nullptr);
    if(track->pluginList.indexOf(spatial)<0)return Result::fail("source insert placement");
    const auto pluginId=spatial->itemID;
    auto neutral=device.capture(*edit);
    if(energyDb(neutral.output,0,&dry.output)>-95||energyDb(neutral.output,1,&dry.output)>-95)
        return Result::fail("neutral insert transparency through hosted device");

    auto p=SourceParameters{};p.direction.angle=-90;p.direction.timing=0;p.direction.shadow=0;
    spatial->beginGesture("Test source positioning");spatial->setParameters(p);spatial->endGesture();
    auto left=device.capture(*edit);
    if(std::abs((energyDb(left.output,0)-energyDb(left.output,1))-4.f)>.3f)
        return Result::fail("native insert audible left ear difference");
    auto& undo=edit->getUndoManager();
    if(!undo.undo()||valuesOf(spatial->parameters())!=valuesOf(SourceParameters{}))
        return Result::fail("undo source settings");
    if(!undo.redo()||valuesOf(spatial->parameters())!=valuesOf(p))
        return Result::fail("redo source settings");
    spatial->beginGesture("Test bypass");spatial->setEnabled(false);spatial->endGesture();
    auto bypassed=device.capture(*edit);
    if(energyDb(bypassed.output,0,&dry.output)>-95||energyDb(bypassed.output,1,&dry.output)>-95)
        return Result::fail("source insert true bypass");
    spatial->beginGesture("Enable source");spatial->setEnabled(true);spatial->endGesture();

    // Actual editor sliders and preset selector address the attached instance.
    {
        SourceWindow window([&]{return spatial;});
        auto* content=window.getContentComponent();
        juce::Slider* distance=nullptr;juce::ComboBox* preset=nullptr;
        juce::Slider* earlyAmount=nullptr;juce::ComboBox* reflectionRoom=nullptr;
        for(int i=0;content&&i<content->getNumChildComponents();++i)
        {
            auto* child=content->getChildComponent(i);
            if(auto* slider=dynamic_cast<juce::Slider*>(child);slider&&slider->getName()=="Distance")distance=slider;
            if(auto* combo=dynamic_cast<juce::ComboBox*>(child);combo&&combo->getName()=="Spatial preset")preset=combo;
            if(auto* slider=dynamic_cast<juce::Slider*>(child);slider&&slider->getName()=="Early reflections")earlyAmount=slider;
            if(auto* combo=dynamic_cast<juce::ComboBox*>(child);combo&&combo->getName()=="Reflection room")reflectionRoom=combo;
        }
        if(!distance||!preset||!earlyAmount||!reflectionRoom)return Result::fail("editor controls missing");
        distance->setValue(50,juce::sendNotificationSync);
        if(std::abs(spatial->parameters().distance-.5f)>.001f)
            return Result::fail("editor distance binding");
        preset->setSelectedId(4,juce::sendNotificationSync);
        if(valuesOf(spatial->parameters())!=valuesOf(sourcePresets()[3].parameters))
            return Result::fail("editor preset binding");
        earlyAmount->setValue(75,juce::sendNotificationSync);
        reflectionRoom->setSelectedId(2,juce::sendNotificationSync);
        if(spatial->parameters().reflections.amount!=.75f||spatial->parameters().reflections.room!=1)
            return Result::fail("reflection editor bindings");
        window.setVisible(false);
    }
    spatial->beginGesture("Test live source parameter");spatial->setParameters(p);spatial->endGesture();
    auto right=p;right.direction.angle=90;
    auto live=device.capture(*edit,[&]{spatial->setParameters(right);});
    if(std::abs((energyDb(live.output,1)-energyDb(live.output,0))-4.f)>.5f)
        return Result::fail("source parameter publication during playback");
    const auto direct=device.capture(*edit);
    right.reflections={1,2};
    spatial->beginGesture("Test early reflections");spatial->setParameters(right);spatial->endGesture();
    const auto reflected=device.capture(*edit);
    if(energyDb(reflected.output,0,&direct.output)<-85||energyDb(reflected.output,1,&direct.output)<-85)
        return Result::fail("native early reflections must change audible output");
    if(hasRoomSend(*track)||roomReturnTrack(*edit)!=nullptr)
        return Result::fail("source insert changed Stage routing");
    if(!te::EditFileOperations(*edit).writeToFile(temporary.getFile(),false))
        return Result::fail("save source insert");
    clip=nullptr;instrument=nullptr;effect=nullptr;track=nullptr;spatial=nullptr;
    edit.reset();edit=te::loadEditFromFile(engine,temporary.getFile());
    if(!edit)return Result::fail("reopen source insert");
    auto* restored=te::findAudioTrackForID(*edit,trackId);
    if(!restored)return Result::fail("reopen source track");
    for(auto* plugin:restored->pluginList.getPlugins())
        if(plugin->itemID==pluginId)spatial=dynamic_cast<SourcePlugin*>(plugin);
    if(!spatial||valuesOf(spatial->parameters())!=valuesOf(right))
        return Result::fail("restored native spatial type and settings");
    auto reopened=device.capture(*edit);
    if(energyDb(reopened.output,0,&reflected.output)>-90||energyDb(reopened.output,1,&reflected.output)>-90)
        return Result::fail("restored source and early reflection processing");
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
Result verifyPhysicalStage()
{
    te::Engine engine ("Studio physical Stage tests", {}, std::make_unique<OfflineEngine>());
    registerPlugins (engine);
    engine.getPluginManager().createBuiltInType<DryPiano>();
    HostedDevice device (engine);
    const juce::TemporaryFile session (".tracktionedit"), rendered (".wav");
    auto edit = te::createEmptyEdit (engine, session.getFile());
    auto source = edit->insertNewAudioTrack (te::TrackInsertPoint::getEndOfTracks (*edit), nullptr);
    if (source == nullptr) return Result::fail ("physical source track");
    source->getOutput().setOutputToDefaultDevice (false);
    auto instrument = edit->getPluginCache().createNewPlugin (DryPiano::xmlTypeName, {});
    source->pluginList.insertPlugin (instrument, 0, nullptr);
    auto clip = source->insertMIDIClip ({ {}, te::TimePosition::fromSeconds (1.4) }, nullptr);
    if (clip == nullptr) return Result::fail ("physical MIDI fixture");
    clip->getSequence().addNote (60, te::BeatPosition::fromBeats (.5), te::BeatDuration::fromBeats (.2), 96, 0, nullptr);
    const auto sourceId = source->itemID;
    edit->getMasterVolumePlugin()->setVolumeDb (0);
    source->getVolumePlugin()->setVolumeDb (0);
    Room room;
    room.bypass = true;
    auto root = juce::ValueTree (sceneType).setProperty ("version", version, nullptr);
    root.addChild (writeRoom (room, "Hosted fixture"), -1, nullptr);
    edit->state.addChild (root, -1, nullptr);
    auto seat = writeSeat (sourceId.toString(), { { -.6f, .57f }, Section::strings, -8 });
    source->state.addChild (seat, -1, nullptr);
    auto original = device.capture (*edit);
    if (const auto result = setRoomSend (*source, true, { -.6f, .57f }, -8, room); result.failed()) return result;
    auto legacy = device.capture (*edit);
    auto& undo = edit->getUndoManager();
    undo.beginNewTransaction ("Adopt physical Stage");
    if (const auto result = adoptPhysicalStage (*edit); result.failed()) return result;
    undo.beginNewTransaction();
    if (! hasStageSource (*source) || ! usesPhysicalStage (*edit) || ! seat.hasProperty ("xMetres"))
        return Result::fail ("physical migration and stable seat identity");
    auto physical = device.capture (*edit);
    if (energyDb (physical.output, 0, &legacy.output) < -55.f ||
        energyDb (physical.output, 0) <= energyDb (physical.output, 1) ||
        energyDb (physical.output, 0) < -35)
        return Result::fail ("physical direct path must be audible, distinct and left-biased");
    if (! undo.undo() || usesPhysicalStage (*edit) || hasStageSource (*source)
        || energyDb (device.capture (*edit).output, 0, &legacy.output) > -90.f)
        return Result::fail ("undo migration restores legacy audio");
    if (! undo.redo() || ! usesPhysicalStage (*edit) || ! hasStageSource (*source))
        return Result::fail ("redo physical migration");

    root.setProperty ("stageBypass", true, nullptr);
    if (energyDb (device.capture (*edit).output, 0, &original.output) > -90.f)
        return Result::fail ("Stage bypass restores the single dry path");
    root.setProperty ("stageBypass", false, nullptr);
    auto mics = root.getOrCreateChildWithName (microphonesType, nullptr);
    Mic left;
    left.pattern = Pattern::omni;
    left.pan = -1;
    Mic right = left;
    right.pan = 1;
    right.delay = 13;
    mics.addChild (writeMic ("left", "Main L", left), -1, nullptr);
    mics.addChild (writeMic ("right", "Main R", right), -1, nullptr);
    root.setProperty ("microphoneMix", true, nullptr);
    auto pair = device.capture (*edit);
    if (energyDb (pair.output, 0) < -40 || energyDb (pair.output, 1) < -40)
        return Result::fail ("hosted microphone pair");
    mics.getChild (1).setProperty ("solo", true, nullptr);
    auto solo = device.capture (*edit);
    if (energyDb (solo.output, 0) > -100 || energyDb (solo.output, 1) < -40)
        return Result::fail ("microphone solo must replace the original dry output");
    mics.getChild (1).setProperty ("mute", true, nullptr);
    auto muted = device.capture (*edit);
    if (energyDb (muted.output, 0) > -100 || energyDb (muted.output, 1) > -100)
        return Result::fail ("microphone mute must silence the direct and local reflection paths");
    mics.getChild (1).setProperty ("solo", false, nullptr).setProperty ("mute", false, nullptr);
    room.bypass = false;
    room.gain = -6;
    root.removeChild (root.getChildWithName (roomType), nullptr);
    root.addChild (writeRoom (room, "Hosted fixture"), -1, nullptr);
    auto expected = device.capture (*edit);
    if (! expected.room || expected.room->wetPeak < .001f || ! expected.room->reachesMaster)
        return Result::fail ("managed shared late-field contribution");
    mics.getChild (0).setProperty ("mute", true, nullptr);
    mics.getChild (1).setProperty ("mute", true, nullptr);
    if (const auto silence = device.capture (*edit); energyDb (silence.output, 0) > -100 || energyDb (silence.output, 1) > -100)
        return Result::fail ("the shared tail must follow microphone mixer mute");
    mics.getChild (0).setProperty ("mute", false, nullptr);
    mics.getChild (1).setProperty ("mute", false, nullptr);
    source->getVolumePlugin()->setVolumeDb (-12.f);
    auto lowered = device.capture (*edit);
    if (std::abs (energyDb (expected.output, 1) - energyDb (lowered.output, 1) - 12.f) > .8f)
        return Result::fail ("physical Stage track fader must control the direct and wet paths");
    source->getVolumePlugin()->setVolumeDb (0.f);

    te::Renderer::Parameters params (*edit);
    juce::WavAudioFormat wave;
    params.audioFormat = &wave;
    params.destFile = rendered.getFile();
    params.sampleRateForAudio = testRate;
    params.blockSizeForAudio = blockSize;
    params.bitDepth = 24;
    params.time = { {}, te::TimePosition::fromSeconds (3.0) };
    params.canRenderInMono = false;
    params.useMasterPlugins = true;
    auto file = te::Renderer::renderToFile ("Studio Stage export test", params);
    std::unique_ptr<juce::AudioFormatReader> reader (wave.createReaderFor (file.createInputStream().release(), true));
    if (reader == nullptr || reader->numChannels != 2 || reader->lengthInSamples < 2 * testRate)
        return Result::fail ("offline Stage WAV capture");
    AudioBuffer<float> exported (2, int (reader->lengthInSamples));
    if (! reader->read (&exported, 0, exported.getNumSamples(), 0, true, true)
        || exported.getMagnitude (1, testRate, testRate) < .00012f)
        return Result::fail ("offline export must include the late tail beyond the dry phrase");
    const auto liveRms = energyDb (expected.output, 1);
    AudioBuffer<float> span (2, expected.output.getNumSamples());
    for (int ch = 0; ch < 2; ++ch) span.copyFrom (ch, 0, exported, ch, 0, span.getNumSamples());
    const auto offlineRms = energyDb (span, 1);
    if (std::abs (liveRms - offlineRms) > 1.2f)
        return Result::fail ("offline and hosted Stage RMS diverged: " + juce::String (liveRms) + " vs " + juce::String (offlineRms));

    auto effect = edit->getPluginCache().createNewPlugin (ReverbPlugin::xmlTypeName, {});
    auto* reverb = dynamic_cast<ReverbPlugin*> (effect.get());
    if (reverb == nullptr) return Result::fail ("native reverb registered");
    reverb->setRoom (room);
    auto fxRoom = room;
    fxRoom.shape.mix = 0;
    fxRoom.shape.quality = Quality::high;
    reverb->setRoom (fxRoom);
    source->pluginList.insertPlugin (effect, 1, nullptr);
    if (! te::EditFileOperations (*edit).writeToFile (session.getFile(), false))
        return Result::fail ("save physical Stage");
    source = nullptr; clip = nullptr; instrument = nullptr; effect = nullptr;
    edit.reset();
    edit = te::loadEditFromFile (engine, session.getFile());
    if (edit == nullptr) return Result::fail ("reopen physical Stage");
    auto* restored = te::findAudioTrackForID (*edit, sourceId);
    if (restored == nullptr || ! hasStageSource (*restored) || ! usesPhysicalStage (*edit))
        return Result::fail ("restore physical source and Stage scene");
    auto* reopened = dynamic_cast<ReverbPlugin*> (restored->pluginList[1]);
    if (reopened == nullptr || reopened->room().shape.quality != Quality::high
        || reopened->room().shape.mix != 0 || energyDb (device.capture (*edit).output, 1, &expected.output) > -75)
        return Result::fail ("restored microphone audio and reverb parameters");
    edit->deleteTrack (restored);
    if (energyDb (device.capture (*edit).output, 1) > -100)
        return Result::fail ("deleted source leaked audio into the room");
    std::printf ("Physical Stage hosted %.2f dBFS, offline %.2f dBFS, tail at 1–2 s %.6f FS.\n",
                 double (liveRms), double (offlineRms), double (exported.getMagnitude (1, testRate, testRate)));
    return Result::ok();
}


}

int runSpatialRoutingTest()
{
    auto result = verify();
    if (result.wasOk()) result = verifySignal();
    if (result.wasOk()) result = verifySourceInsert();
    if (result.wasOk()) result = verifyPhysicalStage();
    if (result.failed()) fprintf (stderr, "Studio spatial routing test failed: %s\n", result.getErrorMessage().toRawUTF8());
    else fprintf (stdout, "Tracktion room routing, audible stereo wet signal, meters, depth, solo, bypass, project reopen, native inserts, physical Stage, microphone mixer and offline export passed.\n");
    return result.wasOk() ? 0 : 1;
}
}
