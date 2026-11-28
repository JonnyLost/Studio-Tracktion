// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "SpatialModel.h"
#include "SpatialSource.h"
#include <bit>

namespace studio::spatial::stage
{
using Point = early::Point;
inline constexpr size_t microphoneLimit = 6;
inline constexpr double speed = early::speed;
inline constexpr double microphoneHistorySeconds = .31;

enum class Family { general, soloStrings, stringSection, winds, brass, percussion, keys, harp, choir };
inline constexpr std::array familyNames { "General", "Solo strings", "String section", "Woodwinds", "Brass",
    "Percussion", "Keyboards", "Harp", "Choir" };

struct Source
{
    float height = 1.2f, orientation = 180, width = 1, directDb = 0, direction = 0.5f;
    float early = 0.65f;
    Family family = Family::general;
    bool operator== (const Source&) const = default;
};

inline Source validate (Source s)
{
    s.height = bounded (s.height, 0.25f, 20, 1.2f);
    s.orientation = bounded (s.orientation, -180, 180, 180);
    s.width = bounded (s.width, 0, 2, 1);
    s.directDb = bounded (s.directDb, -24, 6, 0);
    s.direction = bounded (s.direction, 0, 1, .5f);
    s.early = bounded (s.early, 0, 1, .65f);
    if ((unsigned) s.family >= familyNames.size()) s.family = Family::general;
    return s;
}

struct Bounds { double halfWidth, front, rear; };

inline Bounds stageBounds (Room raw)
{
    const auto room = spatial::validate (raw);
    return { room.width * .41, room.length * .34, room.length * .88 };
}

// +X is the conductor's right when facing the stage; +Y runs from
// downstage to upstage, starting at the front room wall; +Z is height.
inline Point sourcePoint (Room room, Position seat, float height)
{
    const auto bounds = stageBounds (room);
    seat = validPosition (seat);
    return { bounds.halfWidth * seat.x, std::lerp (bounds.front, bounds.rear, seat.depth),
             std::clamp (double (height), .1, double (spatial::validate (room).height) - .1) };
}

inline Position atlasToSeat (double chartX, double chartY)
{
    return validPosition ({ float (2 * chartX - 1), float (1 - chartY) });
}

inline Point receiverPoint (Room raw, Position position, float height)
{
    const auto room = spatial::validate (raw);
    position = validPosition (position);
    return { room.width * .46 * position.x, room.length * (.12 + .79 * position.depth),
             std::clamp (double (height), .1, double (room.height) - .1) };
}

inline double directionCosine (Point from, Point to, float azimuth, float elevation = 0)
{
    const auto az = azimuth * early::pi / 180, el = elevation * early::pi / 180;
    return std::clamp (((to.x - from.x) * std::sin (az) * std::cos (el)
        + (to.y - from.y) * std::cos (az) * std::cos (el) + (to.z - from.z) * std::sin (el))
        / std::max (.001, early::length (from, to)), -1., 1.);
}

inline float polarGain (Pattern pattern, double cosine)
{
    switch (pattern)
    {
        case Pattern::cardioid: return float (.5 + .5 * cosine);
        case Pattern::supercardioid: return float (.37 + .63 * cosine);
        case Pattern::figureEight: return float (cosine);
        default: return 1;
    }
}

struct Radiation { float low, high; };

inline Radiation radiation (Source setup, Point from, Point to)
{
    constexpr std::array lowCut { .09f, .11f, .07f, .10f, .24f, .07f, .05f, .08f, .16f };
    constexpr std::array highCut { .31f, .42f, .25f, .48f, .77f, .30f, .28f, .36f, .65f };
    const auto family = (size_t) setup.family;
    const auto offAxis = setup.direction * float ((1 - directionCosine (from, to, setup.orientation)) * .5);
    return { 1 - lowCut[family] * offAxis, 1 - highCut[family] * offAxis };
}

inline std::array<Point, early::count> imagePoints (Room room, Point point)
{
    return {{ { -room.width - point.x, point.y, point.z }, { room.width - point.x, point.y, point.z },
        { point.x, -point.y, point.z }, { point.x, 2 * room.length - point.y, point.z },
        { point.x, point.y, -point.z }, { point.x, point.y, 2 * room.height - point.z } }};
}

inline std::array<early::Material, early::count> materials (Room room)
{
    constexpr std::array table { early::wood, early::plaster, early::stone, early::carpet, early::treated };
    const auto flat = table[(size_t) room.wall];
    const early::Material wall { std::lerp (flat.reflection, early::treated.reflection, room.treatment),
        std::lerp (flat.cutoff, early::treated.cutoff, room.treatment) };
    return { wall, wall, wall, wall, table[(size_t) room.floor], table[(size_t) room.ceiling] };
}

struct Settings
{
    Room room;
    Position seat, listener {0, .08f};
    float listenerHeight = 1.4f;
    Source source;
    std::array<Mic, microphoneLimit> microphones {};
    int microphoneCount = 0;
    bool microphoneMix = false, bypass = false;
};

inline std::array<float, 2> diffuseFieldGain (const std::array<Mic, microphoneLimit>& mics, int count)
{
    if (count <= 0 || count > (int) microphoneLimit) return { 1.f, 1.f };
    bool solo = false;
    for (int i = 0; i < count; ++i) solo |= mics[(size_t) i].solo;
    std::array<float, 2> energy {};
    constexpr std::array polarRms { 1.f, .58f, .52f, .58f };
    for (int i = 0; i < count; ++i)
    {
        const auto mic = spatial::validate (mics[(size_t) i]);
        if (mic.mute || (solo && ! mic.solo)) continue;
        const auto gain = source::gain (mic.gain) * polarRms[(size_t) mic.pattern];
        energy[0] += gain * gain * (1 - mic.pan);
        energy[1] += gain * gain * (1 + mic.pan);
    }
    return { std::clamp (std::sqrt (energy[0] / float (count)), 0.f, 2.f),
             std::clamp (std::sqrt (energy[1] / float (count)), 0.f, 2.f) };
}

struct CapsuleTap
{
    float delay = 0, gain = 0, pole = 1, high = 1;
    bool operator== (const CapsuleTap&) const = default;
};

struct Capsule
{
    CapsuleTap direct;
    std::array<CapsuleTap, early::count> reflections;
    float left = 0, right = 0;
    bool operator== (const Capsule&) const = default;
};

struct MicPlan
{
    std::array<Capsule, microphoneLimit> capsules {};
    float side = 0;
    int count = 0;
    bool operator== (const MicPlan&) const = default;
};

struct Plan
{
    source::Plan direct;
    early::Plan earlyField;
    MicPlan microphones;
    bool microphoneMix = false, bypass = false;
};

inline float distanceGain (double metres)
{
    return float (std::clamp (4. / std::max (metres, 4.), .055, 1.));
}

inline Plan makePlan (double rate, Settings raw)
{
    if (! early::supported (rate)) rate = 48000;
    const auto room = spatial::validate (raw.room);
    const auto setup = validate (raw.source);
    const auto s = sourcePoint (room, raw.seat, setup.height);
    const auto reference = receiverPoint (room, raw.listener, bounded (raw.listenerHeight, .25f, 20, 1.4f));
    const auto distance = early::length (s, reference);
    const auto paths = imagePoints (room, s);
    const auto departures = imagePoints (room, reference);
    const auto surfaces = materials (room);
    const auto directivity = radiation (setup, s, reference);

    source::Parameters p;
    p.width = setup.width;
    p.outputDb = setup.directDb;
    p.direction.angle = float (std::atan2 (s.x - reference.x, s.y - reference.y) * 180 / early::pi);
    p.direction.focus = .25f;
    Plan plan;
    plan.bypass = raw.bypass;
    plan.direct = source::makePlan (rate, p);
    plan.direct.level = distanceGain (distance) * directivity.low;
    plan.direct.airPole = source::coefficient (rate, 17500. / (1 + .11 * distance));
    plan.direct.airMix = std::clamp (float (distance - 3) * .035f + (1 - directivity.high / directivity.low), 0.f, .95f);

    plan.earlyField.rate = rate;
    plan.earlyField.enabled = setup.early * room.early > 0;
    if (plan.earlyField.enabled)
        for (size_t i = 0; i < paths.size(); ++i)
        {
            const auto path = early::length (paths[i], reference);
            const auto excess = std::max (0., path - distance);
            if (excess / speed > early::maxSeconds) continue;
            const auto pan = std::clamp ((paths[i].x - reference.x) / path, -1., 1.);
            const auto ray = radiation (setup, s, departures[i]);
            const auto gain = .19f * setup.early * room.early * surfaces[i].reflection * distanceGain (path) * ray.low;
            plan.earlyField.taps[i] = { float (std::max (1., excess * rate / speed)),
                gain * float (std::sqrt (.5 * (1 - pan))), gain * float (std::sqrt (.5 * (1 + pan))),
                source::coefficient (rate, surfaces[i].cutoff * ray.high / (1 + .02 * excess)) };
        }

    const int count = std::clamp (raw.microphoneCount, 0, int (microphoneLimit));
    plan.microphoneMix = raw.microphoneMix && count > 0;
    if (! plan.microphoneMix) return plan;
    bool solo = false;
    for (int i = 0; i < count; ++i) solo |= raw.microphones[(size_t) i].solo;
    plan.microphones.side = .25f * setup.width;
    plan.microphones.count = count;
    double origin = microphoneHistorySeconds * speed;
    for (int j = 0; j < count; ++j)
    {
        const auto other = spatial::validate (raw.microphones[(size_t) j]);
        origin = std::min (origin, early::length (s, receiverPoint (room, other.position, other.height)));
    }
    for (int i = 0; i < count; ++i)
    {
        const auto mic = spatial::validate (raw.microphones[(size_t) i]);
        if (mic.mute || (solo && ! mic.solo)) continue;
        const auto receiver = receiverPoint (room, mic.position, mic.height);
        const auto mirror = imagePoints (room, receiver);
        // The nearest capsule establishes the timing reference for this source.
        // Use the same origin for every capsule and all of its reflections.
        const float delay = std::max (0.f, mic.delay) * .001f;
        auto& capsule = plan.microphones.capsules[(size_t) i];
        const auto gain = source::gain (mic.gain) / std::sqrt (float (count));
        capsule.left = gain * std::sqrt (.5f * (1 - mic.pan));
        capsule.right = gain * std::sqrt (.5f * (1 + mic.pan));
        const auto tap = [&] (Point arrival, Point departure, float level, float cutoff, bool reflection)
        {
            const auto length = early::length (arrival, receiver);
            const auto seconds = std::max (0., (length - origin) / speed) + delay;
            if (seconds > microphoneHistorySeconds) return CapsuleTap {};
            const auto ray = radiation (setup, s, departure);
            const auto polar = polarGain (mic.pattern, directionCosine (receiver, arrival, mic.azimuth, mic.elevation));
            return CapsuleTap { float (seconds * rate), level * polar * distanceGain (length) * ray.low,
                source::coefficient (rate, cutoff / (1 + .053 * length)),
                ray.high / ray.low * (reflection ? .23f : float (std::exp (-.012 * length))) };
        };
        capsule.direct = tap (s, receiver, source::gain (setup.directDb), 17000.f, false);
        if (setup.early * room.early > 0)
            for (size_t k = 0; k < paths.size(); ++k)
                capsule.reflections[k] = tap (paths[k], mirror[k],
                    .19f * setup.early * room.early * surfaces[k].reflection, surfaces[k].cutoff, true);
    }
    return plan;
}

class Renderer
{
public:
    bool prepare (double sampleRate, const Plan& initial);
    void setPlan (const Plan&) noexcept;
    void reset() noexcept;
    std::array<float, microphoneLimit> process (float*, float*, int) noexcept;
    size_t allocatedBytes() const noexcept;

private:
    struct Filters
    {
        std::array<float, early::count> reflections {};
        float direct = 0, side = 0;
    };
    struct Bank
    {
        MicPlan plan;
        std::array<Filters, microphoneLimit> filters {};
    };
    source::Processor direct;
    early::Engine reflections;
    Bank current, next;
    MicPlan requested;
    std::vector<float> mid, side;
    size_t cursor = 0, mask = 0;
    int fade = 0, fadeLength = 1, silentFrames = 0, historyFrames = 0;
    float bypass = 0, perspective = 0, smoothing = .001f;
    bool targetBypass = false, targetPerspective = false, ready = false, pending = false;
    std::array<float, microphoneLimit> peaks {};
    std::array<float, 2> render (Bank&) noexcept;
    float read (const std::vector<float>&, float) const noexcept;
};
}
