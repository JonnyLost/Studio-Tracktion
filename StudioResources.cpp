// SPDX-License-Identifier: GPL-3.0-or-later
#include "StudioResources.h"
#include "StudioMixing.h"
#include "ResourceSample.h"
#include "StudioUI.h"

#include <chrono>
#include <cmath>

#if JUCE_MAC
 #include <dispatch/dispatch.h>
 #include <mach/mach.h>
 #include <sys/resource.h>
 #include <sys/sysctl.h>
#endif

using namespace juce;

namespace studio
{
namespace
{
String memoryText (std::optional<std::uint64_t> bytes)
{
    if (! bytes) return "Unavailable";
    if (*bytes >= 1000000000) return String ((double) *bytes / 1000000000.0, 2) + " GB";
    return String ((double) *bytes / 1000000.0, 1) + " MB";
}

String loadText (std::optional<float> value)
{
    return value ? String (roundToInt (*value * 100.0f)) + "%" : "Unavailable";
}
}

struct StudioResources::ProcessStats
{
    std::optional<std::uint64_t> footprint, physical;
    std::optional<float> cpu;
    String pressure { "Unavailable" };

   #if JUCE_MAC
    dispatch_source_t source = nullptr;
    std::optional<double> previousCPU;
    std::chrono::steady_clock::time_point lastSample;

    ProcessStats()
    {
        std::uint64_t bytes = 0;
        auto size = sizeof (bytes);
        if (sysctlbyname ("hw.memsize", &bytes, &size, nullptr, 0) == 0 && size == sizeof (bytes))
            physical = bytes;
        source = dispatch_source_create (DISPATCH_SOURCE_TYPE_MEMORYPRESSURE, 0,
            DISPATCH_MEMORYPRESSURE_NORMAL | DISPATCH_MEMORYPRESSURE_WARN | DISPATCH_MEMORYPRESSURE_CRITICAL,
            dispatch_get_main_queue());
        if (source != nullptr)
        {
            pressure = "Awaiting macOS notification";
            dispatch_set_context (source, this);
            dispatch_source_set_event_handler_f (source, [] (void* context)
            {
                if (auto* stats = static_cast<ProcessStats*> (context))
                {
                    const auto state = dispatch_source_get_data (stats->source);
                    stats->pressure = (state & DISPATCH_MEMORYPRESSURE_CRITICAL) != 0 ? "Critical"
                        : (state & DISPATCH_MEMORYPRESSURE_WARN) != 0 ? "Warning" : "Normal";
                }
            });
            dispatch_activate (source);
        }
    }

    ~ProcessStats()
    {
        // The source and its handler both belong to the main queue.
        if (source != nullptr)
        {
            dispatch_set_context (source, nullptr);
            dispatch_source_cancel (source);
            dispatch_release (source);
        }
    }
   #endif

