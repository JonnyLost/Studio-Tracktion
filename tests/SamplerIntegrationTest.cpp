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

bool testSample (const File& file, AudioFormat& format, double frequency, int channels, int metadataRoot = -1)
{
    std::unique_ptr<OutputStream> stream = file.createOutputStream();
    if (! stream) return false;
    auto options = AudioFormatWriterOptions().withSampleRate (rate / 2).withNumChannels (channels).withBitsPerSample (24);
    if (metadataRoot >= 0) options = options.withMetadata ("MidiUnityNote", String (metadataRoot));
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

bool longSample (const File& file, AudioFormat& format, int frames, int channels)
{
    std::unique_ptr<OutputStream> stream = file.createOutputStream();
    if (! stream) return false;
    auto writer = format.createWriterFor (stream, AudioFormatWriterOptions().withSampleRate (48000)
        .withNumChannels (channels).withBitsPerSample (24));
    if (! writer) return false;
    constexpr int chunk = 8192;
    AudioBuffer<float> data (channels, chunk);
    for (int ch = 0; ch < channels; ++ch)
        for (int i = 0; i < chunk; ++i)
            data.setSample (ch, i, static_cast<float> (0.3 * std::sin (2 * std::numbers::pi * (i % 256) / 256 + ch * 0.6)));
    for (int offset = 0; offset < frames; offset += chunk)
        if (! writer->writeFromAudioSampleBuffer (data, 0, jmin (chunk, frames - offset))) return false;
    return true;
}

Result verifyStreamingImport (const File& directory, Plugin& sampler, WavAudioFormat& wav, AiffAudioFormat& aiff)
{
    // Stereo recording exceeds the former 64 MiB decoded sample limit.
    constexpr int bigFrames = 9 * 1024 * 1024 + 123;
    constexpr int aiffFrames = 16 * streamPageFrames + 77;
    const auto big = directory.getChildFile ("Stream C4.wav");
    const auto other = directory.getChildFile ("Stream G4.aiff");
    const auto preset = directory.getChildFile ("Stream test.studiosampler");
    if (! longSample (big, wav, bigFrames, 2) || ! longSample (other, aiff, aiffFrames, 1))
        return Result::fail ("create long WAV/AIFF stream fixtures");
    Instrument proposal; String summary;
    if (Plugin::prepareImport ({}, { big, other }, true, proposal, summary).failed()
        || proposal.zones.size() != 2)
        return Result::fail ("import long WAV and AIFF files");
    for (const auto& entry : proposal.zones)
    {
        const auto& sample = entry.mapping.sample;
        const bool isWav = entry.file == big;
        const auto expected = isWav ? bigFrames : aiffFrames;
        if (! sample || ! sample->stream || sample->frameCount() != expected
            || sample->left.size() != streamPreloadFrames || entry.mapping.loopEnd != expected
            || sample->bytes() != (isWav ? 1048576u : 524288u))
            return Result::fail ("bounded preload, cache, and full recording length");
        StreamCache::Cursor cursor;
        float l = 0, r = 0;
        const int deep = expected - 2;
        if (! sample->readFrame (deep, cursor, l, r, true)) return Result::fail ("read actual source near EOF");
        const auto wantL = static_cast<float> (0.3 * std::sin (2 * std::numbers::pi * (deep % 256) / 256));
        const auto wantR = isWav ? static_cast<float> (0.3 * std::sin (2 * std::numbers::pi * (deep % 256) / 256 + 0.6)) : wantL;
        if (std::abs (l - wantL) > 1e-5f || std::abs (r - wantR) > 1e-5f)
            return Result::fail ("correct samples from deep disk page");
    }
    // Move a zone's start and loop well beyond the resident attack, then
    // persist it and reconstruct with an empty cache.
    auto& zone = proposal.zones[0].mapping;
    zone.start = 5 * streamPageFrames + 27;
    zone.loop = true;
    zone.loopStart = 6 * streamPageFrames;
    zone.loopEnd = 10 * streamPageFrames + 11;
    sampler.commitImport (proposal);
    if (sampler.savePreset (preset).failed()) return Result::fail ("save streamed preset");
    Instrument reopened;
    if (Plugin::preparePreset (preset, {}, reopened).failed() || reopened.zones.size() != 2
        || reopened.zones[0].mapping.start != zone.start || reopened.zones[0].mapping.loopEnd != zone.loopEnd
        || ! reopened.zones[0].mapping.sample || ! reopened.zones[0].mapping.sample->stream)
        return Result::fail ("restore long-source preset and loop");
    const auto stats = sampler.resourceSnapshot();
    if (stats.memoryBytes.value_or (0) != 1572864u || stats.cacheBytes.value_or (0) != 1179648u
        || stats.streamingBytes.value_or (0) <= 64u * 1024u * 1024u)
        return Result::fail ("report resident/cache memory separately from decoded stream size");
    if (sampler.verifyStreamSources (true).failed()) return Result::fail ("verify healthy stream sources");
    if (! big.deleteFile() || sampler.verifyStreamSources (false).wasOk())
        return Result::fail ("detect vanished streamed source");
    sampler.commitImport ({}, false);
    return Result::ok();
}

Result verifySfzImport (const File& directory, Plugin& sampler, WavAudioFormat& wav)
{
    const auto source = directory.getChildFile ("SFZ test samples");
    if (source.createDirectory().failed()) return Result::fail ("create SFZ fixture directory");
    const auto sample = source.getChildFile ("Long note C4.wav");
    if (! longSample (sample, wav, 16 * streamPageFrames + 77, 2))
        return Result::fail ("write SFZ stream fixture");
    const auto definition = directory.getChildFile ("Test instrument.sfz");
    const auto include = directory.getChildFile ("Regions.inc");
    if (! include.replaceWithText (R"sfz(
<region> sample=Long note C4.wav seq_position=2 volume=-6
<region> sample="Long note C4.wav" seq_position=1 volume=-3
<group> key=D4 trigger=release
<region> sample=Long note C4.wav loop_mode=one_shot
<group> key=E4 locc1=64 hicc1=127
<region> sample=Missing.wav
<group> key=F4 lorand=0 hirand=0.5
<region> sample=Long note C4.wav
)sfz") || ! definition.replaceWithText (R"sfz(
#define $samples SFZ test samples
<control> default_path=$samples
<global> ampeg_attack=0.02 ampeg_release=0.5 amp_veltrack=0
<group> key=C4 sw_last=C1 sw_default=C1 sw_label=Sustain seq_length=2
loop_mode=loop_sustain offset=65536 loop_start=70000 loop_end=100000 pan=-25
xfin_lovel=20 xfin_hivel=80 xf_velcurve=gain
#include "Regions.inc"
)sfz")) return Result::fail ("write SFZ and included definitions");

    const auto revision = sampler.changeCount();
    Instrument imported; String report;
    if (auto result = Plugin::prepareSfz (definition, sampler.instrument(), imported, report); result.failed()) return result;
    if (sampler.changeCount() != revision || imported.zones.size() != 4
        || ! report.contains ("3 playable regions") || ! report.contains ("1 unavailable")
        || ! report.contains ("1 omitted") || ! report.contains ("lorand"))
        return Result::fail ("transactional SFZ compatibility proposal");
    const auto& z = imported.zones[0].mapping;
    if (! z.sample || ! z.sample->stream || z.sample != imported.zones[1].mapping.sample
        || z.root != 60 || z.low != 60 || z.high != 60 || z.gainDb != -3
        || z.group == 0 || z.group != imported.zones[1].mapping.group
        || z.start != 65536 || z.loopEnd != 100001 || ! z.sfz.sustainLoop
        || z.sfz.velocityTrack != 0 || z.sfz.pan != -25 || z.sfz.powerFade
        || std::abs (z.sfz.attack - 0.02f) > 0.00001f || z.sfz.release != 0.5f)
        return Result::fail ("preserve SFZ sequence order, loop endpoints, envelope, pan and streaming");
    if (imported.groups[0].playback.switchDefault != 24 || ! imported.groups[1].playback.releaseTrigger
        || imported.groups[2].playback.controller != 1 || imported.groups[2].playback.ccLow != 64
        || imported.zones[3].mapping.sample || imported.zones[3].problem.isEmpty())
        return Result::fail ("keyswitch, release, controller and unavailable sample mapping");

    sampler.commitImport (imported);
    const auto preset = directory.getChildFile ("SFZ converted.studiosampler");
    if (sampler.savePreset (preset).failed() || sampler.loadPreset (preset).failed()
        || sampler.instrument().zones.size() != 4
        || sampler.instrument().zones[0].mapping.sfz.release != 0.5f
        || sampler.instrument().groups[0].playback.switchDefault != 24
        || ! sampler.instrument().zones[0].mapping.sample->stream)
        return Result::fail ("persist native SFZ region parameters and streaming");
    std::atomic<bool> cancel { true };
    Instrument cancelled; String cancelledReport;
    if (Plugin::prepareSfz (definition, sampler.instrument(), cancelled, cancelledReport, &cancel).wasOk())
        return Result::fail ("cancel SFZ proposal");
    const auto bad = directory.getChildFile ("Bad SFZ.sfz");
    if (! bad.replaceWithText ("<region> sample=bad.wav lokey=invalid")
        || Plugin::prepareSfz (bad, sampler.instrument(), cancelled, cancelledReport).wasOk())
        return Result::fail ("reject invalid SFZ mapping");
    sampler.commitImport ({}, false);
    return Result::ok();
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
        window.setAlwaysOnTop (true);
        window.setVisible (true);
        window.toFront (true);
        MessageManager::getInstance()->runDispatchLoopUntil (60);
        if (! editor.runReview (wavFile, aiffFile, screenshots, [&] { return playback.peak (8) > 0.01f; }))
            return Result::fail ("editor interactions or screenshots");
    }

    const auto metadataFile = directory.getChildFile ("Metadata.wav");
    if (! testSample (metadataFile, wav, 523.251, 1, 72)) return Result::fail ("write root-note metadata fixture");
    Instrument metadataProposal; String metadataSummary;
    if (Plugin::prepareImport ({}, { metadataFile }, true, metadataProposal, metadataSummary).failed()
        || metadataProposal.zones[0].mapping.root != 72) return Result::fail ("embedded WAV root metadata mapping");
    Instrument cancelledProposal; String cancelledSummary; std::atomic<bool> cancelled { true }; std::atomic<int> progress { 0 };
    if (Plugin::prepareImport ({}, { directory }, true, cancelledProposal, cancelledSummary, &cancelled, &progress).wasOk())
        return Result::fail ("cancel folder import before enumeration");
    const auto layeredFile = directory.getChildFile ("Layer_C4_v64-127.wav");
    if (! wavFile.copyFileTo (layeredFile)) return Result::fail ("create velocity filename fixture");
    Instrument layeredProposal; String layeredSummary;
    if (Plugin::prepareImport ({}, { layeredFile }, true, layeredProposal, layeredSummary).failed()
        || layeredProposal.zones[0].mapping.soft != 64 || layeredProposal.zones[0].mapping.hard != 127)
        return Result::fail ("velocity filename ranges in import proposal");
    auto cached = layeredProposal.zones[0].mapping.sample;
    if (Plugin::prepareImport (layeredProposal, { layeredFile }, true, cancelledProposal, cancelledSummary).failed()
        || cancelledProposal.zones[1].mapping.sample != cached) return Result::fail ("decoded sample cache reuse");
    // Bulk import proposals must not mutate live audio or undo history until accepted.
    Instrument proposal; String summary;
    const auto revision = other->changeCount();
    if (other->proposeImport ({ wavFile, aiffFile, directory.getChildFile ("unsupported.txt") }, true, proposal, summary).failed()
        || other->changeCount() != revision || proposal.zones.size() != 2
        || proposal.zones[0].mapping.root != 60 || proposal.zones[0].mapping.high != 63
        || proposal.zones[1].mapping.low != 64 || ! summary.contains ("1 unsupported"))
        return Result::fail ("deterministic bulk import preview and cancellation");
    if (auto streamed = verifyStreamingImport (directory, *other, wav, aiff); streamed.failed())
        return streamed;
    if (auto imported = verifySfzImport (directory, *other, wav); imported.failed()) return imported;
    other->commitImport (proposal);
    other->editGroups ({ { 0, "Piano Soft" }, { 1, "Piano Loud" } });
    auto groups = other->instrument().groups;
    groups[0].playback.trigger = GroupTrigger::keyswitch; groups[0].playback.key = 24;
    groups[1].playback.trigger = GroupTrigger::controller; groups[1].playback.controller = 20; groups[1].playback.ccLow = 64;
    groups[0].playback.mute = true;
    other->editGroups (groups);
    if (other->articulations().size() != 2) return Result::fail ("muting preserves verified articulation assignments");
    groups[0].playback.mute = false; other->editGroups (groups);
    other->moveZone (1, 1);
    other->undoMapping (false);
    if (other->instrument().zones[1].folder != 0) return Result::fail ("undo group assignment");
    other->undoMapping (true);
    if (other->instrument().zones[1].folder != 1) return Result::fail ("redo group assignment");
    const auto groupPreset = directory.getChildFile ("Groups.studiosampler");
    if (other->savePreset (groupPreset).failed() || other->loadPreset (groupPreset).failed()
        || other->instrument().groups.size() != 2 || other->instrument().groups[1].name != "Piano Loud"
        || other->instrument().zones[1].folder != 1 || other->articulations().size() != 2
        || other->instrument().groups[1].playback.ccLow != 64) return Result::fail ("named group and articulation persistence");
    if (other->savePreset (wavFile).wasOk()) return Result::fail ("protect source recording");
    auto legacy = XmlDocument::parse (groupPreset);
    legacy->setAttribute ("version", 1);
    legacy->deleteAllChildElementsWithTagName ("GROUP");
    for (auto* child : legacy->getChildIterator()) { child->removeAttribute ("start"); child->removeAttribute ("end"); child->removeAttribute ("folder"); }
    const auto legacyFile = directory.getChildFile ("Legacy.studiosampler");
    if (! legacy->writeTo (legacyFile) || other->loadPreset (legacyFile).failed()
        || other->instrument().groups.size() != 1 || other->instrument().zones[0].mapping.start != 0)
        return Result::fail ("version one preset migration");
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
