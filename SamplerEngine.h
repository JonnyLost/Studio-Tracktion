// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>
#include "SamplerStream.h"
#include <limits>

namespace studio::sampler
{
constexpr int maxVoices = 64, maxZones = 128, maxGroups = 16;
constexpr std::size_t maxSampleBytes = 64 * 1024 * 1024;
constexpr std::size_t maxBankBytes = 256 * 1024 * 1024;

struct Sample
{
    double rate = 0;
    std::int64_t sourceBytes = -1, sourceTime = 0;
    int rootNote = -1; // Verified file metadata, not acoustic pitch analysis.
    int embeddedLoopStart = -1, embeddedLoopEnd = -1; // exclusive end; forward WAV smpl loop
    std::vector<float> left, right;
    // For streamed recordings the vectors contain the immutable attack
    // preload, not the entire recording. Existing short/fixture samples
    // keep their original fully resident representation.
    int totalFrames = 0;
    std::shared_ptr<StreamCache> stream;
    int frameCount() const noexcept { return totalFrames > 0 ? totalFrames : static_cast<int> (left.size()); }
    bool stereo() const noexcept { return ! right.empty(); }
    std::size_t residentBytes() const noexcept { return (left.size() + right.size()) * sizeof (float); }
    std::size_t cacheBytes() const noexcept { return stream ? stream->bytes() : 0; }
    std::size_t bytes() const noexcept { return residentBytes() + cacheBytes(); }
    bool readFrame (int frame, StreamCache::Cursor& cursor, float& l, float& r, bool offline) const noexcept
    {
        if (frame < 0 || frame >= frameCount()) return false;
        if (static_cast<std::size_t> (frame) < left.size())
        {
            l = left[static_cast<std::size_t> (frame)];
            r = stereo() ? right[static_cast<std::size_t> (frame)] : l;
            return true;
        }
        return stream && cursor.read (*stream, frame, l, r, offline);
    }
};

// Optional imported per-region parameters. Legacy Studio instruments keep
// their original global envelopes, linear velocity and native fade controls.
struct SfzRegion
{
    bool enabled = false, sustainLoop = false, powerFade = true;
    float attack = 0, decay = 0, sustain = 1, release = 0.001f;
    float velocityTrack = 100, pan = 0;
    int transpose = 0, keyTrack = 100;
    int fadeInLow = 0, fadeInHigh = 0, fadeOutLow = 127, fadeOutHigh = 127;
};

struct Zone
{
    int root = 60, low = 0, high = 127, soft = 1, hard = 127;
    float gainDb = 0, cents = 0;
    bool oneShot = false, loop = false;
    int start = 0, end = 0; // Zero end means the complete sample (legacy presets).
    int loopStart = 0, loopEnd = 0, group = 0; // End is exclusive, group zero layers.
    int folder = 0, loopCrossfade = -1; // -1 preserves legacy 64-frame smoothing.
    int fadeIn = 0, fadeOut = 0; // Velocity steps inside the zone range.
    std::shared_ptr<const Sample> sample;
    SfzRegion sfz;
};

enum class GroupTrigger { normal, keyswitch, controller };
struct Group
{
    int id = 0;
    GroupTrigger trigger = GroupTrigger::normal;
    int key = 24, controller = 20, ccLow = 0, ccHigh = 127;
    int channel = 0; // Zero accepts any MIDI channel; otherwise 1-16.
    bool mute = false, solo = false, releaseTrigger = false;
    int switchDefault = -2; // -2 legacy first switch; -1 no initial switch.
};
bool validGroup (const Group&) noexcept;
float velocityWeight (const Zone&, int velocity) noexcept;

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
    std::vector<Group> groups;
    std::uint64_t sampleRevision = 0, switchRevision = 0;
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
    void preview (int source, int channel, int note, int velocity, int zone = -1, int first = -1, int end = -1) noexcept;
    void render (float* left, float* right, int frames, bool offline = false) noexcept;
    void publishMeters() noexcept;
    unsigned activeVoices() const noexcept { return active.load (std::memory_order_relaxed); }
    bool noteActive (int note) const noexcept;
    int activeKeyswitch (int channel) const noexcept { return channel >= 1 && channel <= 16 ? keyMeters[static_cast<std::size_t> (channel - 1)].load (std::memory_order_relaxed) : -1; }

private:
    struct Channel
    {
        float volume = 1, expression = 1, bend = 0, modulation = 0, pressure = 0;
        bool pedal = false;
        int selectedKey = -1;
        std::array<unsigned char, 128> cc {};
    };

    struct Voice
    {
        const Sample* sample = nullptr;
        int channel = 0, note = 0, source = 0;
        float velocityGain = 0, level = 0, releaseStep = 0;
        float streamGain = 1, lastLeft = 0, lastRight = 0;
        SfzRegion sfz;
        double position = 0, baseStep = 0;
        std::uint64_t age = 0;
        bool down = false, oneShot = false, loop = false, muted = false;
        int end = 0, loopStart = 0, loopEnd = 0, blend = 0, folder = 0, previewFrames = 0;
        enum Stage { attack, decay, sustain, release } stage = attack;
    };

    struct Held { unsigned short mask = 0; unsigned char velocity = 0; bool pending = false; };
    std::array<std::array<std::array<Held, 128>, 16>, 4> held {};
    std::array<Channel, 16> channels {};
    std::array<Voice, maxVoices> voices {};
    std::array<std::uint32_t, maxGroups + 1> roundRobin {};
    std::array<std::atomic<std::uint64_t>, 2> notes {};
    std::array<std::atomic<int>, 16> keyMeters {};
    std::atomic<unsigned> active { 0 };
    const Bank* bank = nullptr; // Borrowed only during the current render call.
    std::uint64_t age = 0, revision = 0;
    double outputRate = 44100, vibratoPhase = 0;
    float instrumentGain = 0;
    Settings settings;

    const Group* groupFor (int id) const noexcept;
    bool audible (int id) const noexcept;
    bool groupMatches (const Group&, int channel) const noexcept;
    bool controlNote (int channel, int note, bool on) noexcept;
    void releaseSamples (int source, int channel, int note) noexcept;
    void playZones (int source, int channel, int note, int velocity, bool releasing, unsigned short mask) noexcept;
    void start (int source, int channel, int note, int velocity) noexcept;
    void stop (int source, int channel, int note) noexcept;
    void release (Voice&) noexcept;
    void trigger (const Zone&, int source, int channel, int note, int velocity, bool releasing = false, int first = -1, int end = -1, bool direct = false) noexcept;
};
}
