// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <JuceHeader.h>
#include <cstdint>
#include <optional>
#include <vector>

namespace studio
{
enum class ResourceAttribution { measured, estimated, unavailable };

struct InstrumentDiagnosticSnapshot
{
    ResourceAttribution attribution = ResourceAttribution::unavailable;
    std::optional<std::uint64_t> memoryBytes, sampleBytes, cacheBytes, streamingBytes;
    std::optional<unsigned> activeVoices, peakVoices;
    juce::String detail;
};

// Native instruments can expose an already-published snapshot. Called on the
// message thread; the implementation owns publication from its engine.
struct InstrumentDiagnostics
{
    virtual ~InstrumentDiagnostics() = default;
    virtual InstrumentDiagnosticSnapshot resourceSnapshot() const = 0;
};

class StudioResources final : public juce::Component,
                              private juce::Timer,
                              private juce::TableListBoxModel
{
public:
    explicit StudioResources (tracktion::DeviceManager&);
    ~StudioResources() override;
    juce::TextButton audio { "CPU --" }, ram { "RAM --" }, dropouts { "Audio --" };
    std::function<void()> onOpen;
    void setEdit (tracktion::Edit*);
    void appearanceChanged();
    int minimumContentHeight (int width);
    void resized() override;
    void visibilityChanged() override;

private:
    struct Row
    {
        juce::String track, instrument, type, detail;
        std::optional<std::uint64_t> bytes;
        ResourceAttribution status = ResourceAttribution::unavailable;
        bool operator== (const Row&) const = default;
    };

    tracktion::DeviceManager& devices;
    tracktion::Edit* edit = nullptr;
    struct ProcessStats;
    std::unique_ptr<ProcessStats> process;
    juce::Label current, peaks, system;
    juce::TextButton reset { "Reset peaks" };
    juce::TableListBox table { "Instrument resources", this };
    std::vector<Row> rows;
    std::optional<std::uint64_t> peakMemory;
    std::optional<float> peakLoad;
    bool inspectorTick = false;

    void timerCallback() override;
    void updateRows();
    int getNumRows() override;
    void paintRowBackground (juce::Graphics&, int, int, int, bool) override;
    void paintCell (juce::Graphics&, int, int, int, int, bool) override;
    void sortOrderChanged (int, bool) override;
    juce::String getCellTooltip (int, int) override;
    juce::String cellText (const Row&, int) const;
};
}
