// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "AppearanceMetrics.h"

namespace studio
{
enum class AtelierProfile { hosted, sampler, synth };
inline constexpr bool atelierControllerVisible (AtelierProfile profile, bool advanced, int index)
{
    if (index < 0 || index >= 8 || profile == AtelierProfile::synth) return false;
    if (profile == AtelierProfile::hosted && advanced) return true;
    return index == 0 || index == 2 || index == 3 || index == 4;
}
struct AtelierLayout
{
    int portrait, columns, articulationRows, controllerRows, controllerCell, total;
};
inline AtelierLayout atelierLayout (int width, int mappingCount, AtelierProfile profile, bool advanced,
                                    const AppearanceMetrics& m)
{
    const int inner = std::max (0, width - 2 * m.px (9));
    const int columns = inner >= m.span (250) ? 4 : 2;
    const int artCount = std::clamp (mappingCount, 0, 8);
    const int artRows = profile == AtelierProfile::synth ? 0 : std::max (1, (artCount + columns - 1) / columns);
    const int ccCount = profile == AtelierProfile::synth ? 0 : profile == AtelierProfile::hosted && advanced ? 8 : 4;
    const int ccRows = (ccCount + columns - 1) / columns;
    const int portrait = m.px (52);
    const int cell = std::max (m.px (58), m.control (18, 10) + m.px (34));
    const int art = artRows == 0 ? 0 : m.px (3) + m.control (18) + artRows * m.control (25,10) + m.control (24,11);
    const int cc = ccRows == 0 ? 0 : m.px (3) + m.control (24,11) + ccRows * cell
        + (profile == AtelierProfile::hosted && advanced ? m.control (26) + m.control (26,10) : 0);
    const int total = 2 * m.px (5) + m.control (20) + m.control (20,11) + m.px (3)
        + portrait + art + cc + m.px (3) + m.control (20) + m.px (80)
        + m.px (3) + m.control (26,11);
    return { portrait, columns, artRows, ccRows, cell, total };
}
}
