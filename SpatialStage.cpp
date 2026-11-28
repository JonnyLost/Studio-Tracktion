// SPDX-License-Identifier: GPL-3.0-or-later
#include "SpatialStage.h"

namespace studio::spatial::stage
{
namespace
{
float clean (float x) noexcept
{
    return std::isfinite (x) && std::abs (x) >= 1e-24f ? std::clamp (x, -64.f, 64.f) : 0.f;
}

float filtered (float& state, float x, float pole, float high) noexcept
{
    state += pole * (x - state);
    if (std::abs (state) < 1.e-24f) state = 0;
    return state + high * (x - state);
}
}

bool Renderer::prepare (double rate, const Plan& initial)
{
    ready = false;
    if (! source::supportedRate (rate) || ! direct.prepare (rate) || ! direct.setPlan (initial.direct)
        || ! reflections.prepare (rate, initial.earlyField)) return false;
    const auto size = std::bit_ceil (size_t (std::ceil (rate * microphoneHistorySeconds)) + 2);
    mid.assign (size, 0.f);
    side.assign (size, 0.f);
    mask = size - 1;
    fadeLength = int (std::round (rate * .04));
    smoothing = source::coefficient (rate, 1. / (2 * early::pi * .02));
    historyFrames = int (size);
    setPlan (initial);
    reset();
    return ready = true;
}

void Renderer::setPlan (const Plan& plan) noexcept
{
    direct.setPlan (plan.direct);
    reflections.setPlan (plan.earlyField);
    if (requested != plan.microphones)
    {
        requested = plan.microphones;
        pending = true;
    }
    if (silentFrames >= historyFrames)
    {
        current = next = Bank { requested };
        pending = false;
        fade = 0;
    }
    targetBypass = plan.bypass;
    targetPerspective = plan.microphoneMix;
}

void Renderer::reset() noexcept
{
    direct.reset();
    reflections.reset();
    std::fill (mid.begin(), mid.end(), 0.f);
    std::fill (side.begin(), side.end(), 0.f);
    cursor = 0;
    silentFrames = historyFrames;
    fade = 0;
    pending = false;
    current = next = Bank { requested };
    bypass = targetBypass ? 1.f : 0.f;
    perspective = targetPerspective ? 1.f : 0.f;
}

size_t Renderer::allocatedBytes() const noexcept
{
    return (mid.capacity() + side.capacity()) * sizeof (float) + reflections.allocatedBytes();
}

float Renderer::read (const std::vector<float>& history, float delay) const noexcept
{
    const auto whole = size_t (delay);
    const auto pos = cursor - whole;
    const auto previous = history[pos & mask];
    return previous + (delay - float (whole)) * (history[(pos - 1) & mask] - previous);
}

std::array<float, 2> Renderer::render (Bank& bank) noexcept
{
    std::array<float, 2> out {};
    for (int i = 0; i < bank.plan.count; ++i)
    {
        const auto index = (size_t) i;
        const auto& capsule = bank.plan.capsules[index];
        if (capsule.left == 0 && capsule.right == 0) continue;
        auto& state = bank.filters[index];
        const auto& tap = capsule.direct;
        float signal = filtered (state.direct, read (mid, tap.delay), tap.pole, tap.high) * tap.gain;
        const float stereo = filtered (state.side, read (side, tap.delay), tap.pole, tap.high) * tap.gain * bank.plan.side;
        for (size_t k = 0; k < capsule.reflections.size(); ++k)
        {
            const auto& ray = capsule.reflections[k];
            if (ray.gain != 0)
                signal += filtered (state.reflections[k], read (mid, ray.delay), ray.pole, ray.high) * ray.gain;
        }
        out[0] += (signal + stereo) * capsule.left;
        out[1] += (signal - stereo) * capsule.right;
        peaks[index] = std::max (peaks[index], std::abs (signal) * std::max (capsule.left, capsule.right));
    }
    return out;
}

std::array<float, microphoneLimit> Renderer::process (float* left, float* right, int frames) noexcept
{
    peaks = {};
    if (! ready || left == nullptr || frames <= 0) return peaks;
    if (silentFrames >= historyFrames)
    {
        bool active = false;
        for (int i = 0; i < frames && ! active; ++i)
            active = clean (left[i]) != 0 || (right != nullptr && clean (right[i]) != 0);
        if (! active)
        {
            std::fill_n (left, frames, 0.f);
            if (right != nullptr) std::fill_n (right, frames, 0.f);
            return peaks;
        }
    }
    for (int i = 0; i < frames; ++i)
    {
        const auto dryL = clean (left[i]), dryR = right != nullptr ? clean (right[i]) : dryL;
        silentFrames = dryL != 0 || dryR != 0 ? 0 : std::min (silentFrames + 1, historyFrames);
        const float mono = .5f * (dryL + dryR);
        mid[cursor] = mono;
        side[cursor] = .5f * (dryL - dryR);
        float l = dryL, r = dryR;

        const auto aim = [this] (float& value, bool target)
        {
            const float delta = (target ? 1.f : 0.f) - value;
            value += std::abs (delta) < 1.e-5f ? delta : delta * smoothing;
        };
        aim (perspective, targetPerspective);
        aim (bypass, targetBypass);
        if (perspective < 1)
        {
            direct.processFrame (l, r);
            reflections.frame (mono, l, r);
        }
        else
            reflections.historyFrame (mono);
        if (perspective > 0)
        {
            if (fade == 0 && pending)
            {
                pending = false;
                next = Bank { requested };
                fade = fadeLength;
            }
            auto mic = render (current);
            if (fade > 0)
            {
                const auto changed = render (next);
                const auto blend = float (fadeLength - fade + 1) / float (fadeLength);
                mic[0] = std::lerp (mic[0], changed[0], blend);
                mic[1] = std::lerp (mic[1], changed[1], blend);
                if (--fade == 0) current = next;
            }
            l = std::lerp (l, mic[0], perspective);
            r = std::lerp (r, mic[1], perspective);
        }
        else if (pending || fade != 0)
        {
            current = Bank { requested };
            pending = false;
            fade = 0;
        }
        cursor = (cursor + 1) & mask;
        l = std::lerp (l, dryL, bypass);
        r = std::lerp (r, dryR, bypass);
        left[i] = right != nullptr ? clean (l) : clean (.5f * (l + r));
        if (right != nullptr) right[i] = clean (r);
    }
    return peaks;
}
}
