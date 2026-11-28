// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <cmath>

namespace studio::spatial
{
inline float bounded (float value, float low, float high, float fallback)
{
    return std::isfinite (value) ? std::clamp (value, low, high) : fallback;
}

enum class Section { strings, woodwinds, brass, percussion, keys, other };
enum class Seating { concert, chamber, scoring };
inline constexpr std::array sectionNames { "Strings", "Woodwinds", "Brass", "Percussion", "Keyboards", "Other" };
inline constexpr std::array seatingNames { "Concert orchestra", "Chamber ensemble", "Scoring stage" };

struct Position
{
    float x = 0.0f; // Audience perspective, left to right.
    float depth = 0.35f; // Front to back.
    bool operator== (const Position&) const = default;
};

inline Position validPosition (Position p)
{
    return { bounded (p.x, -1.0f, 1.0f, 0.0f), bounded (p.depth, 0.0f, 1.0f, 0.35f) };
}

inline Position seat (Seating layout, Section section, int index, int sectionSize)
{
    constexpr std::array depth { 0.21f, 0.46f, 0.67f, 0.86f, 0.31f, 0.54f };
    const float stagger = layout == Seating::chamber ? 0.72f : 1.0f;
    const int row = index / 9, col = index % 9;
    const int count = std::clamp (sectionSize - row * 9, 1, 9);
    const float spread = count == 1 ? 0.0f : ((float) col / (float) (count - 1) - 0.5f) * 2.0f;
    const float centre = section == Section::keys ? -0.70f : section == Section::other ? 0.70f : 0.0f;
    if (layout == Seating::scoring)
    {
        // Parallel scoring rows with keys and miscellaneous sources in the wings.
        constexpr std::array scoringDepth { 0.18f, 0.41f, 0.65f, 0.82f, 0.25f, 0.49f };
        return validPosition ({ centre + spread * (centre == 0.0f ? 0.84f : 0.17f),
                                scoringDepth[(size_t) section] + (float) row * 0.053f });
    }
    return validPosition ({ stagger * (centre + spread * (centre == 0.0f ? 0.77f : 0.19f)),
        depth[(size_t) section] * stagger + (float) row * 0.053f });
}

inline float sendDb (Position position, float trimDb)
{
    return bounded (trimDb, -60.0f, 0.0f, -14.0f) + (validPosition (position).depth - 1.0f) * 10.0f;
}

enum class Surface { wood, plaster, stone, carpet, absorber, count };
inline constexpr std::array surfaceNames { "Wood", "Plaster", "Stone", "Carpet", "Acoustic panels" };
inline constexpr std::array bandNames { "125 Hz", "250 Hz", "500 Hz", "1 kHz", "2 kHz", "4 kHz" };
inline constexpr std::array absorption {
    std::array { 0.10f, 0.09f, 0.08f, 0.08f, 0.09f, 0.11f },
    std::array { 0.02f, 0.03f, 0.04f, 0.05f, 0.06f, 0.07f },
    std::array { 0.01f, 0.01f, 0.02f, 0.02f, 0.02f, 0.03f },
    std::array { 0.03f, 0.08f, 0.24f, 0.46f, 0.55f, 0.62f },
    std::array { 0.18f, 0.38f, 0.64f, 0.78f, 0.82f, 0.83f }
};

enum class Quality { eco, standard, high };
inline constexpr std::array qualityNames { "Eco", "Standard", "High" };

struct ReverbShape
{
    float decay = 1, size = 1, attack = .45f, diffusion = .72f, density = .8f;
    float motionDepthMs = .42f, motionRate = .38f, width = 1, lowEq = 0, highEq = 0;
    float mix = .32f;
    Quality quality = Quality::standard;
    bool operator== (const ReverbShape&) const = default;
};

inline ReverbShape validate (ReverbShape shape)
{
    shape.decay = bounded (shape.decay, .25f, 3, 1);
    shape.size = bounded (shape.size, .5f, 1.5f, 1);
    shape.attack = bounded (shape.attack, 0, 1, .45f);
    shape.diffusion = bounded (shape.diffusion, 0, 1, .72f);
    shape.density = bounded (shape.density, 0, 1, .8f);
    shape.motionDepthMs = bounded (shape.motionDepthMs, 0, 1.3f, .42f);
    shape.motionRate = bounded (shape.motionRate, .07f, 1.25f, .38f);
    shape.width = bounded (shape.width, 0, 1.4f, 1);
    shape.lowEq = bounded (shape.lowEq, -12, 6, 0);
    shape.highEq = bounded (shape.highEq, -12, 6, 0);
    shape.mix = bounded (shape.mix, 0, 1, .32f);
    if ((unsigned) shape.quality >= qualityNames.size()) shape.quality = Quality::standard;
    return shape;
}

struct Room
{
    float width = 15, length = 21, height = 8, treatment = 0.22f;
    Surface wall = Surface::wood, floor = Surface::wood, ceiling = Surface::plaster;
    std::array<float, 6> decayTrim { 1, 1, 1, 1, 1, 1 };
    float early = 0.40f, late = 0.62f, gain = -12;
    bool bypass = false;
    float preDelayMs = 0.0f;
    ReverbShape shape {};
    bool operator== (const Room&) const = default;
};

inline Surface validSurface (Surface s)
{
    return (int) s >= 0 && s < Surface::count ? s : Surface::wood;
}

inline Room validate (Room r)
{
    r.width = bounded (r.width, 3, 80, 15);
    r.length = bounded (r.length, 3, 80, 21);
    r.height = bounded (r.height, 2.2f, 35, 8);
    r.treatment = bounded (r.treatment, 0, 1, 0.22f);
    r.wall = validSurface (r.wall);
    r.floor = validSurface (r.floor);
    r.ceiling = validSurface (r.ceiling);
    for (float& trim : r.decayTrim) trim = bounded (trim, 0.25f, 2, 1);
    r.early = bounded (r.early, 0, 1, 0.40f);
    r.late = bounded (r.late, 0, 1, 0.62f);
    r.gain = bounded (r.gain, -60, 0, -12);
    r.preDelayMs = bounded (r.preDelayMs, 0, 100, 0);
    r.shape = validate (r.shape);
    return r;
}

inline std::array<float, 6> predictDecay (Room raw)
{
    const auto r = validate (raw);
    const float floor = r.width * r.length, walls = 2.0f * (r.width + r.length) * r.height;
    const float area = 2.0f * floor + walls;
    std::array<float, 6> decay;
    for (size_t band = 0; band < decay.size(); ++band)
    {
        const auto a = [band] (Surface s) { return absorption[(size_t) s][band]; };
        const float mean = (floor * (a (r.floor) + a (r.ceiling))
            + walls * std::lerp (a (r.wall), a (Surface::absorber), r.treatment)) / area;
        // Eyring diffuse-field estimate. Bounded targets feed the algorithmic tank.
        decay[band] = std::clamp (0.161f * floor * r.height / (-area * std::log1p (-mean))
                                 * r.decayTrim[band], 0.20f, 12.0f);
    }
    return decay;
}

struct FactoryRoom { const char* name; const char* category; Room room; const char* description = ""; };

inline constexpr Room venue (float w, float l, float h, float treatment, Surface wall, Surface floor,
                             Surface ceiling, float early, float late, float predelay, ReverbShape shape = {})
{
    return { w, l, h, treatment, wall, floor, ceiling, { 1, 1, 1, 1, 1, 1 }, early, late, -12,
        false, predelay, shape };
}
inline constexpr std::array factoryRooms {
    FactoryRoom { "Wood Concert Hall", "Concert halls", { 24, 34, 14, 0.28f, Surface::wood, Surface::wood, Surface::plaster,
        { .73f, .87f, 1.0f, 1.0f, .91f, .79f }, .40f, .67f, -12.0f, false, 16.0f } },
    FactoryRoom { "Chamber Hall", "Chamber halls", {} },
    FactoryRoom { "Recital Room", "Recital rooms", { 10, 15, 5, 0.31f } },
    FactoryRoom { "Scoring Stage", "Scoring stages", { 22, 27, 10, 0.53f, Surface::wood, Surface::wood, Surface::plaster,
        { .68f, .77f, .86f, .86f, .81f, .74f }, .61f, .49f, -12.0f, false, 7.0f } },
    FactoryRoom { "Treated Studio", "Recording studios", { 8, 10, 3.5f, 0.65f } },
    FactoryRoom { "Stone Nave", "Churches and cathedrals", { 17, 37, 18, 0.16f, Surface::stone, Surface::stone, Surface::stone,
        { .58f, .76f, .90f, 1.0f, .92f, .79f }, .36f, .71f, -12.0f, false, 24.0f } },
    FactoryRoom { "Small Live Room", "Small rooms", { 5, 7, 3, 0.44f, Surface::plaster, Surface::carpet } },
    FactoryRoom { "Small Recording Studio", "Recording", venue (5, 6, 2.7f, .58f, Surface::plaster, Surface::carpet, Surface::absorber, .51f, .28f, 2), "Short, close and subdued." },
    FactoryRoom { "Large Live Room", "Recording", venue (9, 14, 5, .25f, Surface::plaster, Surface::wood, Surface::wood, .64f, .44f, 4), "Lively walls for drums and ensembles." },
    FactoryRoom { "Wooden Recording Room", "Recording", venue (8, 11, 4, .32f, Surface::wood, Surface::wood, Surface::wood, .62f, .43f, 6), "Short wood reflections with a warm tail." },
    FactoryRoom { "Large Scoring Stage", "Recording", venue (31, 39, 13, .52f, Surface::wood, Surface::wood, Surface::absorber, .57f, .62f, 13), "A broad stage with controlled high-frequency decay." },
    FactoryRoom { "Intimate Scoring Stage", "Recording", venue (16, 22, 7, .47f, Surface::wood, Surface::wood, Surface::plaster, .66f, .49f, 5), "Close seating with clear early arrivals." },
    FactoryRoom { "Small Chamber Room", "Classical", venue (9, 13, 5, .28f, Surface::wood, Surface::wood, Surface::plaster, .63f, .52f, 6), "Close walls and short ensemble support." },
    FactoryRoom { "Large Chamber Hall", "Classical", venue (16, 23, 9, .33f, Surface::plaster, Surface::wood, Surface::plaster, .52f, .59f, 11), "A deeper chamber acoustic for larger groups." },
    FactoryRoom { "Recital Hall", "Classical", venue (14, 20, 7, .37f, Surface::wood, Surface::wood, Surface::plaster, .59f, .51f, 9), "Clear solo and small-ensemble articulation." },
    FactoryRoom { "Classical Concert Hall", "Classical", venue (22, 30, 12, .31f, Surface::plaster, Surface::wood, Surface::plaster, .52f, .68f, 16), "Balanced stage support and a distinct hall tail." },
    FactoryRoom { "Traditional Symphonic Hall", "Classical", venue (25, 39, 17, .29f, Surface::plaster, Surface::wood, Surface::plaster, .45f, .69f, 20), "A long, warm orchestral field." },
    FactoryRoom { "Modern Symphonic Hall", "Classical", venue (29, 33, 15, .38f, Surface::wood, Surface::wood, Surface::plaster, .49f, .66f, 15), "Broad seating and a more controlled decay." },
    FactoryRoom { "Large Romantic Concert Hall", "Classical", venue (32, 46, 20, .30f, Surface::plaster, Surface::wood, Surface::stone, .39f, .78f, 24), "Deep space and sustained orchestral bloom." },
    FactoryRoom { "Opera House", "Classical", venue (22, 29, 18, .43f, Surface::wood, Surface::wood, Surface::plaster, .53f, .60f, 12), "A tall room with a controlled tail for voices." },
    FactoryRoom { "Theater", "Classical", venue (18, 25, 10, .46f, Surface::plaster, Surface::carpet, Surface::absorber, .52f, .46f, 8), "An absorptive audience area for clear speech and music." },
    FactoryRoom { "Chapel", "Sacred", venue (9, 19, 8, .21f, Surface::stone, Surface::stone, Surface::plaster, .48f, .65f, 11), "Defined stone returns with a moderate tail." },
    FactoryRoom { "Church", "Sacred", venue (14, 32, 15, .18f, Surface::stone, Surface::stone, Surface::plaster, .41f, .73f, 20), "An elongated field for choir and sustained instruments." },
    FactoryRoom { "Stone Cathedral", "Sacred", venue (25, 58, 29, .22f, Surface::stone, Surface::stone, Surface::stone, .35f, .77f, 31), "Long receding reflections and an expansive tail." }
};

enum class Pattern { omni, cardioid, supercardioid, figureEight };
inline constexpr std::array patternNames { "Omnidirectional", "Cardioid", "Supercardioid", "Figure-eight" };
struct Mic
{
    Position position { 0, 0.08f };
    float height = 2.6f, azimuth = 0, elevation = 0, gain = 0, pan = 0, delay = 0;
    Pattern pattern = Pattern::cardioid;
    bool mute = false, solo = false;
    bool operator== (const Mic&) const = default;
};

inline Mic validate (Mic m)
{
    m.position = validPosition (m.position);
    m.height = bounded (m.height, 0.25f, 20, 2.6f);
    m.azimuth = bounded (m.azimuth, -180, 180, 0);
    m.elevation = bounded (m.elevation, -90, 90, 0);
    m.gain = bounded (m.gain, -60, 12, 0);
    m.pan = bounded (m.pan, -1, 1, 0);
    m.delay = bounded (m.delay, -30, 30, 0);
    if ((int) m.pattern < 0 || (int) m.pattern >= (int) patternNames.size()) m.pattern = Pattern::cardioid;
    return m;
}

struct StereoPair
{
    float spacingMetres, angleDegrees;
    Pattern pattern;
    int capsules = 2;
    float depth = .08f, height = 2.6f;
};
inline constexpr std::array stereoNames { "XY 90 degrees", "ORTF 17 cm / 110 degrees", "Spaced omnis 2 m",
    "Blumlein 90 degrees", "Decca Tree-inspired", "Main stereo pair", "Wide outriggers", "Stage microphones", "Spot microphone" };
inline constexpr std::array stereoPairs {
    StereoPair { 0.0f, 90, Pattern::cardioid }, StereoPair { 0.17f, 110, Pattern::cardioid },
    StereoPair { 2.0f, 0, Pattern::omni }, StereoPair { 0.0f, 90, Pattern::figureEight },
    StereoPair { 2.f, 0, Pattern::omni, 3, .09f, 3.2f },
    StereoPair { .40f, 90, Pattern::cardioid, 2, .08f, 2.7f },
    StereoPair { 6.f, 0, Pattern::omni, 2, .13f, 3.1f },
    StereoPair { 3.1f, 75, Pattern::cardioid, 2, .39f, 2.1f },
    StereoPair { 0, 0, Pattern::supercardioid, 1, .53f, 1.7f }
};
}
