// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <JuceHeader.h>

#include <algorithm>

namespace studio
{
// One canonical bar/beat iterator for the arrangement and MIDI editor.
// Use Tracktion's tempo sequence, including its time signature boundaries,
// instead of assuming every measure contains four quarter notes.
struct MusicalTick
{
    tracktion::TimePosition time;
    int barIndex = 0;   // Zero-based: draw barIndex + 1 to the user.
    int beatIndex = 0;  // Zero-based within the current time signature.
};

template <typename Fn>
void forEachMusicalTick (const tracktion::TempoSequence& sequence,
                         tracktion::TimePosition from,
                         tracktion::TimePosition to,
                         Fn&& handleTick)
{
    if (to <= from)
        return;

    const int firstBar = std::max (0, sequence.toBarsAndBeats (from).bars - 1);
    // Fixed bound avoids pathological work on a very long, zoomed-out edit.
    for (int bar = firstBar; bar < firstBar + 2048; ++bar)
    {
        const auto startBar = tracktion::tempo::BarsAndBeats { bar, {} };
        const auto nextBar = tracktion::tempo::BarsAndBeats { bar + 1, {} };
        const auto start = sequence.toTime (startBar);
        const auto next = sequence.toTime (nextBar);
        if (start > to || next <= start)
            break;

        const int beats = std::clamp ((int) sequence.getTimeSigAt (start).numerator.get(), 1, 32);
        // Divide the *actual bar duration* into notated beats. This respects
        // both the numerator and denominator (e.g. 6/8 = six eighth notes),
        // and changes in tempo/time signature elsewhere in the song.
        const auto firstBeat = sequence.toBeats (startBar).inBeats();
        const auto barLength = sequence.toBeats (nextBar).inBeats() - firstBeat;
        for (int beat = 0; beat < beats; ++beat)
        {
            const auto position = sequence.toTime (tracktion::BeatPosition::fromBeats (
                firstBeat + (barLength * beat / beats)));
            if (position >= from && position <= to)
                handleTick (MusicalTick { position, bar, beat });
        }
    }
}
}
