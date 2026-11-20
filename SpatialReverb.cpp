// SPDX-License-Identifier: GPL-3.0-or-later
#include "SpatialReverb.h"
#include <numbers>

namespace studio::spatial
{
namespace
{
constexpr std::array lineSeconds { 0.0297f, 0.0371f, 0.0411f, 0.0533f };
constexpr std::array splits { 177.0f, 354.0f, 707.0f, 1414.0f, 2828.0f };
float finiteAudio (float x) noexcept
{
    return std::isfinite (x) ? std::clamp (x, -16.0f, 16.0f) : 0.0f;
}
}

void RoomReverb::prepare (double sampleRate, Room initial)
{
    rate = std::isfinite (sampleRate) ? std::clamp (sampleRate, 8000.0, 384000.0) : 48000.0;
    smooth = (float) -std::expm1 (-1.0 / (0.04 * rate));
    for (size_t i = 0; i < tank.size(); ++i) tank[i].samples.resize ((size_t) std::lround (rate * lineSeconds[i]));
    for (auto& channel : history) channel.resize ((size_t) std::ceil (0.24 * rate) + 4);
    for (size_t i = 0; i < poles.size(); ++i)
        poles[i] = (float) -std::expm1 (-2.0 * std::numbers::pi * (double) splits[i] / rate);
    room = validate (initial);
    updateTargets();
    current = target;
    reset();
}

void RoomReverb::reset()
{
    cursor = 0;
    for (auto& channel : history) std::fill (channel.begin(), channel.end(), 0.0f);
    for (auto& line : tank)
    {
        std::fill (line.samples.begin(), line.samples.end(), 0.0f);
        line.cursor = 0;
        line.filtered.fill (0);
    }
}

void RoomReverb::setRoom (Room next)
{
    next = validate (next);
    if (next == room) return;
    room = next;
    updateTargets();
}

void RoomReverb::setCheckEnabled (bool enabled)
{
    if (checkEnabled == enabled) return;
    checkEnabled = enabled;
    updateOutputTarget();
}

void RoomReverb::updateOutputTarget()
{
    // The early taps and diffusion tank attenuate a dry instrument well
    // below the level implied by the send and return trims. Calibrate the
    // wet output, after the feedback network, with the existing trims intact.
    constexpr float wetCalibrationDb = 20.0f;
    const float outputDb = room.gain + wetCalibrationDb + (checkEnabled ? roomCheckBoostDb : 0.0f);
    target.output = room.bypass ? 0.0f : std::pow (10.0f, outputDb / 20.0f);
}

void RoomReverb::updateTargets()
{
    const auto decay = predictDecay (room);
    target.delay = { (float) (room.width * rate / 690.0), (float) (room.length * rate / 624.0),
                     (float) (room.height * rate / 287.0), (float) ((room.width + room.length) * rate / 814.0) };
    target.early = room.early;
    target.late = room.late;
    updateOutputTarget();
    for (size_t i = 0; i < tank.size(); ++i)
    {
        std::array<float, 6> gain;
        for (size_t band = 0; band < gain.size(); ++band)
            gain[band] = std::pow (0.001f, lineSeconds[i] / decay[band]);
        auto& band = target.feedback[i];
        band.back() = gain.back();
        float worstCase = band.back();
        for (size_t k = 0; k < poles.size(); ++k)
        {
            band[k] = gain[k] - gain[k + 1];
            worstCase += std::abs (band[k]);
        }
        // Sum-of-magnitudes bound for the direct term and the five unity
        // lowpasses. Keeps each loop contractive even with extreme EQ shapes.
        if (worstCase > 0.99f) for (auto& value : band) value *= 0.99f / worstCase;
    }
}

float RoomReverb::read (unsigned channel, float delay) const noexcept
{
    const auto& data = history[channel];
    const auto integer = (size_t) delay;
    const auto index = cursor >= integer ? cursor - integer : cursor + data.size() - integer;
    const auto prior = index == 0 ? data.size() - 1 : index - 1;
    return std::lerp (data[index], data[prior], delay - (float) integer);
}

RoomPeaks RoomReverb::process (float* left, float* right, int count) noexcept
{
    RoomPeaks peaks;
    if (history[0].empty() || left == nullptr) return peaks;
    const auto smoothTo = [this] (float& currentValue, float next)
    {
        currentValue += smooth * (next - currentValue);
    };
    for (int frame = 0; frame < count; ++frame)
    {
        const float inputL = finiteAudio (left[frame]), inputR = right != nullptr ? finiteAudio (right[frame]) : inputL;
        peaks.input = std::max (peaks.input, std::max (std::abs (inputL), std::abs (inputR)));
        history[0][cursor] = inputL;
        history[1][cursor] = inputR;
        for (size_t i = 0; i < current.delay.size(); ++i)
        {
            const float next = std::clamp (target.delay[i], 1.0f, (float) history[0].size() - 2.0f);
            current.delay[i] = std::clamp (current.delay[i], 1.0f, (float) history[0].size() - 2.0f);
            current.delay[i] += std::clamp (smooth * (next - current.delay[i]), -0.15f, 0.15f);
        }
        const float reflectionsL = 0.40f * read (0, current.delay[0]) + 0.27f * read (1, current.delay[2])
                                  - 0.18f * read (0, current.delay[3]);
        const float reflectionsR = 0.40f * read (1, current.delay[1]) + 0.27f * read (0, current.delay[2])
                                  - 0.18f * read (1, current.delay[3]);
        if (++cursor == history[0].size()) cursor = 0;

        std::array<float, 4> output, loop;
        for (size_t i = 0; i < tank.size(); ++i)
        {
            auto& line = tank[i];
            const float value = output[i] = line.samples[line.cursor];
            auto& gain = current.feedback[i];
            smoothTo (gain.back(), target.feedback[i].back());
            loop[i] = gain.back() * value;
            for (size_t band = 0; band < poles.size(); ++band)
            {
                smoothTo (gain[band], target.feedback[i][band]);
                auto& filter = line.filtered[band];
                filter += poles[band] * (value - filter);
                if (std::abs (filter) < 1.0e-23f) filter = 0;
                loop[i] += gain[band] * filter;
            }
        }
        const float mix = 0.5f * (loop[0] + loop[1] + loop[2] + loop[3]);
        for (size_t i = 0; i < tank.size(); ++i)
        {
            auto& line = tank[i];
            const float inject = i % 2 == 0 ? inputL : inputR;
            const float next = 0.22f * inject + loop[i] - mix;
            line.samples[line.cursor] = std::abs (next) >= 1.0e-23f ? next : 0.0f;
            if (++line.cursor == line.samples.size()) line.cursor = 0;
        }
        smoothTo (current.early, target.early);
        smoothTo (current.late, target.late);
        smoothTo (current.output, target.output);
        if (target.output == 0.0f && current.output < 1.0e-6f) current.output = 0;
        const float wetL = (reflectionsL * current.early + current.late * 0.5f * (output[0] - output[3])) * current.output;
        const float wetR = (reflectionsR * current.early + current.late * 0.5f * (output[1] - output[2])) * current.output;
        left[frame] = right != nullptr ? wetL : 0.5f * (wetL + wetR);
        if (right != nullptr) right[frame] = wetR;
        peaks.wet = std::max (peaks.wet, right != nullptr
            ? std::max (std::abs (wetL), std::abs (wetR)) : std::abs (left[frame]));
    }
    return peaks;
}

std::size_t RoomReverb::allocatedBytes() const noexcept
{
    std::size_t samples = 0;
    for (const auto& line : tank) samples += line.samples.capacity();
    for (const auto& channel : history) samples += channel.capacity();
    return samples * sizeof (float);
}
}
