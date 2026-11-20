// SPDX-License-Identifier: GPL-3.0-or-later
#include "SpatialReverb.h"

#include <bit>
#include <numbers>

namespace studio::spatial
{
namespace
{
constexpr std::array baseDelay { .02171f, .02683f, .03163f, .03697f, .04231f, .04953f, .05737f, .06419f };
constexpr std::array splitHz { 177.0f, 354.0f, 707.0f, 1414.0f, 2828.0f };
constexpr float maxScale = 1.55f, maxHistory = .27f, motionSeconds = .00028f;
constexpr float unitary = 0.3535533905932738f, denormalFloor = 1.0e-20f;
constexpr std::array leftSigns { 1.f, -1.f, 1.f, -1.f, -1.f, 1.f, -1.f, 1.f };
constexpr std::array rightSigns { 1.f, 1.f, -1.f, -1.f, -1.f, -1.f, 1.f, 1.f };

float clean (float value) noexcept
{
    return std::abs (value) >= denormalFloor ? value : 0.0f;
}

float inputSample (float value) noexcept
{
    return std::isfinite (value) ? std::clamp (value, -16.0f, 16.0f) : 0.0f;
}

float safeWet (float sample) noexcept
{
    const float magnitude = std::abs (sample);
    if (magnitude <= 0.85f) return clean (sample);
    const float excess = magnitude - 0.85f;
    return std::copysign (0.85f + 0.145f * excess / (0.145f + excess), sample);
}

void mixFeedback (std::array<float, roomLineCount>& samples) noexcept
{
    // Three butterfly stages implement the orthonormal 8-point Hadamard.
    for (size_t span = 1; span < samples.size(); span *= 2)
        for (size_t start = 0; start < samples.size(); start += 2 * span)
            for (size_t i = start; i < start + span; ++i)
            {
                const float a = samples[i], b = samples[i + span];
                samples[i] = a + b;
                samples[i + span] = a - b;
            }
}
}

RoomPlan makeRoomPlan (Room raw)
{
    const auto room = validate (raw);
    const auto decay = predictDecay (room);
    const float volume = room.width * room.length * room.height;
    const float scale = std::clamp (std::pow (volume / 2520.0f, 0.16f), 0.58f, maxScale);
    const float pre = room.preDelayMs * .001f;
    RoomPlan plan;
    plan.early = room.early;
    plan.late = room.late;
    // Kept in the M4 calibration range at the factory Chamber Hall trims.
    plan.level = std::pow (10.0f, (room.gain + 16.0f) / 20.0f);
    plan.bypass = room.bypass ? 1.0f : 0.0f;
    plan.geometry.preDelay = pre;
    const float abWall = std::lerp (absorption[(size_t) room.wall][3], absorption[(size_t) Surface::absorber][3], room.treatment);
    const float wall = std::sqrt (1.0f - abWall);
    const float floor = std::sqrt (1.0f - absorption[(size_t) room.floor][3]);
    const float ceiling = std::sqrt (1.0f - absorption[(size_t) room.ceiling][3]);
    const std::array paths { 0.36f * room.height, 0.60f * room.width, 0.67f * room.length,
        1.20f * room.height, 0.52f * (room.width + room.height), 0.60f * (room.length + room.height) };
    const std::array levels { 0.26f * floor, 0.21f * wall, -0.17f * wall,
        0.17f * ceiling, -0.13f * wall * ceiling, 0.08f * wall * floor };
    constexpr std::array leftAdjust { .84f, .91f, 1.08f, .97f, .89f, 1.09f };
    constexpr std::array rightAdjust { 1.19f, 1.13f, .91f, 1.19f, 1.14f, .92f };
    for (size_t tap = 0; tap < paths.size(); ++tap)
    {
        const float base = paths[tap] / 343.0f;
        plan.geometry.reflection[tap] = pre + std::clamp (base * leftAdjust[tap], .004f, .158f);
        plan.geometry.reflection[tap + paths.size()] = pre + std::clamp (base * rightAdjust[tap], .005f, .163f);
        plan.reflectionGain[tap] = levels[tap];
        plan.reflectionGain[tap + paths.size()] = levels[tap] * (tap % 2 == 0 ? .86f : 1.06f);
    }

    for (size_t i = 0; i < roomLineCount; ++i)
    {
        const float delay = plan.geometry.delay[i] = scale * baseDelay[i];
        std::array<float, 6> gains;
        for (size_t band = 0; band < gains.size(); ++band)
            gains[band] = std::exp (-6.90775528f * delay / decay[band]);
        plan.feedback.back()[i] = gains.back();
        float bound = gains.back();
        for (size_t band = 0; band + 1 < gains.size(); ++band)
        {
            const auto correction = gains[band] - gains[band + 1];
            plan.feedback[band][i] = correction;
            bound += std::abs (correction);
        }
        // Each first-order low-pass has unity maximum gain. The bound also
        // covers alternating octave boosts and cuts and leaves modulator margin.
        if (bound > .985f)
            for (auto& band : plan.feedback) band[i] *= .985f / bound;
    }
    return plan;
}

void RoomReverb::Ring::prepare (size_t length)
{
    samples.resize (std::bit_ceil (length));
    mask = samples.size() - 1;
    clear();
}

void RoomReverb::Ring::clear()
{
    cursor = 0;
    std::fill (samples.begin(), samples.end(), 0.0f);
}

float RoomReverb::Ring::read (float delay) const noexcept
{
    const auto integer = (size_t) delay;
    const auto index = cursor - integer;
    const float fraction = delay - (float) integer;
    const float a = samples[index & mask], b = samples[(index - 1) & mask];
    return a + fraction * (b - a);
}

void RoomReverb::Ring::push (float value) noexcept
{
    samples[cursor] = value;
    cursor = (cursor + 1) & mask;
}

void RoomReverb::preparePlan (double sampleRate, const RoomPlan& initial)
{
    rate = std::isfinite (sampleRate) ? std::clamp (sampleRate, 8000.0, 384000.0) : 48000.0;
    smooth = (float) -std::expm1 (-1.0 / (.035 * rate));
    controlSmooth = (float) -std::expm1 (-16.0 / (.060 * rate));
    geometryFrames = (int) std::lround (.16 * rate);
    for (size_t i = 0; i < tank.size(); ++i)
        tank[i].prepare ((size_t) std::ceil ((baseDelay[i] * maxScale + motionSeconds) * rate) + 4);
    for (size_t band = 0; band < poles.size(); ++band)
        poles[band] = (float) -std::expm1 (-2.0 * std::numbers::pi * (double) splitHz[band] / rate);
    constexpr std::array delayMs { std::array { 3.13f, 5.31f, 9.17f }, std::array { 3.67f, 6.07f, 10.31f } };
    for (size_t side = 0; side < diffusers.size(); ++side)
    {
        inputHistory[side].prepare ((size_t) std::ceil (maxHistory * rate) + 4);
        for (size_t stage = 0; stage < diffusers[side].size(); ++stage)
        {
            const auto frames = (size_t) std::lround (delayMs[side][stage] * .001 * rate);
            diffuserDelays[side][stage] = (float) frames;
            diffusers[side][stage].prepare (frames + 2);
        }
    }
    target = initial;
    reset();
}

void RoomReverb::reset()
{
    for (auto& ring : tank) ring.clear();
    for (auto& ring : inputHistory) ring.clear();
    for (auto& side : diffusers)
        for (auto& ring : side) ring.clear();
    filters = {};
    constexpr std::array phases { -.87f, -.61f, -.35f, -.09f, .13f, .38f, .63f, .88f };
    constexpr std::array speeds { .19f, .27f, .31f, .37f, .41f, .47f, .53f, .59f };
    for (size_t i = 0; i < modulation.size(); ++i)
        modulation[i] = { phases[i], (i % 2 == 0 ? 4.f : -4.f) * speeds[i] / (float) rate };
    from = to = target.geometry;
    geometryStep = geometryFrames;
    feedback = target.feedback;
    reflectionGain = target.reflectionGain;
    early = target.early;
    late = target.late;
    updateOutputTarget();
    gain = targetGain;
    inputGain = target.bypass == 0.0f ? 1.0f : 0.0f;
    sleeping = true;
    settling = false;
    controlFrame = quietFrames = 0;
}

void RoomReverb::setPlan (const RoomPlan& next) noexcept
{
    target = next;
    settling = feedback != next.feedback || reflectionGain != next.reflectionGain;
    updateOutputTarget();
    if (sleeping)
    {
        feedback = next.feedback;
        reflectionGain = next.reflectionGain;
        from = to = next.geometry;
        geometryStep = geometryFrames;
        gain = targetGain;
        if (next.bypass != 0.0f) inputGain = 0.0f;
        early = next.early;
        late = next.late;
        settling = false;
    }
    else if (geometryStep == geometryFrames)
        startGeometryChange();
}

void RoomReverb::setCheckEnabled (bool enabled) noexcept
{
    if (checkEnabled == enabled) return;
    checkEnabled = enabled;
    updateOutputTarget();
    if (sleeping) gain = targetGain;
}

void RoomReverb::updateOutputTarget() noexcept
{
    constexpr float checkBoost = 3.9810717f;
    targetGain = target.bypass != 0.0f ? 0.0f : target.level * (checkEnabled ? checkBoost : 1.0f);
}

void RoomReverb::startGeometryChange() noexcept
{
    if (to == target.geometry) return;
    from = to;
    to = target.geometry;
    geometryStep = 0;
}

void RoomReverb::updateCoefficients() noexcept
{
    bool complete = true;
    const auto approach = [&] (float& value, float next)
    {
        const float delta = next - value;
        if (std::abs (delta) < 1.0e-5f) value = next;
        else
        {
            value += controlSmooth * delta;
            complete = false;
        }
    };
    for (size_t band = 0; band < feedback.size(); ++band)
        for (size_t line = 0; line < roomLineCount; ++line)
            approach (feedback[band][line], target.feedback[band][line]);
    for (size_t tap = 0; tap < reflectionGain.size(); ++tap) approach (reflectionGain[tap], target.reflectionGain[tap]);
    settling = ! complete;
}

float RoomReverb::diffuse (size_t channel, float sample) noexcept
{
    for (size_t stage = 0; stage < diffusers[channel].size(); ++stage)
    {
        auto& ring = diffusers[channel][stage];
        constexpr std::array coefficients { .62f, -.64f, .60f };
        const float coefficient = coefficients[stage];
        const float delay = ring.read (diffuserDelays[channel][stage]);
        const float out = delay - coefficient * sample;
        ring.push (clean (sample + coefficient * out));
        sample = out;
    }
    return sample;
}

RoomPeaks RoomReverb::process (float* left, float* right, int count) noexcept
{
    RoomPeaks peaks;
    if (inputHistory[0].samples.empty() || left == nullptr || count <= 0) return peaks;
    if (sleeping)
    {
        for (int i = 0; i < count; ++i)
            peaks.input = std::max (peaks.input, std::max (std::abs (inputSample (left[i])),
                right != nullptr ? std::abs (inputSample (right[i])) : 0.0f));
        if (peaks.input < 1.0e-8f || targetGain == 0.0f)
        {
            std::fill_n (left, count, 0.0f);
            if (right != nullptr) std::fill_n (right, count, 0.0f);
            return peaks;
        }
        sleeping = false;
        quietFrames = 0;
    }

    const auto sampleRate = (float) rate;
    const float transitionScale = 1.0f / (float) geometryFrames;
    const float targetInputGain = target.bypass == 0.0f ? 1.0f : 0.0f;
    float internalPeak = 0;
    for (int frame = 0; frame < count; ++frame)
    {
        if (settling)
        {
            if (controlFrame == 0) updateCoefficients();
            controlFrame = (controlFrame + 1) & 15;
        }
        const bool moving = geometryStep < geometryFrames;
        const float blend = moving ? (float) geometryStep * transitionScale : 1.0f;
        const auto read = [moving, blend, sampleRate] (const Ring& ring, float oldDelay, float newDelay, float offset)
        {
            const float next = ring.read (newDelay * sampleRate + offset);
            if (! moving || oldDelay == newDelay) return next;
            const float prev = ring.read (oldDelay * sampleRate + offset);
            return prev + blend * (next - prev);
        };
        const float dryL = inputSample (left[frame]);
        const float dryR = right != nullptr ? inputSample (right[frame]) : dryL;
        peaks.input = std::max (peaks.input, std::max (std::abs (dryL), std::abs (dryR)));

        early += smooth * (target.early - early);
        late += smooth * (target.late - late);
        gain += smooth * (targetGain - gain);
        if (targetGain == 0.0f && gain < 1.0e-6f) gain = 0.0f;
        // A bypassed return may wake up in the middle of a steady source.
        // Fade the injection so its onset cannot become a delayed click.
        if (inputGain != targetInputGain)
        {
            inputGain += smooth * (targetInputGain - inputGain);
            if (std::abs (targetInputGain - inputGain) < 1.0e-4f) inputGain = targetInputGain;
        }
        // The newest sample is one frame behind the cursor after pushing.
        inputHistory[0].push (dryL * inputGain);
        inputHistory[1].push (dryR * inputGain);
        float reflectionsL = 0, reflectionsR = 0;
        for (size_t tap = 0; tap < roomEarlyTaps / 2; ++tap)
        {
            const size_t r = tap + roomEarlyTaps / 2;
            reflectionsL += reflectionGain[tap] * read (inputHistory[tap % 3 == 2 ? 1 : 0],
                from.reflection[tap], to.reflection[tap], 1.0f);
            reflectionsR += reflectionGain[r] * read (inputHistory[tap % 3 == 2 ? 0 : 1],
                from.reflection[r], to.reflection[r], 1.0f);
        }
        const float diffusedL = diffuse (0, read (inputHistory[0], from.preDelay, to.preDelay, 1.0f));
        const float diffusedR = diffuse (1, read (inputHistory[1], from.preDelay, to.preDelay, 1.0f));
        std::array<float, roomLineCount> out, loop;
        float tailL = 0, tailR = 0;
        for (size_t line = 0; line < out.size(); ++line)
        {
            auto& lfo = modulation[line];
            lfo.value += lfo.step;
            if (lfo.value >= 1 || lfo.value <= -1)
            {
                lfo.value = std::clamp (lfo.value, -1.0f, 1.0f);
                lfo.step = -lfo.step;
            }
            const float value = out[line] = read (tank[line], from.delay[line], to.delay[line],
                                                 lfo.value * motionSeconds * sampleRate);
            internalPeak = std::max (internalPeak, std::abs (value));
            loop[line] = feedback.back()[line] * value;
            tailL += leftSigns[line] * value;
            tailR += rightSigns[line] * value;
        }
        // Store predictor state by band so the eight independent paths can
        // be vectorized and each write stays local to its cache line.
        for (size_t band = 0; band < poles.size(); ++band)
            for (size_t line = 0; line < out.size(); ++line)
            {
                auto& filter = filters[band][line];
                filter = clean (filter + poles[band] * (out[line] - filter));
                loop[line] += feedback[band][line] * filter;
            }
        mixFeedback (loop);
        for (size_t line = 0; line < tank.size(); ++line)
        {
            const float signL = (line & 1u) == 0 ? 1.0f : -1.0f;
            const float signR = (line & 2u) == 0 ? 1.0f : -1.0f;
            tank[line].push (clean (unitary * loop[line] + .165f * (signL * diffusedL + signR * diffusedR)));
        }

        const float wetL = safeWet ((reflectionsL * early + tailL * unitary * late) * gain);
        const float wetR = safeWet ((reflectionsR * early + tailR * unitary * late) * gain);
        left[frame] = right != nullptr ? wetL : .5f * (wetL + wetR);
        if (right != nullptr) right[frame] = wetR;
        peaks.wet = std::max (peaks.wet, right != nullptr ? std::max (std::abs (wetL), std::abs (wetR)) : std::abs (left[frame]));
        if (moving && ++geometryStep == geometryFrames) startGeometryChange();
    }

    if ((peaks.input < 1.0e-8f || gain == 0.0f) && internalPeak < 1.0e-7f && peaks.wet < 1.0e-7f)
        quietFrames += count;
    else
        quietFrames = 0;
    if (quietFrames > (int) (.65 * rate)) sleeping = true;
    return peaks;
}

std::size_t RoomReverb::allocatedBytes() const noexcept
{
    std::size_t samples = 0;
    for (const auto& ring : tank) samples += ring.samples.capacity();
    for (const auto& ring : inputHistory) samples += ring.samples.capacity();
    for (const auto& side : diffusers)
        for (const auto& ring : side) samples += ring.samples.capacity();
    return samples * sizeof (float);
}
}
