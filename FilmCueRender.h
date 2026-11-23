// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "FilmCueDelivery.h"
#include <JuceHeader.h>
#include <memory>
#include <vector>

namespace studio::film
{
// Native Tracktion render graph only. This class owns the asynchronous
// serial queue, and must be accessed on the JUCE message thread. No I/O,
// graph preparation or video decoding is done in the audio callback.
class CueRenderSession final
{
public:
    enum class Mode { stereoMix, individualTracks };
    std::function<void()> onUpdate;

    ~CueRenderSession() { cancel(); queue.reset(); }

    bool isActive() const { return queue != nullptr && ! queue->hasFinished(); }
    bool hasStarted() const { return queue != nullptr; }

    void cancel()
    {
        if (queue != nullptr && ! queue->hasFinished())
        {
            queue->cancelAll();
            message = "Cancelling cue rendering...";
            notify();
        }
    }

    void clear()
    {
        cancel();
        queue.reset();  // Tracktion joins/cancels the render before Edit release.
        message.clear();
        renderedDirectory = {};
    }

    float progress() const
    {
        return queue ? queue->getTotalProgress() : 0.0f;
    }

    juce::String description() const
    {
        if (queue == nullptr) return message;
        if (! queue->hasFinished())
            return "Rendering cue " + juce::String (int (progress() * 100.0f)) + "%";
        int completed = 0, failed = 0, cancelled = 0;
        for (const auto& job : queue->getJobs())
        {
            using State = tracktion::engine::RenderQueue::Job::State;
            switch (job->getState())
            {
                case State::completed: ++completed; break;
                case State::failed: ++failed; break;
                case State::cancelled: ++cancelled; break;
                default: break;
            }
        }
        return "Cue delivery: " + juce::String (completed) + " complete, "
             + juce::String (failed) + " failed, "
             + juce::String (cancelled) + " cancelled. " + renderedDirectory.getFileName();
    }

    juce::File outputDirectory() const { return renderedDirectory; }

    juce::Result start (tracktion::engine::Edit& edit,
                        const Project& project,
                        const Cue& cue,
                        const juce::File& chosenDirectory,
                        Mode mode)
    {
        namespace te = tracktion::engine;
        if (isActive())
            return juce::Result::fail ("A film cue render is already running.");
        // Clear any previous finished jobs before referencing a new Edit.
        clear();

        if (edit.getTransport().isRecording() || edit.getTransport().isPlaying())
            return juce::Result::fail ("Stop playback and recording before exporting a cue.");
        const auto range = deliveryRange (project, cue);
        if (! range)
            return juce::Result::fail ("The cue needs a valid start and end within the Studio timeline.");
        if (! chosenDirectory.isDirectory())
            return juce::Result::fail ("Choose an existing output directory.");

        const juce::String stem (deliveryStem (cue));
        const juce::String folderName = stem + (mode == Mode::individualTracks ? "-Tracks" : "-Mix");
        const auto folder = chosenDirectory.getNonexistentChildFile (folderName, {}, true);
        const auto makeFolder = folder.createDirectory();
        if (makeFolder.failed()) return makeFolder;

        te::RenderSpecification base;
        base.format = te::RenderFormat::wav;
        base.sampleRate = 48000.0;
        base.bitDepth = 24;
        base.channelLayout = "stereo";
        base.time = te::TimeRange {
            te::TimePosition::fromSeconds (range->timelineStart),
            te::TimePosition::fromSeconds (range->timelineEnd)
        };
        // Fixed length cue start and end, for sample-aligned delivery.
        // Reverb tails are deliberately not appended to these outputs.
        base.includeTails = false;
        base.trimSilence = false;
        base.normalise = false;
        base.dither = false;
        base.usePlugins = true;
        base.useMasterPlugins = mode == Mode::stereoMix;

        std::vector<te::RenderSpecification> specifications;
        if (mode == Mode::stereoMix)
        {
            base.destination = folder.getChildFile (stem + "-Stereo.wav");
            specifications.push_back (base);
        }
        else
        {
            const auto tracks = te::getAudioTracks (edit);
            if (tracks.isEmpty() || tracks.size() > 128)
                return juce::Result::fail ("Track delivery supports 1 to 128 audio tracks.");
            // Each track is rendered separately. Tracktion maintains required
            // sidechain, rack and aux source dependencies where possible.
            base.includeSourceTracks = true;
            specifications = te::createPerTrackSpecifications (edit, base, folder);
            if (specifications.empty())
                return juce::Result::fail ("No tracks could be selected for cue delivery.");
        }

        // Validate the whole batch before starting so an invalid spec can't
        // leave half of a cue's outputs delivered.
        std::vector<te::PlannedRenderJob> planned;
        planned.reserve (specifications.size());
        for (const auto& spec : specifications)
        {
            if (spec.destination.exists())
                return juce::Result::fail ("Refusing to overwrite " + spec.destination.getFileName());
            auto valid = te::validateRenderSpecification (edit, spec);
            if (valid.failed()) return valid;
            auto job = te::createRenderJob (edit, spec);
            if (! job)
                return juce::Result::fail ("Unable to prepare Tracktion rendering for "
                                           + spec.destination.getFileName());
            planned.push_back (std::move (*job));
        }

        queue = std::make_unique<te::RenderQueue>();
        queue->onJobFinished = [this] (te::RenderQueue::Job&) { notify(); };
        queue->onFinished = [this] { notify(); };
        queue->addJobs (std::move (planned));
        renderedDirectory = folder;
        message = "Cue delivery rendering started.";
        queue->start();
        notify();
        return juce::Result::ok();
    }

private:
    void notify() { if (onUpdate) onUpdate(); }

    std::unique_ptr<tracktion::engine::RenderQueue> queue;
    juce::File renderedDirectory;
    juce::String message;
};
} // namespace studio::film
