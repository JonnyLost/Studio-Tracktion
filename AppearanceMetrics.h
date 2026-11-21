// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>

namespace studio
{
enum class DockMode { compact, split, expanded };
struct DockRoom { int panelHeight, splitLimit; };
enum class Workspace { closed, instruments, channel, stage, articulations, piano, mix, midi, rooms, microphones, resources, instrumentEditor, orchestraSeating, count };

class WorkspaceScroll
{
public:
    void store (Workspace workspace, int y) { offsets[(std::size_t) workspace] = std::max (0, y); }
    int position (Workspace workspace) const { return offsets[(std::size_t) workspace]; }

private:
    std::array<int, (std::size_t) Workspace::count> offsets {};
};

// Sizes are macOS logical points. The component peer still owns backing-pixel
// scaling, and neither setting participates in musical time or pitch zoom.
class AppearanceMetrics
{
public:
    static constexpr std::array interfaceOptions { 100, 110, 125, 150, 175 };
    static constexpr std::array textOptions { 100, 115, 130 };

    void set (int interfacePercent, int textPercent)
    {
        scale = validated (interfaceOptions, interfacePercent);
        text = validated (textOptions, textPercent);
    }

    int interfacePercent() const { return scale; }
    int textPercent() const { return text; }
    int px (int points) const { return (points * scale + 50) / 100; }
    int span (int points) const { return (points * scale * text + 5000) / 10000; }
    float fontSize (float points) const { return points * (float) (scale * text) / 10000.0f; }

    int line (int fontPoints, int padding = 4) const
    {
        return (fontPoints * scale * text * 12 + 99999) / 100000 + px (padding);
    }

    int control (int points = 26, int fontPoints = 13) const
    {
        return std::max (px (points), line (fontPoints, 6));
    }

    int scrollbar() const { return px (14); }
    int trackHeader() const { return std::min (span (150), px (164)); }
    int trackFooter() const { return px (106); }
    int trackHeight() const { return std::max (px (50), line (15) + control (20)); }
    int trackGap() const { return px (2); }
    int transportKey() const { return px (39); }
    int transportRowHeight() const { return line (9, 1) + line (15, 1) + 4 * px (1); }
    int transportPrimaryWidth() const { return 4 * transportKey() + span (92) + span (73) + span (60); }
    bool elapsedFits (int width) const { return width >= transportPrimaryWidth() + span (102); }
    int mixerStripWidth (int width) const { return span (width < span (950) ? 120 : 134); }

    bool compactBrowser (int width) const { return width < span (1000); }
    bool browserFilterRow (int width) const { return width < span (660); }
    bool stackedBrowserDetails (int width) const { return width < span (445); }

    static constexpr int resourceCPUWidth = 68, resourceRAMWidth = 105, resourceXRWidth = 55;
    static constexpr int viewWidth = 62, projectMenuWidth = 74;

    bool resourcesFitHeader (int width) const
    {
        return width >= span (resourceCPUWidth) + span (resourceRAMWidth)
            + span (viewWidth) + span (projectMenuWidth) + span (120);
    }

    bool dropoutsFitHeader (int width) const { return resourcesFitHeader (width - span (resourceXRWidth)); }

    DockRoom dockRoom (int available, int preferred, DockMode mode) const
    {
        const int upperTools = control (32) + control (26) + px (9);
        const int room = std::max (0, available - upperTools);
        const int desiredDock = control (34) + control (26);
        const int expandedArrangement = px (28);
        const int splitArrangement = std::clamp (room - desiredDock, expandedArrangement, px (65));
        const int limit = std::max (0, room - splitArrangement);
        if (mode == DockMode::split)
            return { std::clamp (preferred, std::min (px (170), limit), limit), limit };
        return { mode == DockMode::expanded ? std::max (0, room - expandedArrangement)
                     : std::min (room, control (34) + line (13, 8)), limit };
    }

    int adjacentInterfacePercent (int delta) const
    {
        const auto index = (int) (std::find (interfaceOptions.begin(), interfaceOptions.end(), scale) - interfaceOptions.begin());
        return interfaceOptions[(std::size_t) std::clamp (index + delta, 0, (int) interfaceOptions.size() - 1)];
    }

private:
    template <std::size_t N>
    static int validated (const std::array<int, N>& options, int value)
    {
        return std::find (options.begin(), options.end(), value) != options.end() ? value : 100;
    }

    int scale = 100, text = 100;
};

// Studio's single message-thread appearance. Tracktion and native plug-in
// windows keep their own audio/rendering state.
inline AppearanceMetrics appearance;
}
