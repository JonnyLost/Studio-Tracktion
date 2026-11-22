// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace studio
{
// Policy is deliberately independent of the host. Track selection is never
// permission to arm recording or steal an explicitly armed/multi-target input.
struct MidiFocusConditions
{
    bool playableTrack = false;
    bool transportRecording = false;
    bool inputRecording = false;
    bool armedDestination = false;
    bool multipleDestinations = false;
    bool notesOrPedalHeld = false;
};

enum class MidiFocusDecision { release, deferRecording, preserveRouting, waitForRelease, switchToTrack };

constexpr MidiFocusDecision midiFocusDecision (const MidiFocusConditions& c) noexcept
{
    if (c.transportRecording || c.inputRecording)
        return MidiFocusDecision::deferRecording;
    if (c.armedDestination || c.multipleDestinations)
        return MidiFocusDecision::preserveRouting;
    if (c.notesOrPedalHeld)
        return MidiFocusDecision::waitForRelease;
    return c.playableTrack ? MidiFocusDecision::switchToTrack : MidiFocusDecision::release;
}


} // namespace studio
