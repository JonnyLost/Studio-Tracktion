// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "FilmScoringCore.h"
#include <cstdint>
#include <cmath>

namespace studio::film
{
struct PreviewObservation
{
    double timelineSeconds = 0;
    double videoSeconds = 0;
    double videoDurationSeconds = 0; // Zero means unknown.
    uint64_t tickMillis = 0;
    bool transportPlaying = false;
    bool videoPlaying = false;
    bool visible = true;
    bool videoOpen = false;
};

struct PreviewCommand
{
    bool stop = false;
    bool seek = false;
    bool play = false;
    bool discontinuity = false;
    double targetSeconds = 0;
};

// Decision-only model, with no JUCE, AVFoundation, clock reads or allocations.
// The GUI host executes the decisions on the message thread. Transport seeks,
// loop wraps and playback transitions bypass the ordinary seek throttle.
class PictureSyncController
{
public:
    void reset()
    {
        hasPrevious = false;
        forceAlignment = true;
        lastSeekMillis = 0;
    }

    PreviewCommand update (const Project& project, const PreviewObservation& observation)
    {
        PreviewCommand result;
        if (! observation.videoOpen || ! observation.visible
            || ! std::isfinite (observation.timelineSeconds)
            || ! std::isfinite (observation.videoSeconds))
        {
            result.stop = observation.videoPlaying;
            reset();
            return result;
        }

        const auto frame = project.pictureFrameAt (observation.timelineSeconds);
        if (! frame) { result.stop = observation.videoPlaying; reset(); return result; }
        result.targetSeconds = frameSeconds (*frame, project.rate);
        if (result.targetSeconds < 0
            || (observation.videoDurationSeconds > 0
                && result.targetSeconds >= observation.videoDurationSeconds))
        {
            result.stop = observation.videoPlaying;
            reset();
            return result;
        }

        // A jump in the transport clock (including the loop's wrap backwards)
        // is not ordinary AVPlayer drift. Request immediate frame alignment.
        if (hasPrevious)
        {
            const double dt = observation.tickMillis >= previousMillis
                            ? double (observation.tickMillis - previousMillis) / 1000.0 : 0.0;
            const double expected = previousTimeline + (previousPlaying ? dt : 0.0);
            const double jump = std::abs (observation.timelineSeconds - expected);
            result.discontinuity = jump > (previousPlaying ? 0.25 : 0.075);
        }
        const bool playbackStarted = observation.transportPlaying && (! hasPrevious || ! previousPlaying);
        const bool playbackStopped = ! observation.transportPlaying && hasPrevious && previousPlaying;
        const bool mustAlign = forceAlignment || result.discontinuity || playbackStarted || playbackStopped;

        const double tolerance = observation.transportPlaying ? 0.16 : 0.04;
        const bool drift = std::abs (observation.videoSeconds - result.targetSeconds) > tolerance;
        const bool throttleElapsed = ! hasPrevious || observation.tickMillis < lastSeekMillis
                                  || observation.tickMillis - lastSeekMillis >= 350;
        result.seek = mustAlign || (drift && throttleElapsed);
        // A discontinuity must not keep showing the stale moving image while
        // the native player is being repositioned.
        result.stop = observation.videoPlaying
                   && (! observation.transportPlaying || result.discontinuity);
        result.play = observation.transportPlaying
                   && (! observation.videoPlaying || result.stop);
        if (result.seek) lastSeekMillis = observation.tickMillis;
        previousMillis = observation.tickMillis;
        previousTimeline = observation.timelineSeconds;
        previousPlaying = observation.transportPlaying;
        hasPrevious = true;
        forceAlignment = false;
        return result;
    }

private:
    bool hasPrevious = false, previousPlaying = false, forceAlignment = true;
    double previousTimeline = 0;
    uint64_t previousMillis = 0, lastSeekMillis = 0;
};
} // namespace studio::film
