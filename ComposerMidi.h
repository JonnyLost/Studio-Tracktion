// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <JuceHeader.h>

namespace studio::midi
{
struct Smf
{
    juce::MidiFile file;
    int type = 0;
};

juce::Result read (const juce::File&, Smf&);
juce::Result import (tracktion::Edit&, const Smf&, tracktion::TimePosition at,
                     tracktion::AudioTrack* destination, bool useFileTempo);
juce::Result write (tracktion::Edit&, const juce::Array<tracktion::MidiClip*>&, const juce::File&);
juce::Result join (const juce::Array<tracktion::MidiClip*>&, tracktion::MidiClip::Ptr& result);
}
