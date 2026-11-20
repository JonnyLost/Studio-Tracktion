// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "SpatialModel.h"
#include <cstddef>
#include <vector>

namespace studio::spatial
{
// One wet-only stereo tank. prepare/reset are non-render operations.
class RoomReverb
{
public:
    void prepare (double sampleRate, Room initial);
    void reset();
    void setRoom (Room);
    void process (float* left, float* right, int count) noexcept;
    std::size_t allocatedBytes() const noexcept;

private:
    struct Line
    {
        std::vector<float> samples;
        std::size_t cursor = 0;
        std::array<float, 5> filtered {};
    };
    struct Taps
    {
        std::array<float, 4> delay {};
        std::array<std::array<float, 6>, 4> feedback {};
        float early = 0, late = 0, output = 0;
    };
    double rate = 48000;
    float smooth = 0.001f;
    std::array<Line, 4> tank;
    std::array<float, 5> poles {};
    std::array<std::vector<float>, 2> history;
    std::size_t cursor = 0;
    Taps target, current;
    Room room;
    void updateTargets();
    float read (unsigned channel, float delay) const noexcept;
};
}
