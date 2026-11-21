// SPDX-License-Identifier: GPL-3.0-or-later
#include "ComposerBounce.h"
#include "ComposerFiles.h"
#include "MusicalTransport.h"
#include "SpatialRouting.h"
#include "StudioSampler.h"
#include "StudioSynth.h"

namespace studio::bounce
{
namespace te = tracktion;
using namespace juce;

namespace
{
Result checkSources (te::Edit& edit)
{
    for (auto* plugin : te::getAllPlugins (edit, true))
    {
        if (auto* hosted = dynamic_cast<te::ExternalPlugin*> (plugin))
            return Result::fail (hosted->getName() + ": hosted plug-in offline rendering is unverified in Studio.");
        if (const auto* sampler = dynamic_cast<studio::sampler::Plugin*> (plugin))
        {
            if (sampler->stateError().isNotEmpty())
                return Result::fail (sampler->stateError());
            for (const auto& zone : sampler->instrument().zones)
                if (zone.problem.isNotEmpty() || ! zone.file.existsAsFile())
                    return Result::fail ("Sampler source unavailable: " + zone.file.getFileName());
        }
        if (const auto* synth = dynamic_cast<studio::synth::Plugin*> (plugin))
            if (synth->warning().isNotEmpty())
                return Result::fail (synth->warning());
    }
    for (auto* track : te::getAudioTracks (edit))
        for (auto* clip : track->getClips())
            if (auto* wave = dynamic_cast<te::WaveAudioClip*> (clip); wave != nullptr && ! wave->getOriginalFile().existsAsFile())
                return Result::fail ("Audio source unavailable: " + clip->getName());
    return Result::ok();
}

bool tooLarge (const Options& options)
{
    const auto seconds = options.range.getLength().inSeconds() + options.tail;
    return seconds * options.sampleRate * (options.bitDepth / 8) * (options.mono ? 1 : 2) >= 4294960000.0;
}
}

Result Job::create (te::Edit& source, const Options& options, std::unique_ptr<Job>& job)
{
    if (! transportSettled (source) || source.getTransport().isPlaying())
        return Result::fail ("Stop playback and let the current take finish before exporting.");
    if (options.range.isEmpty() || options.range.getStart() < te::TimePosition()
        || options.range.getEnd() > te::Edit::getMaximumEditEnd()
        || ! std::isfinite (options.tail) || options.tail < 0 || options.tail > 30
        || (options.sampleRate != 44100 && options.sampleRate != 48000)
        || (options.bitDepth != 16 && options.bitDepth != 24) || tooLarge (options))
        return Result::fail ("Choose a valid range, 0–30 seconds of tail, and a WAV size under 4 GB.");
    if (options.destination == File()
        || ! (options.stems ? options.destination : options.destination.getParentDirectory()).isDirectory())
        return Result::fail ("Choose an existing destination folder.");
    if (auto result = checkSources (source); result.failed())
        return result;

    auto next = std::make_unique<Job>();
    source.flushState();
    next->snapshot = te::Edit::createEdit ({ .engine = source.engine,
        .editState = source.state.createCopy(),
        .editProjectItemID = source.getProjectItemRef(),
        .role = te::Edit::forRendering,
        .numUndoLevelsToStore = 0,
        .editFileRetriever = source.editFileRetriever,
        .filePathResolver = source.filePathResolver,
        .numAudioTracks = 0 });
    if (next->snapshot == nullptr)
        return Result::fail ("Could not create the offline render graph.");
    auto& edit = *next->snapshot;
    spatial::prepareRoomPlayback (edit);
    edit.clickTrackEnabled = false;

    // End the source at the requested boundary on the render copy. Keep the
    // graph clock running for an exact, common decay window across all outputs.
    for (auto* track : te::getAllTracks (edit))
        if (auto* clips = dynamic_cast<te::ClipTrack*> (track))
        {
            const auto items = clips->getClips();
            for (auto* clip : items)
                if (clip->getPosition().getStart() >= options.range.getEnd())
                    clip->removeFromParent();
                else if (clip->getPosition().getEnd() > options.range.getEnd())
                    clip->setEnd (options.range.getEnd(), true);
        }
    edit.dispatchPendingUpdatesSynchronously();
    edit.updateMuteSoloStatuses();

    te::RenderSpecification spec;
    spec.time = options.range.withEnd (options.range.getEnd() + te::TimeDuration::fromSeconds (options.tail));
    spec.includeTails = false;
    spec.sampleRate = options.sampleRate;
    spec.bitDepth = options.bitDepth;
    spec.channelLayout = options.mono ? "mono" : "stereo";
    spec.useMasterPlugins = ! options.stems;

    auto add = [&] (const File& destination) -> Result
    {
        if (destination.exists())
            return Result::fail ("File already exists: " + destination.getFileName());
        auto staged = std::make_unique<TemporaryFile> (destination);
        spec.destination = staged->getFile();
        if (auto valid = te::validateRenderSpecification (edit, spec); valid.failed())
            return valid;
        auto render = te::createRenderJob (edit, spec);
        if (! render) return Result::fail ("Could not set up " + destination.getFileName() + ".");
        next->queue.addJob (std::move (*render));
        next->outputs.push_back ({ destination, std::move (staged) });
        return Result::ok();
    };

    if (! options.stems)
    {
        if (auto result = add (options.destination); result.failed()) return result;
    }
    else
    {
        if (options.tracks.isEmpty()) return Result::fail ("Choose at least one stem track.");
        Array<te::EditItemID> roomSources;
        int index = 0;
        for (auto id : options.tracks)
        {
            auto* track = dynamic_cast<te::AudioTrack*> (te::findTrackForID (edit, id));
            if (track == nullptr || spatial::isRoomReturn (*track) || track->getParentTrack() != nullptr
                || ! track->getOutput().usesDefaultAudioOut())
                return Result::fail ("Stems require selected top-level tracks on the default audio output.");
            if (track->shouldBePlayed() && spatial::hasRoomSend (*track))
                roomSources.add (id);
            spec.tracks = { id };
            spec.includeSourceTracks = true;
            const auto name = String (++index).paddedLeft ('0', 2) + " - " + File::createLegalFileName (track->getName());
            if (auto result = add (options.destination.getChildFile (name + ".wav")); result.failed()) return result;
        }
        if (auto* room = spatial::roomReturnTrack (edit); options.room && room != nullptr && ! roomSources.isEmpty())
        {
            spec.tracks = { room->itemID };
            spec.mutedTracks = roomSources;
            spec.includeSourceTracks = false;
            if (auto result = add (options.destination.getChildFile ("Room Return.wav")); result.failed()) return result;
        }
    }

    job = std::move (next);
    return Result::ok();
}

void Job::start()
{
    queue.onFinished = [this] { finished = true; };
    queue.onJobFinished = [this] (auto& job)
    {
        if (job.getState() == te::RenderQueue::Job::State::failed)
            queue.cancelAll();
    };
    queue.start();
}

void Job::cancel()
{
    cancelled = true;
    queue.cancelAll();
}

float Job::progress() const { return queue.getTotalProgress(); }

Result Job::publish()
{
    if (! finished) return Result::fail ("The render is still running.");
    if (cancelled) return Result::fail ("Export cancelled.");
    for (const auto& job : queue.getJobs())
        if (job->getState() == te::RenderQueue::Job::State::failed)
            return Result::fail ("Render failed: " + job->getError());
    for (const auto& output : outputs)
        if (! output.staged->getFile().existsAsFile())
            return Result::fail ("No render was produced for " + output.destination.getFileName() + ".");

    for (size_t i = 0; i < outputs.size(); ++i)
    {
        const auto& output = outputs[i];
        if (auto result = publishNewFile (output.staged->getFile(), output.destination); result.failed())
        {
            // Remove this batch's earlier links if a destination appeared while rendering.
            for (size_t j = 0; j < i; ++j)
            {
                const auto& published = outputs[j];
                std::error_code error;
                if (std::filesystem::equivalent (std::filesystem::u8path (published.destination.getFullPathName().toRawUTF8()),
                    std::filesystem::u8path (published.staged->getFile().getFullPathName().toRawUTF8()), error))
                    published.destination.deleteFile();
            }
            return result;
        }
    }
    return Result::ok();
}
}
