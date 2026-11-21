// SPDX-License-Identifier: GPL-3.0-or-later
#include "../ComposerBounce.h"
#include "../SpatialRouting.h"
#include "../StudioSynth.h"

namespace studio
{
namespace
{
namespace te = tracktion;
using namespace te::literals;
using namespace juce;

Result run (te::Edit& edit, const bounce::Options& options, bool cancel = false)
{
    std::unique_ptr<bounce::Job> job;
    if (auto result = bounce::Job::create (edit, options, job); result.failed()) return result;
    job->start();
    if (cancel) job->cancel();
    const auto deadline = Time::getMillisecondCounterHiRes() + 25000;
    while (! job->isFinished() && Time::getMillisecondCounterHiRes() < deadline)
        MessageManager::getInstance()->runDispatchLoopUntil (10);
    if (! job->isFinished()) return Result::fail ("offline queue timed out");
    return job->publish();
}

Result readWave (const File& file, int channels, int rate, int depth, double seconds, AudioBuffer<float>& samples)
{
    WavAudioFormat format;
    auto stream = file.createInputStream();
    if (! stream) return Result::fail ("missing " + file.getFileName());
    std::unique_ptr<AudioFormatReader> read (format.createReaderFor (stream.release(), true));
    const auto frames = static_cast<int> (std::round (rate * seconds));
    if (! read || (int) read->numChannels != channels || read->sampleRate != rate || (int) read->bitsPerSample != depth
        || std::abs (read->lengthInSamples - frames) > 1)
        return Result::fail ("WAV format or length for " + file.getFileName()
                             + (read ? " (" + String (read->lengthInSamples) + " frames)" : ""));
    samples.setSize (channels, static_cast<int> (read->lengthInSamples));
    if (! read->read (&samples, 0, samples.getNumSamples(), 0, true, channels > 1)
        || samples.getMagnitude (0, samples.getNumSamples()) < 0.0001f)
        return Result::fail ("WAV rendered silently: " + file.getFileName());
    return Result::ok();
}

Result verify (te::Engine& engine, const File& folder)
{
    auto& devices = engine.getDeviceManager();
    auto& audio = devices.getHostedAudioDeviceInterface();
    audio.initialise ({ .sampleRate = 48000, .blockSize = 256, .inputChannels = 0, .outputChannels = 2 });
    audio.prepareToPlay (48000, 256);
    devices.dispatchPendingUpdates();
    const ScopeGuard cleanup { [&] { devices.deviceManager.closeAudioDevice(); devices.removeHostedAudioDeviceInterface(); } };
    const auto project = folder.getChildFile ("Bounce.tracktionedit");
    auto edit = te::createEmptyEdit (engine, project);
    if (edit == nullptr) return Result::fail ("create project");
    auto* track = te::getAudioTracks (*edit).getFirst();
    track->setName ("Theme");
    edit->getMasterVolumePlugin()->setVolumeDb (0);
    auto plugin = edit->getPluginCache().createNewPlugin (synth::Plugin::xmlTypeName, {});
    track->pluginList.insertPlugin (plugin, 0, nullptr);
    auto clip = track->insertMIDIClip ("Notes", { 0_tp, 0.75_tp }, nullptr);
    clip->getSequence().addNote (60, 0_bp, 0.4_bd, 90, 0, nullptr);
    if (auto result = spatial::setRoomSend (*track, true, { 0.3f, 0.8f }, -8, {}); result.failed()) return result;
    const auto room = spatial::roomReturnTrack (*edit);
    if (room == nullptr) return Result::fail ("create shared room");
    // A muted native track should never be made audible to feed the room.
    auto muted = edit->insertNewAudioTrack (te::TrackInsertPoint::getEndOfTracks (*edit), nullptr);
    muted->setMute (true);
    const auto originalSelection = clip->getMidiChannel();
    const auto originalMute = track->isMuted (false);
    const auto originalSolo = track->isSolo (false);

    bounce::Options options;
    options.range = { 0_tp, 0.75_tp };
    options.tail = 0.25;
    options.destination = folder.getChildFile ("Stereo.wav");
    if (auto result = run (*edit, options); result.failed()) return result;
    AudioBuffer<float> stereo, mono, dry, wet;
    if (auto result = readWave (options.destination, 2, 48000, 24, 1, stereo); result.failed()) return result;
    if (run (*edit, options).wasOk()) return Result::fail ("protect existing WAV");

    options.destination = folder.getChildFile ("Mono.wav");
    options.mono = true;
    options.sampleRate = 44100;
    options.bitDepth = 16;
    if (auto result = run (*edit, options); result.failed()) return result;
    if (auto result = readWave (options.destination, 1, 44100, 16, 1, mono); result.failed()) return result;

    options.stems = true;
    options.mono = false;
    options.sampleRate = 48000;
    options.bitDepth = 24;
    options.tracks = { track->itemID };
    options.destination = folder;
    if (auto result = run (*edit, options); result.failed()) return result;
    if (auto result = readWave (folder.getChildFile ("01 - Theme.wav"), 2, 48000, 24, 1, dry); result.failed()) return result;
    if (auto result = readWave (folder.getChildFile ("Room Return.wav"), 2, 48000, 24, 1, wet); result.failed()) return result;
    if (folder.findChildFiles (File::findFiles, false, "*.wav").size() != 4)
        return Result::fail ("stem selection generated an extra track");
    float error = 0;
    for (int channel = 0; channel < 2; ++channel)
        for (int frame = 0; frame < stereo.getNumSamples(); ++frame)
            error = jmax (error, std::abs (stereo.getSample (channel, frame)
                           - dry.getSample (channel, frame) - wet.getSample (channel, frame)));
    if (error > 0.002f)
        return Result::fail ("dry and wet stems do not reconstruct the unity-master mix (error " + String (error) + ")");

    options.stems = false;
    options.tail = 5;
    options.destination = folder.getChildFile ("Cancel.wav");
    if (auto result = run (*edit, options, true); result.wasOk() || result.getErrorMessage() != "Export cancelled."
        || options.destination.exists())
        return Result::fail ("cancel and clean partial WAV");
    options.destination = folder.getChildFile ("missing").getChildFile ("Bad.wav");
    if (run (*edit, options).wasOk()) return Result::fail ("reject bad directory");
    if (track->isMuted (false) != originalMute || track->isSolo (false) != originalSolo
        || ! muted->isMuted (false) || clip->getMidiChannel() != originalSelection
        || spatial::roomReturnTrack (*edit) != room)
        return Result::fail ("offline rendering changed the live project");
    return Result::ok();
}
}

int runComposerBounceTest (const juce::File& artifacts)
{
    juce::TemporaryFile scratch (".folder");
    const auto folder = artifacts == juce::File() ? scratch.getFile() : artifacts;
    if (folder.createDirectory().failed()) return 1;
    const juce::ScopeGuard cleanup { [&] { if (artifacts == juce::File()) folder.deleteRecursively(); } };
    tracktion::Engine engine ("Studio Composer Bounce Test");
    synth::registerPlugin (engine);
    spatial::registerPlugins (engine);
    const auto result = verify (engine, folder);
    if (result.failed())
    {
        std::fprintf (stderr, "Composer bounce: %s\n", result.getErrorMessage().toRawUTF8());
        return 1;
    }
    std::puts ("Composer stereo, mono, dry stems, single room return, and cancellation passed.");
    return 0;
}
}
