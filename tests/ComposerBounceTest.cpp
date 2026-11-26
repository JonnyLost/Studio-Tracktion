// SPDX-License-Identifier: GPL-3.0-or-later
#include "../ComposerBounce.h"
#include "../FilmCueRender.h"
#include "../SpatialRouting.h"
#include "../StudioSynth.h"
#include "../StudioSampler.h"

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

Result verifyFilmDelivery (te::Edit& edit, te::AudioTrack& track, const File& folder)
{
    using Mode = film::CueRenderSession::Mode;
    constexpr double duration = 0.5;
    film::Project picture;
    picture.rate = film::Rate::fps24;
    picture.pictureStartFrame = -24;
    const film::Cue cue { "1M1", "Test cue", "", 27, 39 };
    const std::vector<film::StemAssignment> assignments {
        { track.itemID.toString().toStdString(), film::StemFamily::strings }
    };
    film::CueRenderSession session;
    for (const auto mode : { Mode::stereoMix, Mode::individualTracks, Mode::familyStems })
    {
        if (auto result = session.start (edit, picture, cue, folder, mode, assignments); result.failed())
            return result;
        const auto deadline = Time::getMillisecondCounterHiRes() + 10000;
        while (session.isActive() && Time::getMillisecondCounterHiRes() < deadline)
            MessageManager::getInstance()->runDispatchLoopUntil (10);
        if (session.isActive())
            return Result::fail ("film cue render timed out");

        session.releaseFinishedRender();
        if (session.hasStarted()
            || (mode != Mode::individualTracks && ! session.description().contains ("1 complete")))
            return Result::fail ("release the offline graph after cue delivery");
        const auto delivery = session.outputDirectory();
        const String stem (film::deliveryStem (cue));
        const auto filename = mode == Mode::stereoMix ? stem + "-Stereo.wav"
                            : mode == Mode::individualTracks ? track.getName() + ".wav"
                            : stem + "-Strings.wav";
        AudioBuffer<float> samples;
        if (auto result = readWave (delivery.getChildFile (filename), 2, 48000, 24, duration, samples); result.failed())
            return result;
        const auto outputs = delivery.findChildFiles (File::findFiles, false, "*.wav");
        if (mode == Mode::individualTracks ? outputs.size() < 2 : outputs.size() != 1)
            return Result::fail ("film delivery rendered an incorrect set of tracks");
        if (! delivery.getChildFile ("Delivery-Report.csv").loadFileAsString().contains (",completed,"))
            return Result::fail ("film delivery receipt missing completed output");
    }
    return Result::ok();
}

