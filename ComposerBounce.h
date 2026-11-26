// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <JuceHeader.h>

namespace studio::bounce
{
std::unique_ptr<tracktion::Edit> createRenderSnapshot (tracktion::Edit&);
juce::Result verifySamplerSources (tracktion::Edit&, bool checkOfflineErrors);

struct Options
{
    tracktion::TimeRange range;
    double tail = 2.0;
    int sampleRate = 48000;
    int bitDepth = 24;
    bool mono = false;
    bool stems = false;
    bool room = true;
    juce::Array<tracktion::EditItemID> tracks;
    juce::File destination; // Mix file or stem directory.
};

class Job
{
public:
    static juce::Result create (tracktion::Edit&, const Options&, std::unique_ptr<Job>&);
    void start();
    void cancel();
    bool isFinished() const { return finished; }
    float progress() const;
    juce::Result publish();
    int fileCount() const { return static_cast<int> (outputs.size()); }

private:
    struct Output
    {
        juce::File destination;
        std::unique_ptr<juce::TemporaryFile> staged;
    };

    std::unique_ptr<tracktion::Edit> snapshot;
    std::vector<Output> outputs;
    tracktion::RenderQueue queue;
    bool cancelled = false, finished = false;
};
}
