// SPDX-License-Identifier: GPL-3.0-or-later
#include "../FilmCueDelivery.h"
#include <cassert>
#include <cmath>
#include <iostream>

using namespace studio::film;

int main()
{
    Project project;
    project.rate = Rate::fps24;
    project.pictureStartFrame = 240; // 10 second picture leader.
    Cue cue { "1M1", "Opening / Scene", "", 360, 600 };
    auto range = deliveryRange (project, cue);
    assert (range);
    assert (range->startFrame == 360 && range->endFrame == 600);
    assert (std::abs (range->timelineStart - 25.0) < 1e-8);
    assert (std::abs (range->timelineEnd - 35.0) < 1e-8);
    assert (deliveryStem (cue) == "1M1-Opening-Scene");

    // Cue positions are source-frame anchored and independent of tempo.
    TempoMap slow ({ { 0, 120, 4, 4 }, { 20, 54, 3, 4 } });
    assert (slow.valid());
    assert (deliveryRange (project, cue)->timelineStart == 25.0);

    cue.endPictureFrame.reset();
    assert (! deliveryRange (project, cue));
    cue.endPictureFrame = 360;
    assert (! deliveryRange (project, cue));
    cue.endPictureFrame = 359;
    assert (! deliveryRange (project, cue));
    cue.endPictureFrame = 600;
    cue.startPictureFrame = -1;
    assert (! deliveryRange (project, cue));
    cue.startPictureFrame = 360;

    project.pictureStartFrame = -2000;
    assert (! deliveryRange (project, cue)); // Would require pre-roll before Studio zero.
    project.pictureStartFrame = 240;

    // Avoid path traversal, platform-reserved names and empty components.
    assert (safeDeliveryName ("../../secret:mix\\take?.wav") == "secret-mix-take-wav");
    assert (safeDeliveryName ("CON") == "Cue-CON");
    assert (safeDeliveryName ("com1") == "Cue-com1");
    assert (safeDeliveryName ("") == "Untitled");
    assert (safeDeliveryName ("   ") == "Untitled");
    assert (safeDeliveryName ("Violin I") == "Violin-I");
    assert (safeDeliveryName (std::string (250, 'A')).size() <= 72);
    assert (safeDeliveryName ("a\r\nb") == "a-b");

    project.rate = Rate::fps2997DF;
    project.pictureStartFrame = 0;
    cue.startPictureFrame = 17982;
    cue.endPictureFrame = 35964;
    range = deliveryRange (project, cue);
    assert (range);
    const double expected = frameSeconds (17982, Rate::fps2997DF);
    assert (std::abs (range->timelineStart - expected) < 1e-7);
    assert (std::abs (range->timelineEnd - expected * 2) < 1e-7);
    std::cout << "Film cue delivery ranges and safe filenames passed\n";
}
