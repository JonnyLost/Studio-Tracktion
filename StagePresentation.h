// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "SpatialModel.h"

namespace studio::spatial
{
// Every acoustic depth uses its own chord of the stage. This keeps the full
// saved pan range available at the back as well as the front.
struct StageSemicircle
{
    static constexpr float front = 0.07f, back = 0.91f, edge = 0.94f;

    static float halfWidth (float d) { return std::sqrt (std::max (0.0f, 1.0f - d * d)); }

    static std::array<float, 2> project (Position raw)
    {
        const auto p = validPosition (raw);
        const auto d = std::lerp (front, back, p.depth);
        return { p.x * edge * halfWidth (d), d };
    }

    static Position unproject (float x, float depth)
    {
        const auto d = bounded (depth, front, back, front);
        return validPosition ({ x / (edge * halfWidth (d)), (d - front) / (back - front) });
    }

    static bool contains (float x, float depth)
    {
        return depth >= 0.0f && depth <= 1.0f && std::abs (x) <= halfWidth (depth);
    }
};

struct StagePresentation
{
    float frontWidth, rearWidth, curvature;
    std::array<float, 4> rows;
    int rowCount;

    constexpr float width (float depth) const { return std::lerp (frontWidth, rearWidth, depth); }
    constexpr float rise (float x) const { return curvature * x * x; }

    constexpr std::array<float, 2> project (Position seat) const
    {
        return { 0.5f + 0.5f * seat.x * width (seat.depth),
                 1.0f - rise (seat.x) - 0.81f * seat.depth };
    }

    Position unproject (float x, float y) const
    {
        // Width grows toward the back. Solve for the displayed depth at the
        // cursor without changing the saved pan/depth coordinate system.
        const float offset = 2.0f * x - 1.0f;
        const auto pan = [&] (float depth) { return std::clamp (offset / width (depth), -1.0f, 1.0f); };
        float front = 0.0f, rear = 1.0f;
        for (int step = 0; step < 16; ++step)
        {
            const float mid = (front + rear) * 0.5f;
            if (1.0f - rise (pan (mid)) - 0.81f * mid > y) front = mid;
            else rear = mid;
        }
        const float depth = (front + rear) * 0.5f;
        return { pan (depth), depth < 0.0001f ? 0.0f : depth > 0.9999f ? 1.0f : depth };
    }
};

inline constexpr std::array stagePresentations {
    StagePresentation { 0.76f, 1.00f, 0.16f, { 0.21f, 0.46f, 0.67f, 0.86f }, 4 },
    StagePresentation { 0.65f, 0.78f, 0.12f, { 0.16f, 0.36f, 0.60f, 0.0f }, 3 },
    StagePresentation { 0.94f, 0.98f, 0.04f, { 0.18f, 0.41f, 0.65f, 0.82f }, 4 }
};
}
