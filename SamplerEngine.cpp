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

bool validGroup (const Group& g) noexcept
{
    return g.id >= 0 && g.id < maxGroups && g.key >= 0 && g.key <= 127
        && g.controller >= 0 && g.controller < 120 && g.ccLow >= 0 && g.ccHigh <= 127 && g.ccLow <= g.ccHigh
        && g.channel >= 0 && g.channel <= 16 && static_cast<int> (g.trigger) >= 0 && static_cast<int> (g.trigger) <= 2
        && g.switchDefault >= -2 && g.switchDefault <= 127;
}
float velocityWeight (const Zone& z, int v) noexcept
{
    if (v < z.soft || v > z.hard) return 0;
    if (z.sfz.enabled)
    {
        const auto ramp = [] (int n, int lo, int hi)
        { return hi == lo ? (n >= hi ? 1.0f : 0.0f) : std::clamp (float (n - lo) / (hi - lo), 0.0f, 1.0f); };
        const float in = z.sfz.fadeInHigh > z.sfz.fadeInLow ? ramp (v, z.sfz.fadeInLow, z.sfz.fadeInHigh) : 1.0f;
        const float out = z.sfz.fadeOutHigh > z.sfz.fadeOutLow ? 1.0f - ramp (v, z.sfz.fadeOutLow, z.sfz.fadeOutHigh) : 1.0f;
        const float weight = in * out;
        return z.sfz.powerFade ? std::sqrt (weight) : weight;
    }
    float weight = 1;
    if (z.fadeIn > 0) weight = std::min (weight, float (v - z.soft + 1) / (z.fadeIn + 1));
    if (z.fadeOut > 0) weight = std::min (weight, float (z.hard - v + 1) / (z.fadeOut + 1));
    return weight;
}
const Group* Engine::groupFor (int id) const noexcept
{
    for (const auto& g : bank->groups) if (g.id == id) return &g;
    return nullptr;
}
bool Engine::audible (int id) const noexcept
{
    bool solo = false;
    for (const auto& g : bank->groups) solo |= g.solo;
    const auto* g = groupFor (id);
    return g ? ! g->mute && (! solo || g->solo) : ! solo;
}
bool Engine::groupMatches (const Group& g, int channel) const noexcept
{
    if (g.channel && g.channel != channel + 1) return false;
    const auto& c = channels[static_cast<std::size_t> (channel)];
    if (g.trigger == GroupTrigger::keyswitch)
    {
        int key = c.selectedKey;
        if (key < 0 && g.switchDefault != -2) key = g.switchDefault;
        if (key < 0 && g.switchDefault == -2) for (const auto& first : bank->groups)
            if (first.trigger == GroupTrigger::keyswitch && (! first.channel || first.channel == channel + 1)) { key = first.key; break; }
        return key == g.key;
    }
    return g.trigger != GroupTrigger::controller || (c.cc[static_cast<std::size_t> (g.controller)] >= g.ccLow && c.cc[static_cast<std::size_t> (g.controller)] <= g.ccHigh);
}
bool Engine::controlNote (int channel, int note, bool on) noexcept
{
    for (const auto& g : bank->groups)
        if (g.trigger == GroupTrigger::keyswitch && g.key == note && (! g.channel || g.channel == channel + 1))
        {
            if (on) channels[static_cast<std::size_t> (channel)].selectedKey = note;
            return true;
        }
    return false;
}

bool validMapping (const Zone& z) noexcept
{
    return z.root >= 0 && z.root <= 127 && z.low >= 0 && z.low <= z.high && z.high <= 127
        && z.soft >= 1 && z.soft <= z.hard && z.hard <= 127 && z.group >= 0 && z.group <= maxGroups
        && z.folder >= 0 && z.folder < maxGroups && z.loopCrossfade >= -1 && z.loopCrossfade <= 1000000
        && z.fadeIn >= 0 && z.fadeIn <= 127 && z.fadeOut >= 0 && z.fadeOut <= 127
        && (! z.sfz.enabled || (std::isfinite (z.sfz.attack) && z.sfz.attack >= 0 && z.sfz.attack <= 100
            && std::isfinite (z.sfz.decay) && z.sfz.decay >= 0 && z.sfz.decay <= 100
            && std::isfinite (z.sfz.sustain) && z.sfz.sustain >= 0 && z.sfz.sustain <= 1
            && std::isfinite (z.sfz.release) && z.sfz.release >= 0 && z.sfz.release <= 100
            && std::isfinite (z.sfz.velocityTrack) && z.sfz.velocityTrack >= -100 && z.sfz.velocityTrack <= 100
            && std::isfinite (z.sfz.pan) && z.sfz.pan >= -100 && z.sfz.pan <= 100
            && z.sfz.transpose >= -48 && z.sfz.transpose <= 48 && z.sfz.keyTrack >= 0 && z.sfz.keyTrack <= 200
            && z.sfz.fadeInLow >= 0 && z.sfz.fadeInHigh >= z.sfz.fadeInLow && z.sfz.fadeInHigh <= 127
            && z.sfz.fadeOutLow >= 0 && z.sfz.fadeOutHigh >= z.sfz.fadeOutLow && z.sfz.fadeOutHigh <= 127));
}

