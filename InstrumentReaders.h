// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "InstrumentLibrary.h"

namespace studio::instrumentimport
{
using SampleLocator = std::function<juce::File (const juce::String&)>;
std::vector<Definition> readSoundFont (const juce::File&, Progress&);
Definition readExs (const juce::File&, const SampleLocator&);
Definition readDecent (const juce::File&, const SampleLocator&);
}
