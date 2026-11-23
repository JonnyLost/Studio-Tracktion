// SPDX-License-Identifier: GPL-3.0-or-later
#include "../FilmScoringCueSheet.h"
#include <cassert>
#include <iostream>

using namespace studio::film;
int main()
{
    Project picture;
    picture.rate = Rate::fps2997DF;
    picture.pictureStartFrame = 300;
    assert (picture.addCue ({ "1M2", "Ending", "End note", 17982, 21582 }));
    assert (picture.addCue ({ "1M1", "Opening", "Draft", 0, std::nullopt }));
    assert (! picture.addCue ({ "1M1", "Duplicate", "", 200, 300 }));
    assert (! picture.addCue ({ "1M3", "Bad duration", "", 100, 99 }));
    assert (! picture.addCue ({ "1M4", "Before picture", "", -1, 200 }));
    assert (picture.cues.size() == 2);
    auto changed = *picture.findCue ("1M1");
    changed.id = "1M2";
    assert (! picture.updateCue ("1M1", changed));
    changed.id = "1M1";
    changed.title = "=SUM(1,1)";
    changed.notes = "Line one\nAnother \"quote\"";
    changed.endPictureFrame = 1800;
    assert (picture.updateCue ("1M1", changed));
    assert (picture.findCue ("1M1")->endPictureFrame == 1800);
    assert (picture.findCue ("1M1")->startPictureFrame == 0);

    const auto csv = cueSheetCSV (picture);
    assert (csv.find ("Start SMPTE,End SMPTE") != std::string::npos);
    assert (csv.find ("00:00:00;00") != std::string::npos);
    assert (csv.find ("00:01:00;02") != std::string::npos);
    assert (csv.find ("00:10:00;00") != std::string::npos);
    assert (csv.find ("\"'=SUM(1,1)\"") != std::string::npos);
    assert (csv.find ("Another \"\"quote\"\"") != std::string::npos);
    assert (csv.find ("1M1") < csv.find ("1M2")); // Sorted by picture start.
    assert (picture.findCue ("1M2")->startPictureFrame == 17982);

    // A tempo change must never rewrite source-picture cue boundaries.
    TempoMap alternate ({ { 0, 120, 4, 4 }, { 30, 80, 4, 4 } });
    assert (alternate.valid());
    assert (picture.findCue ("1M2")->startPictureFrame == 17982);
    assert (picture.removeCue ("1M2"));
    assert (! picture.removeCue ("1M2"));
    assert (picture.findCue ("1M1") != nullptr);
    const auto noEnd = Project {};
    assert (cueSheetCSV (noEnd).find ("Cue ID,Title") == 0);
    std::cout << "Frame-anchored film cue sheet checks passed\n";
}