bool validLoop (const Zone& z, int frames) noexcept
{
    return z.loopStart >= z.start && z.loopEnd >= 2 && z.loopEnd <= (z.end > 0 ? std::min (z.end, frames) : frames) && z.loopStart <= z.loopEnd - 2;
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
        for (auto& source : held) for (auto& channel : source) channel.fill ({});
        roundRobin.fill (0);
        revision = next.sampleRevision;
    }
    bank = &next;
    settings = next.settings;
    instrumentGain = amplitude (settings.gainDb);
    for (auto& voice : voices) if (voice.sample && controlNote (voice.channel, voice.note, false))
    { voice.down = false; release (voice); held[static_cast<std::size_t> (voice.source)][static_cast<std::size_t> (voice.channel)][static_cast<std::size_t> (voice.note)] = {}; }
    for (auto& voice : voices) if (voice.sample && ! voice.muted && ! audible (voice.folder)) { voice.muted = true; release (voice); voice.releaseStep = voice.level / 32; }
    for (int i = settings.polyphony; i < maxVoices; ++i)
        voices[static_cast<std::size_t> (i)] = {};
}

void Engine::panic() noexcept
{
    for (auto& source : held) for (auto& channel : source) channel.fill ({});
    voices.fill ({});
    channels.fill ({});
    roundRobin.fill (0);
    vibratoPhase = 0;
}

void Engine::release (Voice& voice) noexcept
{
    if (voice.stage == Voice::release) return;
    voice.stage = Voice::release;
    voice.releaseStep = static_cast<float> (voice.level / std::max (1.0, (voice.sfz.enabled ? voice.sfz.release : settings.release) * outputRate));
    if (voice.sfz.enabled && voice.sfz.sustainLoop) voice.loop = false;
}

void Engine::trigger (const Zone& zone, int source, int channel, int note, int velocity, bool releasing, int firstOverride, int endOverride, bool direct) noexcept
{
    if (! zone.sample || zone.sample->frameCount() < 2 || zone.start < 0 || zone.start >= zone.sample->frameCount()
        || zone.end < 0 || (zone.end && (zone.end <= zone.start || zone.end > zone.sample->frameCount()))) return;
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
    const int frames = zone.sample->frameCount();
    voice->position = std::clamp (zone.start, 0, frames - 1);
    voice->end = zone.end > 0 ? std::clamp (zone.end, zone.start + 1, frames) : frames;
    voice->channel = channel;
    voice->note = note;
    voice->source = source;
    voice->down = ! releasing;
    voice->age = ++age;
    voice->sfz = zone.sfz;
    float velocityGain = static_cast<float> (velocity) / 127;
    if (zone.sfz.enabled)
    {
        const float track = zone.sfz.velocityTrack / 100.0f;
        const float scaled = track < 0 ? 1.0f - velocityGain : velocityGain;
        velocityGain = 1.0f - std::abs (track) + std::abs (track) * scaled * scaled;
    }
    voice->velocityGain = amplitude (zone.gainDb) * velocityGain * (direct ? 1.0f : velocityWeight (zone, velocity));
    voice->folder = zone.folder;
    const auto trackedNote = (note - zone.root) * (zone.sfz.enabled ? zone.sfz.keyTrack * 0.01 : 1.0);
    voice->baseStep = zone.sample->rate / outputRate * pitchRatio (trackedNote + zone.cents * 0.01 + (zone.sfz.enabled ? zone.sfz.transpose : 0));
    voice->oneShot = zone.oneShot || releasing;
    voice->loop = ! releasing && ! zone.oneShot && zone.loop && validLoop (zone, zone.sample->frameCount());
    voice->loopStart = zone.loopStart;
    voice->loopEnd = zone.loopEnd;
    voice->blend = std::min (zone.loopCrossfade < 0 ? 64 : zone.loopCrossfade, (zone.loopEnd - zone.loopStart) / 4);
    if (firstOverride >= 0 && endOverride > firstOverride && endOverride <= frames)
    { voice->position = firstOverride; voice->end = endOverride; voice->loop = false; voice->oneShot = true; }
    if (source != 0) voice->previewFrames = static_cast<int> (outputRate * 0.045);
}

