// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "SpatialModel.h"
#include <cstddef>
#include <vector>

namespace studio::spatial
{
inline constexpr float roomCheckBoostDb = 12.0f;
inline constexpr std::size_t roomLineCount = 16, roomEarlyTaps = 12;

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
    float early = 0, late = 0, level = 0, bypass = 0;
    float mode = 0, diffusion = 1, buildup = 1, motion = .00028f, motionRate = 1;
    float width = 1, lowEq = 1, highEq = 1, mix = 1, standalone = 0;
    float leftGain = 1, rightGain = 1;
};

enum class RoomMode { legacy, stage, effect };
RoomPlan makeRoomPlan (Room, RoomMode = RoomMode::legacy);

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
    int activeLines() const noexcept { return lines; }
    std::size_t allocatedBytes() const noexcept;

private:
    struct Ring
    {
        std::vector<float> samples;
        std::size_t cursor = 0, mask = 0, written = 0;
        void prepare (std::size_t);
        void clear() noexcept;
        float read (float delay) const noexcept;
        void push (float) noexcept;
    };
    struct Modulator { float value = 0, step = 0; };

    using Coefficients = std::array<std::array<float, roomLineCount>, 6>;
    double rate = 48000;
    float smooth = 0.001f, controlSmooth = 0.014f;
    int geometryStep = 0, geometryFrames = 0, controlFrame = 0;
    int quietFrames = 0, lines = 8, mode = 0;
    float crossfade = 1, outputMix = 1, stereoWidth = 1, motionDepth = .00028f, motionSpeed = 1;
    float diffusionAmount = 1, build = 1, equalLow = 1, equalHigh = 1, leftGain = 1, rightGain = 1;
    std::array<std::array<float, 2>, 2> eqState {};
    std::array<float, 2> eqPoles {};
    void switchNetwork() noexcept;
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
    std::array<std::array<Ring, 5>, 2> diffusers;
    std::array<std::array<float, 5>, 2> diffuserDelays {};
    float early = 0, late = 0, gain = 0, targetGain = 0, inputGain = 1;

    void updateOutputTarget() noexcept;
    void startGeometryChange() noexcept;
    void updateCoefficients() noexcept;
    float diffuse (std::size_t channel, float sample) noexcept;
};
}
