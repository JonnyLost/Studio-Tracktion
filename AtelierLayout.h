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
    const int columns = inner >= m.span (316) ? 4 : 2;
    const int artCount = std::clamp (mappingCount, 0, 8);
    const int artRows = profile == AtelierProfile::synth ? 0 : std::max (1, (artCount + columns - 1) / columns);
    const int ccCount = profile == AtelierProfile::synth ? 0 : profile == AtelierProfile::hosted && advanced ? 8 : 4;
    const int ccRows = (ccCount + columns - 1) / columns;
    const int portrait = std::clamp (inner * 3 / 4, m.px (110), m.px (180));
    const int cell = std::max (m.px (82), m.control (18, 10) + m.px (52));
    const int art = artRows == 0 ? 0 : m.px (9) + m.control (20) + artRows * m.control (37) + m.control (28);
    const int cc = ccRows == 0 ? 0 : m.px (9) + m.control (20)
        + (profile == AtelierProfile::hosted ? m.control (27) : 0)
        + m.control (28) + ccRows * cell + m.control (30, 10);
    const int total = 2 * m.px (9) + m.control (26) + m.control (25, 11) + m.px (6)
        + portrait + m.control (28, 11) + art + cc + m.px (9) + m.control (22) + m.px (204)
        + m.px (5) + m.control (30, 10) + m.control (34);
    return { portrait, columns, artRows, ccRows, cell, total };
}
}