void Engine::start (int source, int channel, int note, int velocity) noexcept
{
    if (controlNote (channel, note, true)) return;
    // Retriggering a pitch releases the previous sustained strike on that source.
    for (auto& voice : voices)
        if (voice.sample && voice.source == source && voice.channel == channel && voice.note == note && ! voice.oneShot)
        {
            voice.down = false;
            release (voice);
        }

    auto& strike = held[static_cast<std::size_t> (source)][static_cast<std::size_t> (channel)][static_cast<std::size_t> (note)];
    strike = {}; strike.velocity = static_cast<unsigned char> (velocity);
    for (const auto& g : bank->groups)
        if (g.releaseTrigger && groupMatches (g, channel) && audible (g.id)) strike.mask |= static_cast<unsigned short> (1u << g.id);
    playZones (source, channel, note, velocity, false, strike.mask);
}

void Engine::playZones (int source, int channel, int note, int velocity, bool releasing, unsigned short mask) noexcept
{
    std::array<unsigned, maxGroups + 1> count {}, index {};
    const auto matches = [=, this] (const Zone& z)
    {
        const auto* g = groupFor (z.folder);
        const bool enabled = g ? (releasing ? (mask & (1u << g->id)) != 0 : groupMatches (*g, channel)) : true;
        return enabled && audible (z.folder) && (g && g->releaseTrigger) == releasing
            && z.sample && z.sample->frameCount() >= 2 && validMapping (z)
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
            {
                trigger (z, source, channel, note, velocity, releasing);
            }
        }

    for (std::size_t group = 1; group < roundRobin.size(); ++group)
        if (count[group] != 0)
            ++roundRobin[group];
}

void Engine::releaseSamples (int source, int channel, int note) noexcept
{
    auto& strike = held[static_cast<std::size_t> (source)][static_cast<std::size_t> (channel)][static_cast<std::size_t> (note)];
    if (! strike.velocity) return;
    const auto velocity = strike.velocity; const auto mask = strike.mask;
    strike = {};
    playZones (source, channel, note, velocity, true, mask);
}
void Engine::stop (int source, int channel, int note) noexcept
{
    if (controlNote (channel, note, false)) return;
    auto& strike = held[static_cast<std::size_t> (source)][static_cast<std::size_t> (channel)][static_cast<std::size_t> (note)];
    if (source == 0 && channels[static_cast<std::size_t> (channel)].pedal) strike.pending = true;
    else releaseSamples (source, channel, note);
    for (auto& voice : voices)
        if (voice.sample && voice.source == source && voice.channel == channel && voice.note == note && ! voice.oneShot)
        {
            voice.down = false;
            if (voice.previewFrames <= 0 && (source != 0 || ! channels[static_cast<std::size_t> (channel)].pedal))
                release (voice);
        }
}

void Engine::preview (int source, int channel, int note, int velocity, int zone, int first, int end) noexcept
{
    if (! bank || source < 0 || source > 3 || channel < 0 || channel > 15 || note > 127 || velocity < 0 || velocity > 127) return;
    if (note < 0)
    {
        for (auto& channelHeld : held[static_cast<std::size_t> (source)]) channelHeld.fill ({});
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
        if (selected.sample && audible (selected.folder))
            trigger (selected, source, channel, note, velocity, false, first, end, true);
    }
}

