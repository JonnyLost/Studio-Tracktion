// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "StudioSampler.h"

namespace studio::instrumentimport
{
enum class Compatibility { supported, partial, originalPlayer, unsupported };
const char* statusName (Compatibility);

struct Definition
{
    juce::File source;
    juce::String variant;
    sampler::Instrument instrument;
    std::size_t decodedBytes = 0;
    Compatibility compatibility = Compatibility::unsupported;
    juce::StringArray issues;
    juce::Array<juce::var> regions;
};

struct Inventory
{
    juce::File root;
    juce::File selection;
    bool complete = false;
    int reused = 0;
    juce::String listingFingerprint;
    std::vector<Definition> definitions;
    juce::Array<juce::var> samples;
    juce::StringArray issues;
    juce::var manifest() const;
};

struct Progress
{
    std::atomic<bool> cancelled { false };
    std::atomic<int> completed { 0 };
};

bool canConvert (Compatibility, bool allowPartial);
Inventory scan (const juce::File& selection, Progress&, const juce::File& indexDirectory = {},
                const juce::File& sampleRoot = {});
Inventory proposeSampleFolder (const juce::File&, Progress&);
juce::Result relinkPreset (const juce::File& preset, const juce::File& originalRoot,
                         const juce::File& newRoot, const juce::File& catalog,
                         Progress&, juce::File& output);
void writeIndex (const Inventory&, const juce::File& directory);
juce::Result saveInventory (const Inventory&, const juce::File& directory, juce::File& output);
juce::Result convert (const Inventory&, const Definition&, const juce::File& directory,
                      bool allowPartial, Progress&, juce::File& output);
}