    void sample()
    {
       #if JUCE_MAC
        task_vm_info_data_t info {};
        mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
        footprint.reset();
        if (task_info (mach_task_self(), TASK_VM_INFO, reinterpret_cast<task_info_t> (&info), &count) == KERN_SUCCESS
            && count >= TASK_VM_INFO_REV1_COUNT)
            footprint = info.phys_footprint;

        struct rusage usage {};
        cpu.reset();
        if (getrusage (RUSAGE_SELF, &usage) == 0)
        {
            const auto now = std::chrono::steady_clock::now();
            const double seconds = (double) usage.ru_utime.tv_sec + (double) usage.ru_stime.tv_sec
                + ((double) usage.ru_utime.tv_usec + (double) usage.ru_stime.tv_usec) / 1000000.0;
            const auto elapsed = std::chrono::duration<double> (now - lastSample).count();
            if (previousCPU)
                cpu = processCPUFraction (*previousCPU, seconds, elapsed);
            previousCPU = seconds;
            lastSample = now;
        }
       #endif
    }
};

StudioResources::StudioResources (tracktion::DeviceManager& manager)
    : devices (manager), process (std::make_unique<ProcessStats>())
{
    for (auto* button : { &audio, &ram, &dropouts })
        button->onClick = [this] { if (onOpen) onOpen(); };

    audio.setTooltip ("Audio callback utilization. Open Resource Inspector.");
    ram.setTooltip ("Studio process physical footprint. Open Resource Inspector.");
    dropouts.setTooltip ("Native audio device overloads. Open Resource Inspector.");
    for (Component* item : std::initializer_list<Component*> { &current, &peaks, &system, &reset, &table })
        addAndMakeVisible (*item);
    reset.onClick = [this] { peakMemory.reset(); peakLoad.reset(); };

    auto& header = table.getHeader();
    header.addColumn ("Track", 1, 128);
    header.addColumn ("Instrument", 2, 148);
    header.addColumn ("Type", 3, 98);
    header.addColumn ("Memory", 4, 86);
    header.addColumn ("Status", 5, 104);
    header.addColumn ("Diagnostics", 6, 188);
    header.setSortColumnId (1, true);
    table.setColour (ListBox::backgroundColourId, colours::canvas);
    appearanceChanged();
    startTimerHz (2);
}

StudioResources::~StudioResources()
{
    stopTimer();
    table.setModel (nullptr);
}

void StudioResources::setEdit (tracktion::Edit* next)
{
    edit = next;
    rows.clear();
    table.updateContent();
    if (isShowing()) updateRows();
}

void StudioResources::appearanceChanged()
{
    const auto& m = appearance;
    table.setHeaderHeight (m.control (25));
    table.setRowHeight (m.line (13, 6));
    table.getViewport()->setScrollBarThickness (m.scrollbar());
    constexpr int widths[] { 128, 148, 98, 86, 104, 188 };
    for (int i = 0; i < 6; ++i) table.getHeader().setColumnWidth (i + 1, m.span (widths[i]));
    resized();
}

int StudioResources::minimumContentHeight (int width)
{
    const auto& m = appearance;
    return 2 * m.px (6) + studio::toolbarHeight (width - m.px (12), { { &reset, 116 } })
        + 6 * m.line (13, 2) + table.getHeaderHeight() + 3 * table.getRowHeight() + m.scrollbar();
}

void StudioResources::resized()
{
    const auto& m = appearance;
    auto area = getLocalBounds().reduced (m.px (6));
    studio::layoutToolbar (area, { { &reset, 116 } });
    for (auto* label : { &current, &peaks, &system })
        label->setBounds (area.removeFromTop (2 * m.line (13, 2)));
    table.setBounds (area);
}

void StudioResources::visibilityChanged()
{
    if (isShowing()) updateRows();
}

void StudioResources::timerCallback()
{
    // JUCE's timer runs on the message thread. Studio installs no new audio callbacks.
    process->sample();
    if (process->footprint && (! peakMemory || *process->footprint > *peakMemory))
        peakMemory = process->footprint;
    std::optional<float> load;
    int overruns = -1;
    if (auto* device = devices.deviceManager.getCurrentAudioDevice(); device != nullptr && device->isPlaying())
    {
        const auto utilization = devices.getCpuUsage();
        if (std::isfinite (utilization) && utilization >= 0) load = utilization;
        overruns = device->getXRunCount(); // Native device counter, without JUCE's timing-estimate fallback.
    }
    if (load && (! peakLoad || *load > *peakLoad)) peakLoad = load;
    audio.setButtonText (load ? "CPU " + loadText (load) : "CPU --");
    ram.setButtonText (process->footprint ? "RAM " + memoryText (process->footprint) : "RAM --");
    dropouts.setButtonText (overruns >= 0 ? "XR " + String (overruns) : "XR --");
    dropouts.setColour (TextButton::textColourOffId, overruns > 0 ? colours::warning : colours::secondary);
    if (! isShowing()) return;
    current.setText ("Audio callback " + loadText (load) + "  |  Process CPU (100% per core) "
        + loadText (process->cpu) + "  |  Studio footprint " + memoryText (process->footprint), dontSendNotification);
    peaks.setText ("Sampled peaks: audio " + loadText (peakLoad) + "  |  Studio footprint " + memoryText (peakMemory)
        + "  |  Device overloads " + (overruns >= 0 ? String (overruns) : "Unavailable"), dontSendNotification);
    system.setText ("Physical RAM " + memoryText (process->physical) + "  |  Memory pressure " + process->pressure,
        dontSendNotification);
    for (auto* label : { &current, &peaks, &system }) label->setTooltip (label->getText());
    inspectorTick = ! inspectorTick;
    if (inspectorTick) updateRows();
}

void StudioResources::updateRows()
{
    std::vector<Row> next;
    if (edit != nullptr)
        for (auto* track : tracktion::getAudioTracks (*edit))
            for (auto* instrument : track->pluginList)
            {
                if (! isInstrumentPlugin (*instrument)) continue;
                Row row { track->getName(), instrument->getName(),
                          dynamic_cast<tracktion::ExternalPlugin*> (instrument) != nullptr ? "Hosted Audio Unit" : "Studio",
                          "The instrument does not report per-instance memory.", {}, ResourceAttribution::unavailable };
                if (auto* provider = dynamic_cast<InstrumentDiagnostics*> (instrument))
                {
                    const auto report = provider->resourceSnapshot();
                    row.bytes = report.attribution == ResourceAttribution::unavailable ? std::nullopt : report.memoryBytes;
                    row.status = row.bytes ? report.attribution : ResourceAttribution::unavailable;
                    row.detail = report.detail;
                    if (report.sampleBytes) row.detail << " Samples " << memoryText (report.sampleBytes);
                    if (report.cacheBytes) row.detail << " Cache " << memoryText (report.cacheBytes);
                    if (report.streamingBytes) row.detail << " Streaming " << memoryText (report.streamingBytes);
                    if (report.activeVoices) row.detail << " Voices " << String (*report.activeVoices);
                    if (report.peakVoices) row.detail << " Peak voices " << String (*report.peakVoices);
                }
                next.push_back (std::move (row));
            }
    rows = std::move (next);
    sortOrderChanged (table.getHeader().getSortColumnId(), table.getHeader().isSortedForwards());
    table.updateContent();
}

int StudioResources::getNumRows() { return (int) rows.size(); }

void StudioResources::paintRowBackground (Graphics& g, int row, int, int, bool selected)
{
    g.fillAll (selected ? colours::selected : row % 2 == 0 ? colours::canvas : colours::control);
}

String StudioResources::cellText (const Row& row, int column) const
{
    switch (column)
    {
        case 1: return row.track;
        case 2: return row.instrument;
        case 3: return row.type;
        case 4: return row.bytes ? memoryText (row.bytes) : "-";
        case 5: return row.status == ResourceAttribution::measured ? "Measured"
                         : row.status == ResourceAttribution::estimated ? "Estimated" : "Unavailable";
        case 6: return row.detail;
        default: return {};
    }
}

void StudioResources::paintCell (Graphics& g, int row, int column, int w, int h, bool)
{
    g.setColour (column >= 5 ? colours::secondary : colours::text);
    g.setFont (uiFont (13.0f));
    auto area = Rectangle<int> (w, h).reduced (appearance.px (4), 0);
    g.drawFittedText (getCellTooltip (row, column), area, Justification::centredLeft, 1, 1.0f);
}

String StudioResources::getCellTooltip (int row, int column)
{
    return isPositiveAndBelow (row, (int) rows.size()) ? cellText (rows[(size_t) row], column) : String();
}

void StudioResources::sortOrderChanged (int column, bool forwards)
{
    std::stable_sort (rows.begin(), rows.end(), [&] (const Row& a, const Row& b)
    {
        if (column == 4 && a.bytes.has_value() != b.bytes.has_value()) return a.bytes.has_value();
        const int comparison = column == 4 && a.bytes && b.bytes ? (*a.bytes < *b.bytes ? -1 : *a.bytes > *b.bytes ? 1 : 0)
            : cellText (a, column).compareNatural (cellText (b, column));
        return forwards ? comparison < 0 : comparison > 0;
    });
    table.repaint();
}
}
