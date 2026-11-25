// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "StudioSampler.h"

namespace studio::garritan
{
enum class Compatibility { supported, partial, unsupported };
const char* statusName (Compatibility);

struct Definition
{
    juce::File source;
    sampler::Instrument instrument;
    std::size_t decodedBytes = 0;
    Compatibility compatibility = Compatibility::unsupported;
    juce::StringArray issues;
    juce::Array<juce::var> regions;
};

struct Inventory
{
    juce::File root;
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

Inventory scan (const juce::File& root, Progress&);
juce::Result saveInventory (const Inventory&, const juce::File& directory, juce::File& output);
juce::Result convert (const Inventory&, const Definition&, const juce::File& directory,
                      bool allowPartial, Progress&, juce::File& output);
}
