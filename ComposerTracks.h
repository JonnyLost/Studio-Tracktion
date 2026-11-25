// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <JuceHeader.h>

namespace studio::tracks
{
juce::String restriction (tracktion::AudioTrack&, bool destructive);
bool rename (tracktion::AudioTrack&, const juce::String&);
bool canMove (tracktion::AudioTrack&, int direction);
void move (tracktion::AudioTrack&, int direction);
tracktion::Track::Ptr duplicate (tracktion::AudioTrack&);
void remove (tracktion::AudioTrack&);
// A real Tracktion submix folder, not a visual label or duplicate bus.
tracktion::FolderTrack::Ptr createSubmix (tracktion::Edit&, const juce::Array<tracktion::AudioTrack*>&, const juce::String&);
bool moveIntoSubmix (tracktion::AudioTrack&, tracktion::FolderTrack&);
bool removeFromSubmix (tracktion::AudioTrack&);
}