Result verifyStreamedBounce (te::Engine& engine, const File& folder)
{
    const auto media = folder.getChildFile ("Streamed bounce source.wav");
    constexpr int chunk = 8192;
    {
        WavAudioFormat format;
        auto stream = media.createOutputStream();
        auto writer = format.createWriterFor (stream, AudioFormatWriterOptions().withSampleRate (48000)
            .withNumChannels (2).withBitsPerSample (24));
        AudioBuffer<float> signal (2, chunk);
        for (int i = 0; i < chunk; ++i)
        {
            const auto v = 0.2f * std::sin (i * (2.0f * juce::MathConstants<float>::pi / 256.0f));
            signal.setSample (0, i, v); signal.setSample (1, i, v);
        }
        if (! writer) return Result::fail ("create streamed WAV bounce fixture");
        for (int i = 0; i < 48; ++i)
            if (! writer->writeFromAudioSampleBuffer (signal, 0, chunk)) return Result::fail ("write streamed WAV bounce fixture");
    }
    auto edit = te::createEmptyEdit (engine, folder.getChildFile ("Streamed Bounce.tracktionedit"));
    if (! edit) return Result::fail ("create streamed sampler bounce edit");
    auto* track = te::getAudioTracks (*edit).getFirst();
    auto plugin = edit->getPluginCache().createNewPlugin (sampler::Plugin::xmlTypeName, {});
    if (! plugin) return Result::fail ("create streamed sampler plugin");
    track->pluginList.insertPlugin (plugin, 0, nullptr);
    auto* samplerPlugin = dynamic_cast<sampler::Plugin*> (plugin.get());
    if (samplerPlugin == nullptr || samplerPlugin->loadSample (media).failed())
        return Result::fail ("load streamed WAV for bounce");
    auto zone = samplerPlugin->instrument().zones[0].mapping;
    if (! zone.sample || ! zone.sample->stream) return Result::fail ("fixture must stream");
    zone.start = 4 * sampler::streamPageFrames + 100;
    zone.root = 60;
    samplerPlugin->editZone (0, zone);
    auto clip = track->insertMIDIClip ("Long note", { 0_tp, 2_tp }, nullptr);
    if (! clip) return Result::fail ("create long sampler note");
    clip->getSequence().addNote (60, 0_bp, 3.5_bd, 110, 0, nullptr);
    edit->clickTrackEnabled = false;
    edit->dispatchPendingUpdatesSynchronously();

    bounce::Options options;
    options.range = { 0_tp, 2_tp };
    options.tail = 0;
    options.destination = folder.getChildFile ("Streamed bounce.wav");
    if (auto result = run (*edit, options); result.failed()) return result;
    AudioBuffer<float> rendered;
    if (auto result = readWave (options.destination, 2, 48000, 24, 2.0, rendered); result.failed()) return result;
    // Several page boundaries beyond the attack must contain real audio.
    // Check every 4096-frame window while the MIDI note is still held.
    for (int offset = 4096; offset < 64000; offset += 4096)
        if (rendered.getRMSLevel (0, offset, 4096) < 0.001f)
            return Result::fail ("streamed bounce contains an unexpected silent page");
    if (! media.deleteFile()) return Result::fail ("remove streamed bounce fixture");
    options.destination = folder.getChildFile ("Streamed missing.wav");
    if (run (*edit, options).wasOk() || options.destination.exists())
        return Result::fail ("prevent export with missing streamed sample");
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
    const auto media = folder.getChildFile ("Fixture Source.wav");
    {
        WavAudioFormat format;
        std::unique_ptr<OutputStream> stream = media.createOutputStream();
        auto writer = format.createWriterFor (stream, AudioFormatWriterOptions().withSampleRate (48000)
                                              .withNumChannels (1).withBitsPerSample (16));
        AudioBuffer<float> signal (1, 4800);
        for (int i = 0; i < signal.getNumSamples(); ++i)
            signal.setSample (0, i, 0.1f * std::sin (i * 0.11f));
        if (! writer || ! writer->writeFromAudioSampleBuffer (signal, 0, signal.getNumSamples()))
            return Result::fail ("write synthetic audio fixture");
    }
    // Audio clips and relative media are resolved by the same offline graph.
    if (! te::EditFileOperations (*edit).writeToFile (project, false)) return Result::fail ("save new project before importing");
    auto wave = track->insertWaveClip ("Synthetic source", media, { { 0.50_tp, 0.60_tp }, {} }, false);
    if (wave == nullptr || ! te::EditFileOperations (*edit).writeToFile (project, false))
        return Result::fail ("save project with synthetic audio");
    if (wave->getOriginalFile() != media || ! media.existsAsFile())
        return Result::fail ("source fixture path " + wave->state[te::IDs::source].toString()
            + " resolves to " + wave->getOriginalFile().getFullPathName() + "; expected " + media.getFullPathName());
    const auto savedProject = project.loadFileAsString();
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
    if (folder.findChildFiles (File::findFiles, false, "*.wav").size() != 5)
        return Result::fail ("stem selection generated an extra track");
    float error = 0;
    for (int channel = 0; channel < 2; ++channel)
        for (int frame = 0; frame < stereo.getNumSamples(); ++frame)
            error = jmax (error, std::abs (stereo.getSample (channel, frame)
                           - dry.getSample (channel, frame) - wet.getSample (channel, frame)));
    if (error > 0.002f)
        return Result::fail ("dry and wet stems do not reconstruct the unity-master mix (error " + String (error) + ")");

    if (auto result = verifyFilmDelivery (*edit, *track, folder); result.failed()) return result;
    if (auto result = verifyStreamedBounce (engine, folder); result.failed()) return result;

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
        || spatial::roomReturnTrack (*edit) != room || project.loadFileAsString() != savedProject)
        return Result::fail ("offline rendering changed the live project or saved edit");

    options.destination = folder.getChildFile ("Missing.wav");
    if (! media.deleteFile()) return Result::fail ("remove synthetic audio fixture");
    const auto missing = run (*edit, options);
    if (missing.wasOk() || ! missing.getErrorMessage().contains ("Audio source unavailable") || options.destination.exists())
        return Result::fail ("report missing media before render");
    wave->removeFromParent();
    PluginDescription unavailable;
    unavailable.name = "Studio test unavailable AU";
    unavailable.pluginFormatName = "AudioUnit";
    unavailable.fileOrIdentifier = "AudioUnit:MusicDevice/test,zzzz,abcd";
    unavailable.isInstrument = true;
    auto hosted = edit->getPluginCache().createNewPlugin (te::ExternalPlugin::xmlTypeName, unavailable);
    if (hosted == nullptr) return Result::fail ("create unavailable AU fixture");
    track->pluginList.insertPlugin (hosted, 0, nullptr);
    const auto external = run (*edit, options);
    if (external.wasOk() || ! external.getErrorMessage().contains ("hosted plug-in offline rendering")
        || options.destination.exists())
        return Result::fail ("report unsupported AU before rendering");
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
    sampler::registerPlugin (engine);
    spatial::registerPlugins (engine);
    const auto result = verify (engine, folder);
    if (result.failed())
    {
        std::fprintf (stderr, "Composer bounce: %s\n", result.getErrorMessage().toRawUTF8());
        return 1;
    }
    std::puts ("Composer bounce and Film Scoring stereo, track and family WAV delivery passed.");
    return 0;
}
}
