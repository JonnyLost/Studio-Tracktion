// SPDX-License-Identifier: GPL-3.0-or-later
#include "SpatialReverb.h"

#include <bit>
#include <numbers>

namespace studio::spatial
{
namespace
{
constexpr std::array baseDelay { .02171f, .02683f, .03163f, .03697f, .04231f, .04953f, .05737f, .06419f,
    .01931f, .02359f, .02911f, .03403f, .03991f, .04661f, .05323f, .06103f };
constexpr std::array splitHz { 177.0f, 354.0f, 707.0f, 1414.0f, 2828.0f };
constexpr float maxScale = 1.55f, maxHistory = .27f, maximumMotion = .0013f, motionSeconds = .00028f;
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

void mixFeedback (std::array<float, roomLineCount>& samples, size_t lines) noexcept
{
    // Three butterfly stages implement the orthonormal 8-point Hadamard.
    for (size_t span = 1; span < lines; span *= 2)
        for (size_t start = 0; start < lines; start += 2 * span)
            for (size_t i = start; i < start + span; ++i)
            {
                const float a = samples[i], b = samples[i + span];
                samples[i] = a + b;
                samples[i + span] = a - b;
            }
}
}

RoomPlan makeRoomPlan (Room raw, RoomMode mode)
{
    const auto room = validate (raw);
    const auto decay = predictDecay (room);
    const float volume = room.width * room.length * room.height;
    const bool modern = mode != RoomMode::legacy;
    const float scale = std::clamp (std::pow (volume / 2520.0f, 0.16f) * (modern ? room.shape.size : 1.f), modern ? 0.44f : 0.58f, maxScale);
    const float pre = room.preDelayMs * .001f;
    RoomPlan plan;
    plan.early = mode == RoomMode::stage ? 0.f : room.early;
    plan.late = room.late;
    // Kept in the M4 calibration range at the factory Chamber Hall trims.
    plan.level = std::pow (10.0f, (room.gain + 16.0f) / 20.0f);
    plan.bypass = room.bypass ? 1.0f : 0.0f;
    plan.geometry.preDelay = pre;
    if (modern)
    {
        const auto& shape = room.shape;
        plan.mode = float ((int) shape.quality + 1);
        plan.diffusion = shape.diffusion;
        plan.buildup = .25f * shape.density + .75f * shape.attack;
        plan.motion = shape.motionDepthMs * .001f;
        plan.motionRate = shape.motionRate / .38f;
        plan.width = shape.width;
        plan.lowEq = std::pow (10.f, shape.lowEq / 20);
        plan.highEq = std::pow (10.f, shape.highEq / 20);
        plan.standalone = mode == RoomMode::effect ? 1.f : 0.f;
        plan.mix = plan.standalone != 0 ? shape.mix : 1.f;
    }
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

    constexpr std::array ecoDelays { .01931f, .03163f, .04661f, .06103f };
    for (size_t i = 0; i < roomLineCount; ++i)
    {
        const float seconds = plan.mode == 1 && i < ecoDelays.size() ? ecoDelays[i] : baseDelay[i];
        const float distribution = modern ? std::lerp ((i & 1u) == 0 ? .94f : 1.05f, 1.f, room.shape.density) : 1.f;
        const float delay = plan.geometry.delay[i] = std::min (maxScale, scale * distribution) * seconds;
        std::array<float, 6> gains;
        for (size_t band = 0; band < gains.size(); ++band)
            gains[band] = std::exp (-6.90775528f * delay / std::clamp (decay[band] * (modern ? room.shape.decay : 1.f), .15f, 12.f));
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

void RoomReverb::Ring::clear() noexcept
{
    cursor = written = 0;
}

float RoomReverb::Ring::read (float delay) const noexcept
{
    const auto integer = (size_t) delay;
    const auto index = cursor - integer;
    const float fraction = delay - (float) integer;
    const float a = integer <= written ? samples[index & mask] : 0.f;
    const float b = integer < written ? samples[(index - 1) & mask] : 0.f;
    return a + fraction * (b - a);
}

void RoomReverb::Ring::push (float value) noexcept
{
    samples[cursor] = value;
    cursor = (cursor + 1) & mask;
    if (written < samples.size()) ++written;
}

void RoomReverb::preparePlan (double sampleRate, const RoomPlan& initial)
{
    rate = std::isfinite (sampleRate) ? std::clamp (sampleRate, 8000.0, 384000.0) : 48000.0;
    smooth = (float) -std::expm1 (-1.0 / (.035 * rate));
    controlSmooth = (float) -std::expm1 (-16.0 / (.060 * rate));
    geometryFrames = (int) std::lround (.16 * rate);
    for (size_t i = 0; i < tank.size(); ++i)
        tank[i].prepare ((size_t) std::ceil ((std::max (baseDelay[i], i < 4 ? .062f : 0.f) * maxScale + maximumMotion) * rate) + 4);
    for (size_t band = 0; band < poles.size(); ++band)
        poles[band] = (float) -std::expm1 (-2.0 * std::numbers::pi * (double) splitHz[band] / rate);
    eqPoles = { (float) -std::expm1 (-2 * std::numbers::pi * 210 / rate),
                (float) -std::expm1 (-2 * std::numbers::pi * 4200 / rate) };
    constexpr std::array delayMs { std::array { 3.13f, 5.31f, 9.17f, 12.37f, 16.97f },
                                  std::array { 3.67f, 6.07f, 10.31f, 14.11f, 19.31f } };
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

void RoomReverb::switchNetwork() noexcept
{
    mode = (int) target.mode;
    lines = mode == 1 ? 4 : mode == 3 ? 16 : 8;
    for (auto& ring : tank) ring.clear();
    for (auto& side : diffusers)
        for (auto& ring : side) ring.clear();
    filters = {};
    eqState = {};
    constexpr std::array phases { -.87f, -.61f, -.35f, -.09f, .13f, .38f, .63f, .88f };
    constexpr std::array speeds { .19f, .27f, .31f, .37f, .41f, .47f, .53f, .59f };
    for (size_t i = 0; i < modulation.size(); ++i)
        modulation[i] = { phases[i % 8] * (i < 8 ? 1.f : -.82f),
            (i % 2 == 0 ? 4.f : -4.f) * speeds[i % 8] * (i < 8 ? 1.f : 1.13f) / (float) rate };
    from = to = target.geometry;
    geometryStep = geometryFrames;
    feedback = target.feedback;
    reflectionGain = target.reflectionGain;
    settling = false;
}

void RoomReverb::reset()
{
    switchNetwork();
    for (auto& ring : inputHistory) ring.clear();
    early = target.early;
    late = target.late;
    crossfade = 1;
    outputMix = target.bypass != 0 && target.standalone != 0 ? 0 : target.mix;
    stereoWidth = target.width;
    motionDepth = target.motion;
    motionSpeed = target.motionRate;
    diffusionAmount = target.diffusion;
    build = target.buildup;
    equalLow = target.lowEq;
    equalHigh = target.highEq;
    leftGain = target.leftGain;
    rightGain = target.rightGain;
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
        if (mode != (int) target.mode) switchNetwork();
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
    const auto stages = (size_t) (mode == 1 ? 2 : mode == 3 ? 5 : mode == 2 ? 4 : 3);
    for (size_t stage = 0; stage < stages; ++stage)
    {
        auto& ring = diffusers[channel][stage];
        constexpr std::array coefficients { .62f, -.64f, .60f, -.56f, .51f };
        const float coefficient = coefficients[stage] * (mode == 0 ? 1.f : diffusionAmount);
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
        if (peaks.input < 1.0e-8f || (targetGain == 0.0f && target.standalone == 0))
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
    const bool standalone = target.standalone != 0;
    const float targetMix = standalone && target.bypass != 0 ? 0.f : target.mix;
    const float switchIncrement = 1.f / (.03f * sampleRate);
    float internalPeak = 0;
    for (int frame = 0; frame < count; ++frame)
    {
        if (mode != (int) target.mode)
        {
            crossfade = std::max (0.f, crossfade - switchIncrement);
            if (crossfade == 0) switchNetwork();
        }
        else
            crossfade = std::min (1.f, crossfade + switchIncrement);
        const auto lineCount = (size_t) lines;
        const float normalization = mode == 1 ? .5f : mode == 3 ? .25f : unitary;
        if (mode != 0)
        {
            stereoWidth += smooth * (target.width - stereoWidth);
            motionDepth += smooth * (target.motion - motionDepth);
            motionSpeed += smooth * (target.motionRate - motionSpeed);
            diffusionAmount += smooth * (target.diffusion - diffusionAmount);
            build += smooth * (target.buildup - build);
            equalLow += smooth * (target.lowEq - equalLow);
            equalHigh += smooth * (target.highEq - equalHigh);
            leftGain += smooth * (target.leftGain - leftGain);
            rightGain += smooth * (target.rightGain - rightGain);
        }
        outputMix += smooth * (targetMix - outputMix);
        if (std::abs (targetMix - outputMix) < 1e-5f) outputMix = targetMix;
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
        for (size_t tap = 0; early > 1e-7f && tap < roomEarlyTaps / 2; ++tap)
        {
            const size_t r = tap + roomEarlyTaps / 2;
            reflectionsL += reflectionGain[tap] * read (inputHistory[tap % 3 == 2 ? 1 : 0],
                from.reflection[tap], to.reflection[tap], 1.0f);
            reflectionsR += reflectionGain[r] * read (inputHistory[tap % 3 == 2 ? 0 : 1],
                from.reflection[r], to.reflection[r], 1.0f);
        }
        const float preL = read (inputHistory[0], from.preDelay, to.preDelay, 1.0f);
        const float preR = read (inputHistory[1], from.preDelay, to.preDelay, 1.0f);
        const float diffusedL = std::lerp (preL, diffuse (0, preL), mode == 0 ? 1.f : build);
        const float diffusedR = std::lerp (preR, diffuse (1, preR), mode == 0 ? 1.f : build);
        std::array<float, roomLineCount> out, loop;
        float tailL = 0, tailR = 0;
        for (size_t line = 0; line < lineCount; ++line)
        {
            auto& lfo = modulation[line];
            lfo.value += lfo.step * (mode == 0 ? 1.f : motionSpeed);
            if (lfo.value >= 1 || lfo.value <= -1)
            {
                lfo.value = std::clamp (lfo.value, -1.0f, 1.0f);
                lfo.step = -lfo.step;
            }
            const float value = out[line] = read (tank[line], from.delay[line], to.delay[line],
                                                 lfo.value * (mode == 0 ? motionSeconds : motionDepth) * sampleRate);
            internalPeak = std::max (internalPeak, std::abs (value));
            loop[line] = feedback.back()[line] * value;
            const auto polarity = line < 8 ? 1.f : -1.f;
            tailL += leftSigns[line % 8] * value;
            tailR += rightSigns[line % 8] * value * polarity;
        }
        // Store predictor state by band so the eight independent paths can
        // be vectorized and each write stays local to its cache line.
        for (size_t band = 0; band < poles.size(); ++band)
            for (size_t line = 0; line < lineCount; ++line)
            {
                auto& filter = filters[band][line];
                filter = clean (filter + poles[band] * (out[line] - filter));
                loop[line] += feedback[band][line] * filter;
            }
        mixFeedback (loop, lineCount);
        for (size_t line = 0; line < lineCount; ++line)
        {
            const float signL = (line & 1u) == 0 ? 1.0f : -1.0f;
            const float signR = (line & 2u) == 0 ? 1.0f : -1.0f;
            const auto inputGainCompensation = mode == 1 ? .195f : mode == 3 ? .121f : .165f;
            const auto polarity = line < 8 ? 1.f : -1.f;
            tank[line].push (clean (normalization * loop[line]
                + inputGainCompensation * (signL * diffusedL + polarity * signR * diffusedR)));
        }

        float outL = reflectionsL * early + tailL * normalization * late;
        float outR = reflectionsR * early + tailR * normalization * late;
        if (mode != 0)
        {
            const auto mid = .5f * (outL + outR);
            outL = std::lerp (mid, outL, stereoWidth);
            outR = std::lerp (mid, outR, stereoWidth);
            const auto equalize = [&] (float sample, size_t side)
            {
                auto& eq = eqState[side];
                eq[0] = clean (eq[0] + eqPoles[0] * (sample - eq[0]));
                eq[1] = clean (eq[1] + eqPoles[1] * (sample - eq[1]));
                return sample + (equalLow - 1) * eq[0] + (equalHigh - 1) * (sample - eq[1]);
            };
            outL = equalize (outL, 0);
            outR = equalize (outR, 1);
        }
        const float wetL = safeWet (outL * gain * crossfade * leftGain);
        const float wetR = safeWet (outR * gain * crossfade * rightGain);
        const float l = standalone ? std::lerp (dryL, wetL, outputMix) : wetL;
        const float r = standalone ? std::lerp (dryR, wetR, outputMix) : wetR;
        left[frame] = right != nullptr ? l : .5f * (l + r);
        if (right != nullptr) right[frame] = r;
        peaks.wet = std::max (peaks.wet, right != nullptr ? std::max (std::abs (wetL), std::abs (wetR)) : std::abs (.5f * (wetL + wetR)));
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
