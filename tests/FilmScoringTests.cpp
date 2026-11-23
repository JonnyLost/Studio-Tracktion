// SPDX-License-Identifier: GPL-3.0-or-later
#include "../FilmScoringCore.h"
#include "../FilmScoringState.h"
#include <cassert>
#include <cmath>
#include <iostream>

using namespace studio::film;
static void near (double a, double b) { assert (std::abs (a - b) < 1e-7); }

int main()
{
    for (auto rate : { Rate::fps23976, Rate::fps24, Rate::fps25, Rate::fps2997NDF, Rate::fps2997DF, Rate::fps30 })
    {
        for (int64_t frame : { -100000LL, -1LL, 0LL, 1LL, 1799LL, 1800LL, 17982LL, 107892LL, 200000LL })
            assert (toFrames (fromFrames (frame, rate), rate) == frame);
        assert (secondsToFrames (frameSeconds (17982, rate), rate) == 17982);
        assert (! toFrames ({ false, 24, 0, 0, 0 }, rate));
    }
    assert (! toFrames ({ false, 0, 1, 0, 0 }, Rate::fps2997DF));
    assert (! toFrames ({ false, 0, 1, 0, 1 }, Rate::fps2997DF));
    assert (toFrames ({ false, 0, 1, 0, 2 }, Rate::fps2997DF) == 1800);
    assert (toFrames ({ false, 0, 10, 0, 0 }, Rate::fps2997DF) == 17982);
    assert (toFrames ({ false, 1, 0, 0, 0 }, Rate::fps2997DF) == 107892);
    assert (fromFrames (1800, Rate::fps2997DF).frames == 2);
    assert (fromFrames (1800, Rate::fps2997DF).minutes == 1);

    Project project;
    project.rate = Rate::fps23976;
    project.pictureStartFrame = 240;
    project.picturePath = "reference.mov";
    assert (project.addSpot ({ "hit1", "1M1", "Door slams", "hard sync", 300 }));
    assert (! project.addSpot ({ "hit1", "1M1", "duplicate", "", 301 }));
    const auto absolute = project.timelineSeconds (300);
    assert (project.pictureFrameAt (absolute) == 300);
    const auto originalHit = project.find ("hit1")->pictureFrame;
    assert (project.addSpot ({ "another", "1M2", "Jump", "", 350 }));
    assert (project.spots.size() == 2);
    project.spots.pop_back();
    near (project.timelineSeconds (originalHit), absolute);
    assert (project.find ("hit1")->pictureFrame == 300);

    TempoMap map ({ { 0, 120, 4, 4 }, { 10, 60, 3, 4 }, { 16, 90, 6, 8 } });
    assert (map.valid());
    near (*map.beatAt (10), 20);
    near (*map.beatAt (16), 26);
    near (*map.secondAt (26), 16);
    near (*map.secondAt (*map.beatAt (17.25)), 17.25);
    assert (map.barBeatAt (10)->bar == 6);
    assert (map.barBeatAt (13)->bar == 7);
    assert (map.barBeatAt (16)->bar == 8);
    // A tempo change halfway through a bar must not advance to a new bar.
    TempoMap tempoOnly ({ { 0, 120, 4, 4 }, { 3, 60, 4, 4 } });
    assert (tempoOnly.barBeatAt (3)->bar == 2 && tempoOnly.barBeatAt (3)->beat == 3);
    assert (tempoOnly.barBeatAt (5)->bar == 3 && tempoOnly.barBeatAt (5)->beat == 1);
    TempoMap meterChange ({ { 0, 120, 4, 4 }, { 2.5, 100, 3, 4 } });
    assert (meterChange.barBeatAt (2.5)->bar == 3);
    assert (meterChange.barBeatAt (2.5)->beat == 1);
    for (auto rate : { Rate::fps23976, Rate::fps2997DF })
    {
        const auto extreme = fromFrames (std::numeric_limits<int64_t>::min(), rate);
        assert (extreme.hours >= 0 && extreme.hours < 24);
    }
    assert (! TempoMap ({ { 0, 0 } }).valid());
    near (*suggestedBpm (8, 16), 120);
    assert (! suggestedBpm (0, 16));

    assert (project.addSpot ({ "hit2", "1M1", "Cut", "", 540 }));
    auto cue = calculateCue (project, map, "hit1", "hit2", 16);
    assert (cue && cue->durationSeconds > 10 && cue->suggestedTempo > 90);
    assert (! calculateCue (project, map, "hit2", "hit1", 16));

    assert (project.addCue ({ "1M1", "Opening", "Spot notes", 0, 300 }));
    assert (project.addCue ({ "1M2", "Next scene", "", 540, std::nullopt }));
    assert (! project.addCue ({ "1M1", "Duplicate", "", 100, 200 }));

    juce::ValueTree root ("EDIT");
    juce::UndoManager undo;
    store (root, project, &undo);
    auto restored = decode (root);
    assert (restored.rate == project.rate && restored.pictureStartFrame == 240);
    assert (restored.picturePath == "reference.mov");
    assert (restored.spots.size() == 2 && restored.find ("hit1")->notes == "hard sync");
    assert (restored.cues.size() == 2);
    assert (restored.findCue ("1M1")->endPictureFrame == 300);
    assert (! restored.findCue ("1M2")->endPictureFrame);
    assert (undo.undo());
    assert (decode (root).spots.empty());
    assert (undo.redo());
    assert (decode (root).spots.size() == 2);
    // Updating an existing state is one undoable user action, not two.
    auto revised = project;
    revised.spots[0].name = "New title";
    store (root, revised, &undo);
    assert (decode (root).find ("hit1")->name == "New title");
    assert (undo.undo());
    assert (decode (root).find ("hit1")->name == "Door slams");
    assert (undo.redo());
    assert (decode (root).find ("hit1")->name == "New title");
    auto xml = root.createXml();
    assert (xml != nullptr);
    auto reloaded = juce::ValueTree::fromXml (*xml);
    assert (decode (reloaded).find ("hit1")->pictureFrame == 300);
    assert (decode (reloaded).findCue ("1M1")->startPictureFrame == 0);
    assert (decode (reloaded).findCue ("1M2") != nullptr);
    std::cout << "Film scoring timing, spotting, and persistence passed\n";
}
