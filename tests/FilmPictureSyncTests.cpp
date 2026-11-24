// SPDX-License-Identifier: GPL-3.0-or-later
#include "../FilmPictureSync.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>

using namespace studio::film;
int main()
{
    Project picture;
    picture.rate = Rate::fps24;
    picture.pictureStartFrame = 240; // Picture starts 10 s into Studio.
    PictureSyncController sync;
    PreviewObservation sample;
    sample.tickMillis = 1000;
    sample.timelineSeconds = 12.0;
    sample.videoOpen = false;
    assert (! sync.update (picture, sample).seek);

    sample.videoOpen = true;
    auto first = sync.update (picture, sample);
    assert (first.seek && ! first.play && std::abs (first.targetSeconds - 2.0) < 1e-8);
    sample.videoSeconds = 2.0;
    sample.transportPlaying = true;
    sample.tickMillis = 1100;
    auto start = sync.update (picture, sample);
    assert (start.seek && start.play);
    sample.videoPlaying = true;
    sample.timelineSeconds = 12.04;
    sample.videoSeconds = 2.04;
    sample.tickMillis = 1140;
    assert (! sync.update (picture, sample).seek);
    sample.timelineSeconds = 12.08;
    sample.videoSeconds = 1.70;
    sample.tickMillis = 1180;
    assert (! sync.update (picture, sample).seek); // Throttled drift correction.
    sample.timelineSeconds = 12.5;
    sample.videoSeconds = 2.1;
    sample.tickMillis = 1490;
    assert (sync.update (picture, sample).seek); // Drift after throttle.

    sample.timelineSeconds = 45.0;
    sample.videoSeconds = 2.2;
    sample.tickMillis = 1500;
    const auto seek = sync.update (picture, sample);
    assert (seek.discontinuity && seek.seek && seek.stop && seek.play);
    assert (std::abs (seek.targetSeconds - 35.0) < 1e-8);
    sample.timelineSeconds = 12.0; // Simulate loop wrap.
    sample.videoSeconds = 35.0;
    sample.tickMillis = 1542;
    const auto loop = sync.update (picture, sample);
    assert (loop.discontinuity && loop.seek);

    sample.transportPlaying = false;
    sample.timelineSeconds = 16.0;
    sample.videoSeconds = 7.0;
    sample.tickMillis = 1600;
    assert (sync.update (picture, sample).stop);
    sample.visible = false;
    sample.videoPlaying = true;
    assert (sync.update (picture, sample).stop);
    sample.visible = true;
    sample.videoPlaying = false;
    assert (sync.update (picture, sample).seek); // Reopen must align.

    sample.timelineSeconds = 5.0;
    assert (sync.update (picture, sample).stop == false);
    sample.timelineSeconds = std::numeric_limits<double>::quiet_NaN();
    assert (! sync.update (picture, sample).seek);
    sample.timelineSeconds = 12.0;
    sample.videoDurationSeconds = 1.0;
    assert (! sync.update (picture, sample).seek); // End-of-media.
    sync.reset();
    sample.videoDurationSeconds = 0;
    assert (sync.update (picture, sample).seek);
    // At 24 fps, a six-frame lag is measured in signed source frames.
    sync.reset();
    sample.videoOpen = true;
    sample.visible = true;
    sample.videoDurationSeconds = 0;
    sample.transportPlaying = false;
    sample.videoPlaying = false;
    sample.timelineSeconds = 12.0;
    sample.videoSeconds = 1.75;
    sample.tickMillis = 3000;
    auto behind = sync.update (picture, sample);
    assert (behind.seek);
    assert (std::abs (behind.driftFrames + 6.0) < 1e-7);

    // A manual re-align bypasses the throttle, including while stopped.
    sample.videoSeconds = behind.targetSeconds;
    sample.tickMillis = 3010;
    assert (! sync.update (picture, sample).seek);
    sync.reset();
    sample.tickMillis = 3020;
    assert (sync.update (picture, sample).seek);

    // Duration metadata must be finite before seeking the native player.
    sample.videoDurationSeconds = std::numeric_limits<double>::infinity();
    sample.tickMillis = 3100;
    assert (! sync.update (picture, sample).seek);

    // 29.97 drop-frame preview uses the exact rational frame rate, not 30.
    picture.rate = Rate::fps2997DF;
    picture.pictureStartFrame = 0;
    sample.videoDurationSeconds = 0;
    sample.timelineSeconds = frameSeconds (17982, picture.rate);
    sample.videoSeconds = sample.timelineSeconds;
    sync.reset();
    const auto df = sync.update (picture, sample);
    assert (df.seek && std::abs (df.driftFrames) < 1.0);

    std::cout << "Picture sync state, loop, drift, stop and seek checks passed\n";
}
