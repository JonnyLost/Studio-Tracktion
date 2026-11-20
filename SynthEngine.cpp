// SPDX-License-Identifier: GPL-3.0-or-later
#include "SynthEngine.h"
#include "StudioAnalogDSP.h"
#include "StudioFMDSP.h"
#include "StudioWavetableDSP.h"
#include "StudioAdditiveDSP.h"
#include "StudioGranularDSP.h"
#include "StudioPhysicalDSP.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace studio::synth
{
class Native
{
public:
    virtual ~Native() = default;
    virtual bool ready() const = 0;
    virtual bool set (int id, float value) noexcept = 0;
    virtual bool midi (int status, int a, int b) noexcept = 0;
    virtual void reset() noexcept = 0;
    virtual void render (float*, float*, int) noexcept = 0;
};

namespace
{
template <auto Create, auto Destroy, auto Set, auto Midi, auto Reset, auto Render>
class VoiceEngine final : public Native
{
public:
    explicit VoiceEngine (double rate) : handle (Create (rate, voicesPerLayer)) {}
    ~VoiceEngine() override { Destroy (handle); }
    bool ready() const override { return handle != nullptr; }
    bool set (int id, float value) noexcept override { return Set (handle, id, value) != 0; }
    bool midi (int status, int a, int b) noexcept override
    {
        return Midi (handle, static_cast<uint8_t> (status), static_cast<uint8_t> (a), static_cast<uint8_t> (b), 0) != 0;
    }
    void reset() noexcept override { Reset (handle); }
    void render (float* l, float* r, int n) noexcept override { Render (handle, l, r, n); }
    decltype (Create (1.0, 1)) handle;
};
using Analog = VoiceEngine<sa_create, sa_destroy, sa_set_parameter, sa_midi_event, sa_reset_midi_stopped, sa_render>;
using FM = VoiceEngine<sfm_create, sfm_destroy, sfm_set_parameter, sfm_midi_event, sfm_reset_stopped, sfm_render>;
using Table = VoiceEngine<sws_create, sws_destroy, sws_set_parameter, sws_midi_event, sws_reset_stopped, sws_render>;
using Additive = VoiceEngine<sadd_create, sadd_destroy, sadd_set_parameter, sadd_midi_event, sadd_reset_stopped, sadd_render>;
using Granular = VoiceEngine<sgr_create, sgr_destroy, sgr_set_parameter, sgr_midi_event, sgr_reset_stopped, sgr_render>;
using Physical = VoiceEngine<spm_create, spm_destroy, spm_set_parameter, spm_midi_event, spm_reset_stopped, spm_render>;

std::unique_ptr<Native> prepare (const Layer& layer, double rate)
{
    switch (layer.family)
    {
        case Family::analog:
        {
            auto synth = std::make_unique<Analog> (rate);
            if (synth->ready() && layer.wireCount >= 0)
            {
                std::array<SARoute, 24> wires {};
                for (int i = 0; i < layer.wireCount; ++i)
                {
                    const auto& w = layer.wires[i];
                    wires[i] = { static_cast<uint8_t> (w.source), static_cast<uint8_t> (w.destination),
                                 static_cast<uint8_t> (w.input), w.gain };
                }
                if (sa_submit_patch (synth->handle, wires.data(), static_cast<std::size_t> (layer.wireCount)) != 1)
                    return {};
            }
            return synth;
        }
        case Family::fm: return std::make_unique<FM> (rate);
        case Family::wavetable: return std::make_unique<Table> (rate);
        case Family::additive: return std::make_unique<Additive> (rate);
        case Family::physical: return std::make_unique<Physical> (rate);
        case Family::granular:
        {
            auto synth = std::make_unique<Granular> (rate);
            if (! synth->ready()) return {};
            const auto accepted = layer.pcm ? sgr_load_shared_pcm_stopped (synth->handle, layer.pcm.get())
                                            : sgr_select_factory_source_stopped (synth->handle, layer.source);
            return accepted != 0 ? std::move (synth) : nullptr;
        }
        case Family::hybrid: return {};
    }
    return {};
}
}

float Destination::position (float v) const noexcept
{
    return logarithmic && minimum > 0 ? std::log (v / minimum) / std::log (maximum / minimum)
                                     : (v - minimum) / (maximum - minimum);
}
float Destination::value (float t) const noexcept
{
    t = std::clamp (t, 0.0f, 1.0f);
    return logarithmic && minimum > 0 ? minimum * std::pow (maximum / minimum, t) : std::lerp (minimum, maximum, t);
}

Engine::Engine (const Patch& initial, double sampleRate) : patch (&initial), rate (sampleRate)
{
    for (int i = 0; i < initial.count; ++i)
    {
        const auto& layer = initial.layers[i];
        if (! layer.enabled) continue;
        slots[i] = prepare (layer, rate);
        if (! slots[i] || ! slots[i]->ready())
        {
            good = false;
            break;
        }
        for (int id = 0; id < parameterCounts[static_cast<int> (layer.family)]; ++id)
            if (! slots[i]->set (id, layer.values[id])) good = false;
    }
    for (auto& values : last) values.fill (std::numeric_limits<float>::quiet_NaN());
    // Construction has no queued notes to clear. Analog wiring is awaiting its first render.
    for (auto& values : learned) values.fill (-1);
    expression.fill (1);
}
Engine::~Engine() = default;

unsigned Engine::layersPlaying() const
{
    return static_cast<unsigned> (std::count_if (slots.begin(), slots.end(), [] (const auto& s) { return bool (s); }));
}
void Engine::settings (const Patch& next) noexcept { patch = &next; }

void Engine::clear() noexcept
{
    for (auto& s : slots) if (s) s->reset();
    keys = {};
    held = {};
    pedal.fill (false);
    pressure.fill (0);
    wheel.fill (0);
    expression.fill (1);
    for (auto& values : learned) values.fill (-1);
    learnedMacro = -1;
    expressiveGain = 1;
    cycle = {};
    envelope = 0;
    stage = Stage::silent;
}
void Engine::updateGate() noexcept
{
    if (std::ranges::any_of (held, [] (const auto& notes) { return notes.any(); }))
    {
        if (stage == Stage::silent || stage == Stage::release) stage = Stage::attack;
    }
    else if (stage != Stage::release && stage != Stage::silent)
    {
        stage = Stage::release;
        releaseRate = envelope / patch->release;
    }
}

void Engine::midi (int status, int a, int b) noexcept
{
    const auto channel = status & 15;
    if (patch->receive != 0 && patch->receive != channel + 1) return;
    auto type = status & 240;
    if (type == 0x90 || type == 0x80)
    {
        const bool on = type == 0x90 && b != 0;
        keys[channel].set (a, on);
        if (on || ! pedal[channel]) held[channel].set (a, on);
        updateGate();
    }
    if (type == 0xd0)
    {
        pressure[channel] = a;
        a = 1;
        b = std::max (wheel[channel], pressure[channel]);
        type = 0xb0;
    }
    else if (type == 0xb0)
    {
        for (int i = 0; i < patch->mappingCount; ++i)
        {
            const auto& m = patch->mappings[i];
            if (m.controller != a || (m.channel >= 0 && m.channel != channel)) continue;
            const float normalized = std::lerp (m.minimum, m.maximum, b / 127.0f);
            if (m.target.layer < 0) learnedMacro = normalized;
            else learned[m.target.layer][m.target.parameter] = normalized;
        }
        switch (a)
        {
            case 1: wheel[channel] = b; b = std::max (b, pressure[channel]); break;
            case 11: expression[channel] = b / 127.0f; break;
            case 64:
                pedal[channel] = b >= 64;
                if (! pedal[channel]) held[channel] = keys[channel];
                updateGate();
                break;
            case 120: case 123:
                keys[channel].reset();
                held[channel].reset();
                pedal[channel] = false;
                updateGate();
                break;
            default: break;
        }
    }
    if (type != 0x80 && type != 0x90 && type != 0xe0
        && (type != 0xb0 || (a != 1 && a != 2 && a != 11 && a != 64 && a != 120 && a != 123))) return;
    for (int i = 0; i < patch->count; ++i)
        if (slots[i])
        {
            const bool physical = patch->layers[i].family == Family::physical;
            if (type == 0xb0 && (a == 2 || a == 11) && ! physical) continue;
            if (! slots[i]->midi (type | channel, a, b))
            {
                clear(); // A full native command queue must not leave stuck notes.
                return;
            }
        }
}

void Engine::updateControls (int frames, float macroOffset, double bpm) noexcept
{
    const auto dt = static_cast<float> (frames / rate);
    switch (stage)
    {
        case Stage::attack:
            envelope = std::min (1.0f, envelope + dt / patch->attack);
            if (envelope == 1) stage = Stage::decay;
            break;
        case Stage::decay:
            envelope = std::max (patch->sustain, envelope - dt * (1 - patch->sustain) / patch->decay);
            if (envelope <= patch->sustain) stage = Stage::sustain;
            break;
        case Stage::sustain: envelope = patch->sustain; break;
        case Stage::release:
            envelope = std::max (0.0f, envelope - releaseRate * dt);
            if (envelope == 0) stage = Stage::silent;
            break;
        case Stage::silent: break;
    }

    std::array<std::array<float, maxParameters>, maxLayers> offsets {};
    const float macro = std::clamp ((learnedMacro >= 0 ? learnedMacro : patch->macro) + macroOffset, 0.0f, 1.0f);
    const auto lfo = [&] (int i)
    {
        const auto phase = cycle[i] + patch->lfoPhase[i];
        return static_cast<float> (i == 0 ? std::sin (2 * std::numbers::pi * phase)
                                         : 1 - 4 * std::abs ((phase - std::floor (phase)) - .5));
    };
    const auto sources = std::array { lfo (0), lfo (1), envelope,
        static_cast<float> (*std::ranges::max_element (wheel)) / 127, macro };
    for (int i = 0; i < 2; ++i)
    {
        const auto hz = patch->lfoBeats[i] > 0 ? bpm / (60 * patch->lfoBeats[i]) : patch->lfoRate[i];
        cycle[i] = std::fmod (cycle[i] + dt * hz, 1.0);
    }
    for (int i = 0; i < patch->routeCount; ++i)
    {
        const auto& route = patch->routes[i];
        offsets[route.target.layer][route.target.parameter] += sources[static_cast<int> (route.source)] * route.depth;
    }
    std::array<std::array<float, maxParameters>, maxLayers> values {};
    for (int i = 0; i < patch->count; ++i) values[i] = patch->layers[i].values;
    const auto apply = [&] (const Destination& target)
    {
        if (target.layer < 0) return;
        const auto& base = patch->layers[target.layer].values[target.parameter];
        const auto override = learned[target.layer][target.parameter];
        const auto value = target.value ((override >= 0 ? override : target.position (base)) + offsets[target.layer][target.parameter]);
        values[target.layer][target.parameter] = value;
    };
    for (int i = 0; i < patch->routeCount; ++i) apply (patch->routes[i].target);
    for (int i = 0; i < patch->mappingCount; ++i) apply (patch->mappings[i].target);
    for (int i = 0; i < patch->count; ++i)
        if (auto& slot = slots[i])
            for (int id = 0; id < parameterCounts[static_cast<int> (patch->layers[i].family)]; ++id)
                if (last[i][id] != values[i][id] && slot->set (id, values[i][id]))
                    last[i][id] = values[i][id];
}

void Engine::render (float* left, float* right, int frames, float level, float macroOffset, double bpm) noexcept
{
    constexpr int block = 256;
    std::array<float, block> dryL, dryR;
    while (frames > 0)
    {
        const int n = std::min (frames, block);
        updateControls (n, macroOffset, bpm);
        float channelExpression = 1;
        if (patch->receive > 0)
            channelExpression = expression[patch->receive - 1];
        else
        {
            float sum = 0;
            int count = 0;
            for (int i = 0; i < 16; ++i)
                if (held[i].any())
                {
                    sum += expression[i];
                    ++count;
                }
            if (count > 0) channelExpression = sum / count;
            else channelExpression = expressiveGain;
        }
        expressiveGain = channelExpression;
        for (int i = 0; i < patch->count; ++i)
            if (slots[i])
            {
                const auto& layer = patch->layers[i];
                slots[i]->render (dryL.data(), dryR.data(), n);
                const auto gain = patch->gain * level * layer.gain
                    * (layer.family == Family::physical ? 1 : channelExpression);
                const auto l = gain * (1 - std::max (0.0f, layer.pan));
                const auto r = gain * (1 + std::min (0.0f, layer.pan));
                for (int j = 0; j < n; ++j)
                    if (right != nullptr)
                    {
                        left[j] += l * dryL[j];
                        right[j] += r * dryR[j];
                    }
                    else
                        left[j] += 0.5f * (l * dryL[j] + r * dryR[j]);
            }
        frames -= n;
        left += n;
        if (right != nullptr) right += n;
    }
}
}
