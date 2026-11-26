// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <juce_core/juce_core.h>
#include <functional>
#include <map>

namespace studio::aria
{
struct Patch
{
    juce::String id, name, library, family, kind, health, midiRange;
    juce::File source;
    juce::StringArray articulations;
    juce::int64 modified = 0, size = 0;
    bool present = true;
    int literalSamples = 0, missingSamples = 0;
    juce::String libraryVersion;
};

struct Preset
{
    juce::String pluginId, pluginVersion, state;
    int program = 0;
};

using Patches = std::map<juce::String, Patch>;

struct Progress
{
    juce::File root;
    int examined = 0, discovered = 0, missing = 0, errors = 0;
};

class Catalog
{
public:
    explicit Catalog (juce::File directory);
    static juce::Array<juce::File> defaultRoots();
    static juce::File localDirectory();

    void read();
    juce::Result save() const;
    void scan (juce::Array<juce::File> roots,
               const std::function<bool (const Progress&)>& shouldCancel);
    juce::String report() const;
    juce::String status (const Patch&) const;
    Preset preset (const Patch&) const;
    juce::Result registerPreset (const Patch&, const Preset&);
    const Patches& patches() const { return entries; }
    const juce::Array<juce::File>& pendingRoots() const { return pending; }
    const juce::Array<juce::File>& scannedRoots() const { return roots; }

private:
    juce::File directory;
    Patches entries;
    juce::Array<juce::File> pending, roots;
    int examined = 0, errors = 0;
    juce::File presetFile (const Patch&) const;
};
}