void Engine::midi (int status, int data1, int data2) noexcept
{
    if (! bank || data1 < 0 || data1 > 127 || data2 < 0 || data2 > 127) return;
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
            state.cc[static_cast<std::size_t> (data1)] = static_cast<unsigned char> (data2);
            if (data1 == 1) state.modulation = static_cast<float> (data2) / 127.0f;
            if (data1 == 7) state.volume = static_cast<float> (data2) / 127.0f;
            if (data1 == 11) state.expression = static_cast<float> (data2) / 127.0f;
            if (data1 == 64) state.pedal = data2 >= 64;
            if (data1 == 121) state = {};
            if ((data1 == 64 || data1 == 121) && ! state.pedal)
                for (int n = 0; n < 128; ++n) if (held[0][static_cast<std::size_t> (channel)][static_cast<std::size_t> (n)].pending) releaseSamples (0, channel, n);
            if (data1 == 120 || data1 == 123) held[0][static_cast<std::size_t> (channel)].fill ({});
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

void Engine::render (float* left, float* right, int frames, bool offline) noexcept
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
        const float vibrato = v.sfz.enabled ? 0.0f : 0.5f * std::max (channel.modulation, channel.pressure);
        const float sustainLevel = v.sfz.enabled ? v.sfz.sustain : settings.sustain;
        const auto voiceAttack = v.sfz.enabled ? static_cast<float> (1 / std::max (1.0, v.sfz.attack * outputRate)) : attackStep;
        const auto voiceDecay = v.sfz.enabled ? static_cast<float> ((1 - sustainLevel) / std::max (1.0, v.sfz.decay * outputRate)) : decayStep;
        const float panL = v.sfz.enabled ? 1.0f - std::max (0.0f, v.sfz.pan / 100.0f) : 1.0f;
        const float panR = v.sfz.enabled ? 1.0f + std::min (0.0f, v.sfz.pan / 100.0f) : 1.0f;
        const float gain = v.velocityGain * instrumentGain * channel.volume * channel.expression;
        const int length = v.end;
        const int blend = v.blend;
        auto phase = basePhase;
        auto* stream = v.sample->stream.get();
        StreamCache::Cursor mainCursor, nextCursor, loopCursor, loopNextCursor;
        bool missed = false;
        if (stream && ! offline)
        {
            const auto position = static_cast<int> (v.position);
            stream->requestFrame (position);
            if (position <= v.sample->frameCount() - streamPageFrames - 1)
                stream->requestFrame (position + streamPageFrames);
            if (position <= v.sample->frameCount() - 2 * streamPageFrames - 1)
                stream->requestFrame (position + 2 * streamPageFrames);
            if (v.loop && v.loopEnd - position <= 2 * streamPageFrames)
            {
                stream->requestFrame (v.loopStart);
                if (v.loopStart <= v.sample->frameCount() - streamPageFrames - 1)
                    stream->requestFrame (v.loopStart + streamPageFrames);
            }
        }

        for (int i = 0; i < frames; ++i, phase += phaseStep)
        {
            if (v.previewFrames > 0 && --v.previewFrames == 0 && ! v.down && ! v.oneShot)
                release (v);

            switch (v.stage)
            {
                case Voice::attack:
                    v.level = std::min (1.0f, v.level + voiceAttack);
                    if (v.level >= 1) v.stage = Voice::decay;
                    break;
                case Voice::decay:
                    v.level = std::max (sustainLevel, v.level - voiceDecay);
                    if (v.level <= sustainLevel) v.stage = Voice::sustain;
                    break;
                case Voice::sustain: v.level = sustainLevel; break;
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
            float l = 0, r = 0;
            if (! stream)
            {
                l = input (v.sample->left) * level;
                r = v.sample->right.empty() ? l : input (v.sample->right) * level;
            }
            else
            {
                auto pair = [&] (int a, int b, StreamCache::Cursor& c1, StreamCache::Cursor& c2,
                                 float& outL, float& outR)
                {
                    float al = 0, ar = 0, bl = 0, br = 0;
                    const bool firstOK = v.sample->readFrame (a, c1, al, ar, offline);
                    const bool nextOK = v.sample->readFrame (b, c2, bl, br, offline);
                    if (! firstOK || ! nextOK) return false;
                    outL = std::lerp (al, bl, fraction);
                    outR = std::lerp (ar, br, fraction);
                    return true;
                };
                float x = 0, y = 0;
                bool ready = pair (first, next, mainCursor, nextCursor, x, y);
                if (v.loop && blend > 0 && first >= v.loopEnd - blend)
                {
                    const int head = v.loopStart + first - (v.loopEnd - blend);
                    float hl = 0, hr = 0;
                    const bool headReady = pair (head, head + 1, loopCursor, loopNextCursor, hl, hr);
                    ready = ready && headReady;
                    const auto mix = (static_cast<float> (first - (v.loopEnd - blend)) + fraction) / static_cast<float> (blend);
                    x = std::lerp (x, hl, mix);
                    y = std::lerp (y, hr, mix);
                }
                if (ready)
                {
                    v.lastLeft = x; v.lastRight = y;
                    v.streamGain = std::min (1.0f, v.streamGain + 1.0f / 32);
                }
                else
                {
                    missed = true;
                    v.streamGain = std::max (0.0f, v.streamGain - 1.0f / 32);
                }
                l = v.lastLeft * level * v.streamGain;
                r = v.lastRight * level * v.streamGain;
            }
            l *= panL; r *= panR;
            left[i] += right != nullptr ? l : (l + r) * 0.5f;
            if (right != nullptr) right[i] += r;

            v.position += advance;
            if (v.loop && v.position >= v.loopEnd)
            {
                const auto firstFrame = v.loopStart + blend;
                v.position = firstFrame + std::fmod (v.position - v.loopEnd, v.loopEnd - firstFrame);
            }
        }
        if (stream && missed) stream->recordMiss (offline);
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
    for (std::size_t i = 0; i < channels.size(); ++i) keyMeters[i].store (channels[i].selectedKey, std::memory_order_relaxed);
    active.store (count, std::memory_order_relaxed);
    bank = nullptr;
}

bool Engine::noteActive (int note) const noexcept
{
    return (notes[static_cast<std::size_t> (note / 64)].load (std::memory_order_relaxed)
        & (std::uint64_t { 1 } << (note % 64))) != 0;
}
}
