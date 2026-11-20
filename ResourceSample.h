// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cmath>
#include <optional>

namespace studio
{
// The process can consume more than one core during the sample interval.
inline std::optional<float> processCPUFraction (double previousSeconds, double currentSeconds,
                                                double elapsedWallSeconds)
{
    if (! std::isfinite (previousSeconds) || ! std::isfinite (currentSeconds)
        || ! std::isfinite (elapsedWallSeconds) || elapsedWallSeconds <= 0 || currentSeconds < previousSeconds)
        return std::nullopt;
    const auto fraction = (float) ((currentSeconds - previousSeconds) / elapsedWallSeconds);
    return std::isfinite (fraction) ? std::optional<float> (fraction) : std::nullopt;
}
}
