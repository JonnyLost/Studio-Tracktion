// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

namespace studio::sampler
{
constexpr int maxVoices = 64, maxZones = 128, maxGroups = 16;
constexpr std::size_t maxSampleBytes = 64 * 1024 * 1024;
constexpr std::size_t maxBankBytes = 256 * 1024 * 1024;

struct Sample
{
    double rate = 0;
    std::vector<float> left, right;
    std::size_t bytes() const { return (left.size() + right.size()) * sizeof (float); }
};

struct Zone
{
    int root = 60, low = 0, high = 127, soft = 1, hard = 127;
    float gainDb = 0, cents = 0;
    bool oneShot = false, loop = false;
    int start = 0, end = 0; // Zero end means the complete sample (legacy presets).
    int loopStart = 0, loopEnd = 0, group = 0; // End is exclusive, group zero layers.
    std::shared_ptr<const Sample> sample;
};

struct Settings
{
    float attack = 0.005f, decay = 0.12f, sustain = 1.0f, release = 0.12f;
    float gainDb = -9, semitones = 0, bendRange = 2;
    int polyphony = 32;
};

struct Bank
{
    Settings settings;
    std::vector<Zone> zones;
    std::uint64_t sampleRevision = 0;
};

bool validMapping (const Zone&) noexcept;
bool validLoop (const Zone&, int frames) noexcept;

class Engine
{
public:
    void prepare (double rate) noexcept;
    void use (const Bank&) noexcept;
    void panic() noexcept;
    void midi (int status, int data1, int data2) noexcept;
    void preview (int source, int channel, int note, int velocity, int zone = -1) noexcept;
    void render (float* left, float* right, int frames) noexcept;
    void publishMeters() noexcept;
    unsigned activeVoices() const noexcept { return active.load (std::memory_order_relaxed); }
    bool noteActive (int note) const noexcept;

private:
    struct Channel
    {
        float volume = 1, expression = 1, bend = 0, modulation = 0, pressure = 0;
        bool pedal = false;
    };

    struct Voice
    {
        const Sample* sample = nullptr;
        int channel = 0, note = 0, source = 0;
        float velocityGain = 0, level = 0, releaseStep = 0;
        double position = 0, baseStep = 0;
        std::uint64_t age = 0;
        bool down = false, oneShot = false, loop = false;
        int end = 0, loopStart = 0, loopEnd = 0, previewFrames = 0;
        enum Stage { attack, decay, sustain, release } stage = attack;
    };

    std::array<Channel, 16> channels {};
    std::array<Voice, maxVoices> voices {};
    std::array<std::uint32_t, maxGroups + 1> roundRobin {};
    std::array<std::atomic<std::uint64_t>, 2> notes {};
    std::atomic<unsigned> active { 0 };
    const Bank* bank = nullptr; // Borrowed only during the current render call.
    std::uint64_t age = 0, revision = 0;
    double outputRate = 44100, vibratoPhase = 0;
    float instrumentGain = 0;
    Settings settings;

    void start (int source, int channel, int note, int velocity) noexcept;
    void stop (int source, int channel, int note) noexcept;
    void release (Voice&) noexcept;
    void trigger (const Zone&, int source, int channel, int note, int velocity) noexcept;
};
}
