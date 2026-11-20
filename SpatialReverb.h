// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "SpatialModel.h"
#include <cstddef>
#include <vector>

namespace studio::spatial
{
inline constexpr float roomCheckBoostDb = 12.0f;
inline constexpr std::size_t roomLineCount = 8, roomEarlyTaps = 12;

struct RoomPeaks { float input = 0.0f, wet = 0.0f; };

struct RoomGeometry
{
    std::array<float, roomLineCount> delay {};
    std::array<float, roomEarlyTaps> reflection {};
    float preDelay = 0.0f;
    bool operator== (const RoomGeometry&) const = default;
};

// Delays are in seconds. The plan is constructed from project state off the
// render thread and published by the Tracktion adapter as fixed-size scalars.
struct RoomPlan
{
    RoomGeometry geometry;
    std::array<std::array<float, roomLineCount>, 6> feedback {};
    std::array<float, roomEarlyTaps> reflectionGain {};
    float early = 0.0f, late = 0.0f, level = 0.0f, bypass = 0.0f;
};

RoomPlan makeRoomPlan (Room);

class RoomReverb
{
public:
    void prepare (double sampleRate, Room initial) { preparePlan (sampleRate, makeRoomPlan (initial)); }
    void preparePlan (double sampleRate, const RoomPlan&);
    void reset();
    void setRoom (Room room) { setPlan (makeRoomPlan (room)); }
    void setPlan (const RoomPlan&) noexcept;
    void setCheckEnabled (bool) noexcept;
    RoomPeaks process (float* left, float* right, int count) noexcept;
    std::size_t allocatedBytes() const noexcept;

private:
    struct Ring
    {
        std::vector<float> samples;
        std::size_t cursor = 0, mask = 0;
        void prepare (std::size_t);
        void clear();
        float read (float delay) const noexcept;
        void push (float) noexcept;
    };
    struct Modulator { float value = 0, step = 0; };

    using Coefficients = std::array<std::array<float, roomLineCount>, 6>;
    double rate = 48000;
    float smooth = 0.001f, controlSmooth = 0.014f;
    int geometryStep = 0, geometryFrames = 0, controlFrame = 0;
    int quietFrames = 0;
    bool checkEnabled = false, sleeping = true, settling = false;
    RoomPlan target;
    RoomGeometry from, to;
    std::array<Ring, roomLineCount> tank;
    std::array<Modulator, roomLineCount> modulation;
    std::array<float, 5> poles {};
    std::array<std::array<float, roomLineCount>, 5> filters {};
    Coefficients feedback {};
    std::array<float, roomEarlyTaps> reflectionGain {};
    std::array<Ring, 2> inputHistory;
    std::array<std::array<Ring, 3>, 2> diffusers;
    std::array<std::array<float, 3>, 2> diffuserDelays {};
    float early = 0, late = 0, gain = 0, targetGain = 0;

    void updateOutputTarget() noexcept;
    void startGeometryChange() noexcept;
    void updateCoefficients() noexcept;
    float diffuse (std::size_t channel, float sample) noexcept;
};
}
