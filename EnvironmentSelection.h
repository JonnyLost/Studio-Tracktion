// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace studio::environments
{
constexpr int sceneCount = 5;
constexpr int allScenesMask = (1 << sceneCount) - 1;

enum class Rotation { random, sequential, fixed };

// An empty favourite list means that all supplied scenes are eligible.
constexpr int eligibleMask (int favourites)
{
    const auto valid = favourites & allScenesMask;
    return valid != 0 ? valid : allScenesMask;
}

// The caller supplies randomness so launch/rotation behaviour can be verified
// without a global RNG, clock, UI component, or audio-engine dependency.
constexpr int nextScene (int previous, int favourites, Rotation rotation, std::uint32_t draw)
{
    std::array<int, sceneCount> eligible {};
    int count = 0;
    const auto mask = eligibleMask (favourites);
    for (int scene = 0; scene < sceneCount; ++scene)
        if ((mask & (1 << scene)) != 0)
            eligible[(std::size_t) count++] = scene;

    if (rotation == Rotation::fixed)
        return previous >= 0 && previous < sceneCount ? previous : eligible[0];

    if (rotation == Rotation::sequential)
    {
        for (int i = 0; i < count; ++i)
            if (eligible[(std::size_t) i] > previous)
                return eligible[(std::size_t) i];
        return eligible[0];
    }

    // Do not repeat the currently visible scene while another eligible scene
    // is available. Every remaining scene has equal selection probability.
    int options = 0;
    for (int i = 0; i < count; ++i)
        if (eligible[(std::size_t) i] != previous || count == 1)
            ++options;
    int choice = (int) (draw % (std::uint32_t) options);
    for (int i = 0; i < count; ++i)
        if (eligible[(std::size_t) i] != previous || count == 1)
            if (choice-- == 0)
                return eligible[(std::size_t) i];
    return eligible[0];
}
}
