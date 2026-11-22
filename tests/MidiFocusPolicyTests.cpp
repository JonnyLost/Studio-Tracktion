// SPDX-License-Identifier: GPL-3.0-or-later
#include "MidiFocusPolicy.h"
#include <cstdio>

using studio::MidiFocusConditions;
using studio::MidiFocusDecision;

static int failures = 0;

static void check (const char* name, MidiFocusConditions state, MidiFocusDecision wanted)
{
    if (studio::midiFocusDecision (state) == wanted) return;
    std::fprintf (stderr, "MIDI focus policy failed: %s\n", name);
    ++failures;
}

int main()
{
    check ("play selected instrument with no arm",
        { true, false, false, false, false, false }, MidiFocusDecision::switchToTrack);
    check ("no playable track releases focus",
        { false, false, false, false, false, false }, MidiFocusDecision::release);
    check ("never reroute during transport recording",
        { true, true, false, false, false, false }, MidiFocusDecision::deferRecording);
    check ("never reroute when input actively recording",
        { true, false, true, false, false, false }, MidiFocusDecision::deferRecording);
    check ("do not steal another armed track",
        { true, false, false, true, false, false }, MidiFocusDecision::preserveRouting);
    check ("preserve intentional multiple MIDI targets",
        { true, false, false, false, true, false }, MidiFocusDecision::preserveRouting);
    check ("let held notes and sustain pedal release on their old track",
        { true, false, false, false, false, true }, MidiFocusDecision::waitForRelease);
    check ("armed input dominates held-note deferral",
        { true, false, false, true, false, true }, MidiFocusDecision::preserveRouting);
    check ("record safety dominates all other restrictions",
        { false, true, true, true, true, true }, MidiFocusDecision::deferRecording);

    if (failures != 0) return 1;
    std::puts ("Studio MIDI audition focus policy passed.");
    return 0;
}
