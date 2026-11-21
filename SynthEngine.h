// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "StudioSharedPCM.h"
#include <array>
#include <bitset>
#include <cstdint>
#include <memory>

namespace studio::synth
{
enum class Family { analog, fm, wavetable, additive, granular, physical, hybrid };
constexpr int maxLayers = 4, voicesPerLayer = 8, maxParameters = 110, maxRoutes = 24, maxMappings = 32;
constexpr std::array parameterCounts { 25, 32, 31, 110, 13, 21 };
struct AnalogWire { int source, destination, input; float gain; };
struct Layer
{
    Family family = Family::analog;
    bool enabled = true;
    float gain = .75f, pan = 0;
    std::array<float, maxParameters> values {};
    int source = 0, wireCount = -1, voices = voicesPerLayer;
    std::array<AnalogWire, 24> wires {};
    std::shared_ptr<SSSharedPCM> pcm;
};

enum class Source { lfo1, lfo2, envelope, wheel, macro };
struct Destination
{
    int layer = -1, parameter = -1;
    float minimum = 0, maximum = 1;
    bool logarithmic = false;
    float position (float value) const noexcept;
    float value (float position) const noexcept;
};
struct Route { Source source; Destination target; float depth; };
struct Mapping { int controller, channel; Destination target; float minimum, maximum; };

struct Patch
{
    std::array<Layer, maxLayers> layers {};
    int count = 0, receive = 0; // Zero receives all channels. Other values are MIDI channels 1–16.
    float gain = .8f, macro = 0;
    std::array<float, 2> lfoRate { .45f, .18f }, lfoPhase {}, lfoBeats {};
    float attack = .08f, decay = .3f, sustain = .7f, release = .5f;
    std::array<Route, maxRoutes> routes {};
    std::array<Mapping, maxMappings> mappings {};
    int routeCount = 0, mappingCount = 0;
};

class Native; // Original DSP via its C ABI.
class Engine
{
public:
    Engine (const Patch&, double sampleRate);
    ~Engine();
    Engine (const Engine&) = delete;
    Engine& operator= (const Engine&) = delete;
    bool valid() const { return good; }
    unsigned layersPlaying() const;
    void settings (const Patch&) noexcept;
    void clear() noexcept;
    void midi (int status, int first, int second) noexcept;
    void render (float* left, float* right, int frames, float level = 1, float macroOffset = 0, double bpm = 120) noexcept;
private:
    std::array<std::unique_ptr<Native>, maxLayers> slots;
    const Patch* patch = nullptr;
    std::array<std::array<float, maxParameters>, maxLayers> learned {};
    std::array<std::array<float, maxParameters>, maxLayers> last {};
    std::array<std::bitset<128>, 16> keys, held;
    std::array<bool, 16> pedal {};
    std::array<int, 16> wheel {}, pressure {};
    std::array<float, 16> expression {};
    float expressiveGain = 1, learnedMacro = -1, envelope = 0, releaseRate = 0;
    enum class Stage { silent, attack, decay, sustain, release };
    Stage stage = Stage::silent;
    std::array<double, 2> cycle {};
    double rate;
    bool good = true;
    void updateGate() noexcept;
    void updateControls (int frames, float macro, double bpm) noexcept;
};
}
