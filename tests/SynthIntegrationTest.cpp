// SPDX-License-Identifier: GPL-3.0-or-later
#include "../StudioSynth.h"
#include "../StudioSampler.h"
#include "../SpatialRouting.h"
#include "../SpatialState.h"
#include "../StudioMixing.h"
#include <cmath>
#include <cstdio>

namespace studio::synth
{
namespace
{
using namespace juce;
namespace te = tracktion;
using namespace tracktion::literals;
constexpr int renderRate = 48000, blockSize = 256;

struct Props final : te::PropertyStorage
{
    explicit Props (File f) : PropertyStorage ("Studio Synth validation"), directory (std::move (f)) {}
    File getAppPrefsFolder() override { return directory; }
    File directory;
};
struct Behaviour final : te::EngineBehaviour
{
    bool autoInitialiseDeviceManager() override { return false; }
};

float level (const AudioBuffer<float>& b)
{
    float peak = 0;
    for (int c = 0; c < b.getNumChannels(); ++c)
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const auto sample = b.getSample (c, i);
            if (! std::isfinite (sample)) return std::numeric_limits<float>::quiet_NaN();
            peak = jmax (peak, std::abs (sample));
        }
    return peak;
}

Result renderFactory()
{
    const auto& data = catalog();
    std::array<int, 7> counts {};
    StringArray ids;
    bool layersTwo = false, layersThree = false, layersFour = false;
    for (const auto& sound : data.sounds)
    {
        if (sound.id.isEmpty() || ids.contains (sound.id)) return Result::fail ("Duplicate factory ID: " + sound.id);
        ids.add (sound.id);
        ++counts[static_cast<int> (sound.family)];
        const auto patchDocument = JSON::parse (JSON::toString (sound.patch, true));
        Patch patch;
        String warning;
        if (makePlan (patchDocument, {}, patch, warning).failed() || warning.isNotEmpty())
            return Result::fail ("Invalid factory patch: " + sound.id);
        const bool layered = sound.family == Family::hybrid;
        if ((layered && (patch.count < 2 || patch.count > 4))
            || (! layered && (patch.count != 1 || patch.layers[0].family != sound.family)))
            return Result::fail ("Incorrect engines in factory patch: " + sound.id);

        if (layered)
        {
            layersTwo |= patch.count == 2;
            layersThree |= patch.count == 3;
            layersFour |= patch.count == 4;
        }
        Engine engine (patch, renderRate);
        if (! engine.valid() || engine.layersPlaying() != static_cast<unsigned> (patch.count))
            return Result::fail ("Legacy DSP rejected " + sound.id);
        AudioBuffer<float> block (2, blockSize);
        block.clear();
        engine.render (block.getWritePointer (0), block.getWritePointer (1), blockSize);
        if (level (block) != 0) return Result::fail ("Idle DSP is sounding: " + sound.id);
        engine.midi (0x92, 60, 110);
        engine.midi (0x92, 67, 85);
        float peak = 0;
        for (int b = 0; b < 48; ++b)
        {
            if (b == 5) engine.midi (0xb2, 1, 52);
            if (b == 10) engine.midi (0xe2, 0, 70);
            block.clear();
            engine.render (block.getWritePointer (0), block.getWritePointer (1), blockSize);
            const auto signal = level (block);
            if (! std::isfinite (signal)) return Result::fail ("Invalid audio from " + sound.id);
            peak = jmax (peak, signal);
        }
        if (peak < .00001f) return Result::fail ("Factory sound is silent: " + sound.id);
        engine.clear();
        block.clear();
        engine.render (block.getWritePointer (0), block.getWritePointer (1), blockSize);
        if (level (block) != 0) return Result::fail ("Reset left audible voices: " + sound.id);
    }
    if (counts != std::array { 34, 42, 30, 26, 22, 25, 22 } || ! (layersTwo && layersThree && layersFour))
        return Result::fail ("Factory distribution or Hybrid layer coverage differs.");
    return Result::ok();
}

Result expressivePlayback()
{
    const auto& sounds = catalog().sounds;
    const auto analog = std::find_if (sounds.begin(), sounds.end(), [] (const auto& s) { return s.family == Family::analog; });
    if (analog == sounds.end()) return Result::fail ("No Analog factory sound for MIDI checks.");
    Patch patch;
    String warning;
    if (auto result = makePlan (analog->patch, {}, patch, warning); result.failed()) return result;
    patch.receive = 3;
    Engine one (patch, renderRate), two (patch, renderRate);
    if (! one.valid() || ! two.valid()) return Result::fail ("Prepare Analog MIDI test patch.");
    AudioBuffer<float> a (2, blockSize), b (2, blockSize);
    const auto render = [](Engine& e, AudioBuffer<float>& buffer)
    {
        buffer.clear();
        e.render (buffer.getWritePointer (0), buffer.getWritePointer (1), buffer.getNumSamples());
        return level (buffer);
    };
    one.midi (0x91, 60, 127);
    if (render (one, a) != 0) return Result::fail ("Synth received the wrong MIDI channel.");
    one.clear();
    one.midi (0x92, 60, 127);
    two.midi (0xb2, 11, 63);
    two.midi (0x92, 60, 127);
    for (int i = 0; i < 60; ++i)
    {
        const auto full = render (one, a);
        const auto reduced = render (two, b);
        if (! std::isfinite (full) || ! std::isfinite (reduced)
            || std::abs (reduced - full * (63.0f / 127)) > .0002f)
            return Result::fail ("CC11 did not scale the receiving synth channel.");
    }
    one.midi (0xb2, 64, 127);
    one.midi (0x82, 60, 0);
    if (render (one, a) <= .0001f) return Result::fail ("Sustain did not retain the note.");
    one.midi (0xb2, 120, 0);
    if (render (one, a) != 0) return Result::fail ("All Sound Off did not clear held voices.");
    patch.layers[0].enabled = false;
    Engine inactive (patch, renderRate);
    if (! inactive.valid() || inactive.layersPlaying() != 0)
        return Result::fail ("Disabled Synth layer allocated an active engine.");
    return Result::ok();
}

Result verifyRouting (const File& directory, const File& screenshots)
{
    te::Engine engine (std::make_unique<Props> (directory), {}, std::make_unique<Behaviour>());
    registerPlugin (engine);
    sampler::registerPlugin (engine);
    spatial::registerPlugins (engine);
    auto& devices = engine.getDeviceManager();
    devices.setCpuLimitBeforeMuting (1000);
    auto& audio = devices.getHostedAudioDeviceInterface();
    audio.initialise ({ .sampleRate = renderRate, .blockSize = blockSize, .inputChannels = 0, .outputChannels = 2 });
    audio.prepareToPlay (renderRate, blockSize);
    devices.dispatchPendingUpdates();
    const ScopeGuard cleanup { [&] { devices.deviceManager.closeAudioDevice(); devices.removeHostedAudioDeviceInterface(); } };
    const auto project = directory.getChildFile ("Synth Test.tracktionedit");
    auto edit = te::createEmptyEdit (engine, project);
    if (! edit) return Result::fail ("Create Tracktion edit.");
    edit->playInStopEnabled = true;
    auto track = edit->insertNewAudioTrack (te::TrackInsertPoint::getEndOfTracks (*edit), nullptr);
    if (! track) return Result::fail ("Insert Synth track.");
    track->setName ("M6 Synth");
    track->getOutput().setOutputToDefaultDevice (false);
    auto plugin = edit->getPluginCache().createNewPlugin (Plugin::xmlTypeName, {});
    auto* synth = dynamic_cast<Plugin*> (plugin.get());
    if (! synth) return Result::fail ("Register original synth as a native instrument.");
    track->pluginList.insertPlugin (plugin, 0, nullptr);
    if (findInstrument (*track) != synth) return Result::fail ("Mixer did not recognize the Synth.");

    const auto& sounds = catalog().sounds;
    const auto find = [&] (Family family) -> const FactorySound&
    {
        return *std::find_if (sounds.begin(), sounds.end(), [family] (const auto& sound) { return sound.family == family; });
    };
    auto patch = find (Family::hybrid).patch.clone();
    patch.getDynamicObject()->setProperty ("receive", 3);
    patch.getDynamicObject()->setProperty ("output", .65);
    if (auto result = synth->selectFactory (find (Family::hybrid)); result.failed()) return result;
    if (auto result = synth->editPatch (patch, true); result.failed()) return result;
    const auto preset = directory.getChildFile ("Legacy Compatible.studiosynth");
    if (auto saved = synth->savePreset (preset); saved.failed()) return saved;
    if (synth->selectFactory (find (Family::fm)).failed() || synth->loadPreset (preset).failed()
        || synth->getPatch() != patch) return Result::fail ("Legacy-compatible preset round trip.");

    if (screenshots != File())
    {
        if (screenshots.createDirectory().failed()) return Result::fail ("Create screenshot directory.");
        for (int i = 0; i < 7; ++i)
        {
            const auto family = static_cast<Family> (i);
            if (synth->selectFactory (find (family)).failed()) return Result::fail ("Open family in editor.");
            auto view = synth->createEditor();
            FileOutputStream output (screenshots.getChildFile ("synth-" + familyKey (family) + ".png"));
            if (! output.openedOk() || ! output.setPosition (0) || output.truncate().failed()
                || ! PNGImageFormat().writeImageToStream (view->createComponentSnapshot (view->getLocalBounds()), output))
                return Result::fail ("Capture Synth editor.");
        }
        if (synth->loadPreset (preset).failed()) return Result::fail ("Restore Synth test preset.");
    }

    auto clip = track->insertMIDIClip ("Synth Notes", { 0_tp, 2_tp }, nullptr);
    if (! clip) return Result::fail ("Insert Synth MIDI clip.");
    clip->setMidiChannel (te::MidiChannel (3));
    clip->getSequence().addNote (60, 0_bp, 2_bd, 100, 0, nullptr);
    clip->getSequence().addControllerEvent (0_bp, 11, 110 << 7, nullptr);
    if (auto result = spatial::setRoomSend (*track, true, { 0, 0 }, spatial::Seat().sendTrim, spatial::Room()); result.failed())
        return result;

    auto& transport = edit->getTransport();
    edit->dispatchPendingUpdatesSynchronously();
    transport.ensureContextAllocated (true);
    AudioBuffer<float> buffer (2, blockSize);
    MidiBuffer midi;
    const auto process = [&] (int blocks)
    {
        float maximum = 0;
        for (int i = 0; i < blocks; ++i)
        {
            buffer.clear();
            midi.clear();
            audio.processBlock (buffer, midi);
            const auto value = level (buffer);
            if (! std::isfinite (value)) return value;
            maximum = jmax (maximum, value);
        }
        return maximum;
    };
    synth->preview (60, 100);
    if (process (32) < .002f) return Result::fail ("Synth is silent during stopped keyboard audition.");
    synth->preview (60, 0);
    transport.setPosition (0_tp);
    transport.play (false);
    if (const auto peak = process (48); ! std::isfinite (peak) || peak < .002f)
        return Result::fail ("Synth MIDI did not reach the mixer.");
    if (const auto room = spatial::roomSignal (*edit); ! room || ! room->reachesMaster || room->wetPeak == 0)
        return Result::fail ("Synth did not feed the shared Room Return.");
    transport.stop (false, false);
    transport.freePlaybackContext();

    if (auto trim = synth->getAutomatableParameterByID ("studioSynthTrim"))
        trim->getCurve().addPoint (te::EditPosition (0_tp), -6, 0, nullptr);
    else
        return Result::fail ("Missing stable Synth automation target.");
    if (! te::EditFileOperations (*edit).writeToFile (project, false)) return Result::fail ("Save Synth project.");
    plugin = nullptr;
    clip = nullptr;
    track = nullptr;
    edit.reset();
    edit = te::loadEditFromFile (engine, project);
    if (! edit) return Result::fail ("Reopen Synth project.");
    synth = nullptr;
    for (auto* t : te::getAudioTracks (*edit))
        if (t->getName() == "M6 Synth") synth = dynamic_cast<Plugin*> (findInstrument (*t));
    if (! synth || synth->getPatch() != patch) return Result::fail ("Synth project patch differed after reopen.");
    auto trim = synth->getAutomatableParameterByID ("studioSynthTrim");
    if (! trim || ! trim->hasAutomationPoints()) return Result::fail ("Synth automation did not restore.");

    // Export goes through Tracktion's offline graph, including the room and master chain.
    const auto bounced = directory.getChildFile ("Synth Bounce.wav");
    if (! te::Renderer::renderToFile (*edit, bounced, false)) return Result::fail ("Bounce Synth to WAV.");
    WavAudioFormat wav;
    auto stream = bounced.createInputStream();
    std::unique_ptr<AudioFormatReader> reader (wav.createReaderFor (stream.release(), true));
    if (! reader || reader->lengthInSamples < 4000) return Result::fail ("Read the offline Synth bounce.");
    AudioBuffer<float> output (2, 4000);
    if (! reader->read (&output, 0, output.getNumSamples(), 0, true, true) || level (output) < .0005f)
        return Result::fail ("Offline Synth bounce is silent.");

    const auto& granular = find (Family::granular);
    auto external = granular.patch.clone();
    external["layers"][0]["granularPatch"].getDynamicObject()->setProperty ("importedAudioPath",
        directory.getChildFile ("Missing Granular.wav").getFullPathName());
    if (synth->selectFactory (granular).failed() || synth->editPatch (external, true).failed()
        || ! synth->warning().contains ("Missing Granular.wav")) return Result::fail ("Missing Granular audio was not identified.");
    if (synth->getPatch() != external) return Result::fail ("Missing audio changed the saved patch.");
    return Result::ok();
}
}

int runSynthIntegrationTest (const juce::File& screenshots)
{
    const auto directory = juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("studio-synth-validation", {}, false);
    if (directory.createDirectory().failed()) return 1;
    const juce::ScopeGuard cleanup { [directory] { directory.deleteRecursively(); } };
    auto result = renderFactory();
    if (result.wasOk()) result = expressivePlayback();
    if (result.wasOk()) result = verifyRouting (directory, screenshots);
    if (result.failed())
    {
        std::fprintf (stderr, "%s\n", result.getErrorMessage().toRawUTF8());
        return 1;
    }
    std::puts ("201 original Synth patches, hosted routing, preset/project restoration and offline bounce passed.");
    return 0;
}
}
