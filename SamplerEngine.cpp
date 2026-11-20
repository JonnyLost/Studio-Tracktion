// SPDX-License-Identifier: GPL-3.0-or-later
#include "SamplerEngine.h"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace studio::sampler
{
namespace
{
float amplitude (float db) { return std::pow (10.0f, db / 20.0f); }
double pitchRatio (double semitones) { return std::exp2 (semitones / 12.0); }

float interpolate (const std::vector<float>& samples, int first, int next, float fraction)
{
    return std::lerp (samples[static_cast<std::size_t> (first)], samples[static_cast<std::size_t> (next)], fraction);
}
}

bool validMapping (const Zone& z) noexcept
{
    return z.root >= 0 && z.root <= 127 && z.low >= 0 && z.low <= z.high && z.high <= 127
        && z.soft >= 1 && z.soft <= z.hard && z.hard <= 127 && z.group >= 0 && z.group <= maxGroups;
}

bool validLoop (const Zone& z, int frames) noexcept
{
    return z.loopStart >= 0 && z.loopEnd >= 2 && z.loopEnd <= frames && z.loopStart <= z.loopEnd - 2;
}

void Engine::prepare (double rate) noexcept
{
    outputRate = rate > 0 ? rate : 44100;
    revision = 0;
    panic();
    publishMeters();
}

void Engine::use (const Bank& next) noexcept
{
    if (revision != next.sampleRevision)
    {
        for (auto& voice : voices) voice = {};
        roundRobin.fill (0);
        revision = next.sampleRevision;
    }
    bank = &next;
    settings = next.settings;
    instrumentGain = amplitude (settings.gainDb);
    for (int i = settings.polyphony; i < maxVoices; ++i)
        voices[static_cast<std::size_t> (i)] = {};
}

void Engine::panic() noexcept
{
    voices.fill ({});
    channels.fill ({});
    roundRobin.fill (0);
    vibratoPhase = 0;
}

void Engine::release (Voice& voice) noexcept
{
    if (voice.stage == Voice::release) return;
    voice.stage = Voice::release;
    voice.releaseStep = static_cast<float> (voice.level / std::max (1.0, settings.release * outputRate));
}

void Engine::trigger (const Zone& zone, int source, int channel, int note, int velocity) noexcept
{
    Voice* voice = nullptr;
    for (int i = 0; i < settings.polyphony; ++i)
    {
        auto& candidate = voices[static_cast<std::size_t> (i)];
        if (candidate.sample == nullptr)
        {
            voice = &candidate;
            break;
        }
        if (voice == nullptr || (candidate.stage == Voice::release && voice->stage != Voice::release)
            || ((candidate.stage == Voice::release) == (voice->stage == Voice::release) && candidate.age < voice->age))
            voice = &candidate;
    }

    *voice = {};
    voice->sample = zone.sample.get();
    voice->channel = channel;
    voice->note = note;
    voice->source = source;
    voice->down = true;
    voice->age = ++age;
    voice->velocityGain = amplitude (zone.gainDb) * static_cast<float> (velocity) / 127;
    voice->baseStep = zone.sample->rate / outputRate * pitchRatio (note - zone.root + zone.cents * 0.01);
    voice->oneShot = zone.oneShot;
    voice->loop = ! zone.oneShot && zone.loop && validLoop (zone, static_cast<int> (zone.sample->left.size()));
    voice->loopStart = zone.loopStart;
    voice->loopEnd = zone.loopEnd;
    if (source != 0) voice->previewFrames = static_cast<int> (outputRate * 0.045);
}

void Engine::start (int source, int channel, int note, int velocity) noexcept
{
    // Retriggering a pitch releases the previous sustained strike on that source.
    for (auto& voice : voices)
        if (voice.sample && voice.source == source && voice.channel == channel && voice.note == note && ! voice.oneShot)
        {
            voice.down = false;
            release (voice);
        }

    std::array<unsigned, maxGroups + 1> count {}, index {};
    const auto matches = [=] (const Zone& z)
    {
        return z.sample && ! z.sample->left.empty() && validMapping (z)
            && note >= z.low && note <= z.high && velocity >= z.soft && velocity <= z.hard;
    };

    for (const auto& z : bank->zones)
        if (matches (z) && z.group != 0)
            ++count[static_cast<std::size_t> (z.group)];

    for (const auto& z : bank->zones)
        if (matches (z))
        {
            auto group = static_cast<std::size_t> (z.group);
            if (group == 0 || index[group]++ == roundRobin[group] % count[group])
                trigger (z, source, channel, note, velocity);
        }

    for (std::size_t group = 1; group < roundRobin.size(); ++group)
        if (count[group] != 0)
            ++roundRobin[group];
}

void Engine::stop (int source, int channel, int note) noexcept
{
    for (auto& voice : voices)
        if (voice.sample && voice.source == source && voice.channel == channel && voice.note == note && ! voice.oneShot)
        {
            voice.down = false;
            if (voice.previewFrames <= 0 && (source != 0 || ! channels[static_cast<std::size_t> (channel)].pedal))
                release (voice);
        }
}

void Engine::preview (int source, int channel, int note, int velocity, int zone) noexcept
{
    if (note < 0)
    {
        for (auto& voice : voices)
            if (voice.source == source)
                voice = {};
        return;
    }
    if (velocity == 0)
    {
        stop (source, channel, note);
        return;
    }
    if (zone < 0)
        start (source, channel, note, velocity);
    else if (static_cast<std::size_t> (zone) < bank->zones.size())
    {
        const auto& selected = bank->zones[static_cast<std::size_t> (zone)];
        if (selected.sample)
            trigger (selected, source, channel, note, velocity);
    }
}

void Engine::midi (int status, int data1, int data2) noexcept
{
    const int channel = status & 15;
    auto& state = channels[static_cast<std::size_t> (channel)];
    switch (status & 0xf0)
    {
        case 0x80: stop (0, channel, data1); break;
        case 0x90:
            if (data2 != 0) start (0, channel, data1, data2);
            else stop (0, channel, data1);
            break;
        case 0xd0: state.pressure = static_cast<float> (data1) / 127.0f; break;
        case 0xe0: state.bend = (static_cast<float> (data1 | (data2 << 7)) - 8192) / 8192; break;
        case 0xb0:
        {
            if (data1 == 1) state.modulation = static_cast<float> (data2) / 127.0f;
            if (data1 == 7) state.volume = static_cast<float> (data2) / 127.0f;
            if (data1 == 11) state.expression = static_cast<float> (data2) / 127.0f;
            if (data1 == 64) state.pedal = data2 >= 64;
            if (data1 == 121) state = {};
            if (data1 == 64 || data1 >= 120)
                for (auto& voice : voices)
                {
                    if (! voice.sample || voice.channel != channel || voice.source != 0) continue;
                    if (data1 == 120) voice = {};
                    if (data1 == 123)
                    {
                        voice.oneShot = false;
                        voice.down = false;
                        release (voice);
                    }
                    if ((data1 == 64 || data1 == 121) && ! state.pedal && ! voice.down && ! voice.oneShot)
                        release (voice);
                }
            break;
        }
        default: break;
    }
}

void Engine::render (float* left, float* right, int frames) noexcept
{
    const auto attackStep = static_cast<float> (1 / std::max (1.0, settings.attack * outputRate));
    const auto decayStep = static_cast<float> ((1 - settings.sustain) / std::max (1.0, settings.decay * outputRate));
    const auto phaseStep = 2 * std::numbers::pi * 5 / outputRate;
    const auto basePhase = vibratoPhase;
    for (auto& v : voices)
    {
        if (v.sample == nullptr) continue;
        const Channel previewChannel;
        const auto& channel = v.source == 0 ? channels[static_cast<std::size_t> (v.channel)] : previewChannel;
        const double step = v.baseStep * pitchRatio (settings.semitones + channel.bend * settings.bendRange);
        const float vibrato = 0.5f * std::max (channel.modulation, channel.pressure);
        const float gain = v.velocityGain * instrumentGain * channel.volume * channel.expression;
        const int length = static_cast<int> (v.sample->left.size());
        const int blend = std::min (64, (v.loopEnd - v.loopStart) / 4);
        auto phase = basePhase;

        for (int i = 0; i < frames; ++i, phase += phaseStep)
        {
            if (v.previewFrames > 0 && --v.previewFrames == 0 && ! v.down && ! v.oneShot)
                release (v);

            switch (v.stage)
            {
                case Voice::attack:
                    v.level = std::min (1.0f, v.level + attackStep);
                    if (v.level >= 1) v.stage = Voice::decay;
                    break;
                case Voice::decay:
                    v.level = std::max (settings.sustain, v.level - decayStep);
                    if (v.level <= settings.sustain) v.stage = Voice::sustain;
                    break;
                case Voice::sustain: v.level = settings.sustain; break;
                case Voice::release: v.level = std::max (0.0f, v.level - v.releaseStep); break;
            }

            if (v.position >= length || (v.stage == Voice::release && v.level <= 0))
            {
                v = {};
                break;
            }

            const int first = static_cast<int> (v.position);
            const int next = v.loop && first + 1 == v.loopEnd && blend == 0 ? v.loopStart : std::min (first + 1, length - 1);
            const auto fraction = static_cast<float> (v.position - first);
            const auto input = [&] (const std::vector<float>& samples)
            {
                const float x = interpolate (samples, first, next, fraction);
                if (! v.loop || blend <= 0 || first < v.loopEnd - blend) return x;
                const int head = v.loopStart + first - (v.loopEnd - blend);
                const auto y = interpolate (samples, head, head + 1, fraction);
                return std::lerp (x, y, (static_cast<float> (first - (v.loopEnd - blend)) + fraction) / static_cast<float> (blend));
            };
            const double advance = vibrato == 0 ? step : step * pitchRatio (vibrato * std::sin (phase));
            const auto endFade = v.loop ? 1.0 : std::min (1.0, (length - v.position) / (32 * advance));
            const auto level = v.level * gain * static_cast<float> (endFade);
            const float l = input (v.sample->left) * level;
            const float r = v.sample->right.empty() ? l : input (v.sample->right) * level;
            left[i] += right != nullptr ? l : (l + r) * 0.5f;
            if (right != nullptr) right[i] += r;

            v.position += advance;
            if (v.loop && v.position >= v.loopEnd)
            {
                const auto firstFrame = v.loopStart + blend;
                v.position = firstFrame + std::fmod (v.position - v.loopEnd, v.loopEnd - firstFrame);
            }
        }
    }
    vibratoPhase = std::fmod (basePhase + frames * phaseStep, 2 * std::numbers::pi);
}

void Engine::publishMeters() noexcept
{
    std::array<std::uint64_t, 2> keys {};
    unsigned count = 0;
    for (const auto& v : voices)
        if (v.sample != nullptr)
        {
            ++count;
            if (v.down) keys[static_cast<std::size_t> (v.note / 64)] |= std::uint64_t { 1 } << (v.note % 64);
        }

    for (std::size_t i = 0; i < notes.size(); ++i) notes[i].store (keys[i], std::memory_order_relaxed);
    active.store (count, std::memory_order_relaxed);
    bank = nullptr;
}

bool Engine::noteActive (int note) const noexcept
{
    return (notes[static_cast<std::size_t> (note / 64)].load (std::memory_order_relaxed)
        & (std::uint64_t { 1 } << (note % 64))) != 0;
}
}
