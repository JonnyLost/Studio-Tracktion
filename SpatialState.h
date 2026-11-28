// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "SpatialModel.h"
#include "SpatialStage.h"
#include <juce_data_structures/juce_data_structures.h>

namespace studio::spatial
{
inline constexpr int version = 1, physicalVersion = 2;
inline const juce::Identifier sceneType { "STUDIO_SPATIAL" }, roomType { "ROOM" }, libraryType { "ROOM_LIBRARY" },
    seatType { "STUDIO_STAGE_SEAT" }, microphonesType { "MICROPHONES" },
    micType { "MIC" }, setupsType { "MIC_SETUPS" };

inline juce::ValueTree writeRoom (Room r, const juce::String& name)
{
    r = validate (r);
    juce::ValueTree v (roomType);
    v.setProperty ("name", name, nullptr).setProperty ("width", r.width, nullptr).setProperty ("length", r.length, nullptr)
     .setProperty ("height", r.height, nullptr).setProperty ("treatment", r.treatment, nullptr)
     .setProperty ("wall", (int) r.wall, nullptr).setProperty ("floor", (int) r.floor, nullptr)
     .setProperty ("ceiling", (int) r.ceiling, nullptr).setProperty ("early", r.early, nullptr)
     .setProperty ("late", r.late, nullptr).setProperty ("gain", r.gain, nullptr).setProperty ("bypass", r.bypass, nullptr)
     .setProperty ("preDelayMs", r.preDelayMs, nullptr);
    const auto& a = r.shape;
    v.setProperty ("decayScale", a.decay, nullptr).setProperty ("sizeScale", a.size, nullptr)
     .setProperty ("attack", a.attack, nullptr).setProperty ("diffusion", a.diffusion, nullptr)
     .setProperty ("density", a.density, nullptr).setProperty ("motionDepth", a.motionDepthMs, nullptr)
     .setProperty ("motionRate", a.motionRate, nullptr).setProperty ("stereoWidth", a.width, nullptr)
     .setProperty ("lowEq", a.lowEq, nullptr).setProperty ("highEq", a.highEq, nullptr)
     .setProperty ("mix", a.mix, nullptr).setProperty ("quality", (int) a.quality, nullptr);
    for (int i = 0; i < 6; ++i) v.setProperty ("band" + juce::String (i), r.decayTrim[(size_t) i], nullptr);
    return v;
}

inline Room readRoom (const juce::ValueTree& v)
{
    Room r;
    if (! v.hasType (roomType)) return r;
    r.width = (float) v.getProperty ("width", r.width); r.length = (float) v.getProperty ("length", r.length);
    r.height = (float) v.getProperty ("height", r.height); r.treatment = (float) v.getProperty ("treatment", r.treatment);
    r.wall = (Surface) (int) v.getProperty ("wall", (int) r.wall);
    r.floor = (Surface) (int) v.getProperty ("floor", (int) r.floor);
    r.ceiling = (Surface) (int) v.getProperty ("ceiling", (int) r.ceiling);
    r.early = (float) v.getProperty ("early", r.early); r.late = (float) v.getProperty ("late", r.late);
    r.preDelayMs = (float) v.getProperty ("preDelayMs", r.preDelayMs);
    r.gain = (float) v.getProperty ("gain", r.gain); r.bypass = (bool) v.getProperty ("bypass", r.bypass);
    auto& a = r.shape;
    a.decay = (float) v.getProperty ("decayScale", a.decay); a.size = (float) v.getProperty ("sizeScale", a.size);
    a.attack = (float) v.getProperty ("attack", a.attack); a.diffusion = (float) v.getProperty ("diffusion", a.diffusion);
    a.density = (float) v.getProperty ("density", a.density); a.motionDepthMs = (float) v.getProperty ("motionDepth", a.motionDepthMs);
    a.motionRate = (float) v.getProperty ("motionRate", a.motionRate); a.width = (float) v.getProperty ("stereoWidth", a.width);
    a.lowEq = (float) v.getProperty ("lowEq", a.lowEq); a.highEq = (float) v.getProperty ("highEq", a.highEq);
    a.mix = (float) v.getProperty ("mix", a.mix); a.quality = (Quality) (int) v.getProperty ("quality", (int) a.quality);
    for (int i = 0; i < 6; ++i) r.decayTrim[(size_t) i] = (float) v.getProperty ("band" + juce::String (i), 1.0f);
    return validate (r);
}

struct Seat
{
    Position position;
    Section section = Section::other;
    float sendTrim = -14.0f;
    bool operator== (const Seat&) const = default;
};

inline juce::ValueTree writeSeat (const juce::String& trackKey, Seat s)
{
    const auto p = validPosition (s.position);
    return juce::ValueTree (seatType).setProperty ("version", version, nullptr).setProperty ("trackKey", trackKey, nullptr)
        .setProperty ("x", p.x, nullptr).setProperty ("depth", p.depth, nullptr)
        .setProperty ("section", (int) s.section, nullptr).setProperty ("trim", sendDb ({ 0, 1 }, s.sendTrim), nullptr);
}

inline Seat readSeat (const juce::ValueTree& v, Section inferred = Section::other)
{
    const auto kind = (int) v.getProperty ("section", (int) inferred);
    return { validPosition ({ (float) v.getProperty ("x", 0.0f), (float) v.getProperty ("depth", 0.35f) }),
             kind >= 0 && kind < (int) sectionNames.size() ? (Section) kind : inferred,
             sendDb ({ 0, 1 }, (float) v.getProperty ("trim", -14.0f)) };
}

inline bool isPhysicalScene (const juce::ValueTree& scene)
{
    return (int) scene.getProperty ("version", 0) == physicalVersion;
}

inline void writeMetres (juce::ValueTree node, Room room, Position position, juce::UndoManager& undo)
{
    const auto point = stage::sourcePoint (room, position, 1.2f);
    node.setProperty ("xMetres", point.x, &undo).setProperty ("yMetres", point.y, &undo);
}

inline Seat readPhysicalSeat (const juce::ValueTree& node, Room room, Section inferred = Section::other)
{
    auto seat = readSeat (node, inferred);
    if (node.hasProperty ("xMetres") && node.hasProperty ("yMetres"))
    {
        const auto area = stage::stageBounds (room);
        seat.position = validPosition ({ (float) ((double) node["xMetres"] / area.halfWidth),
            (float) (((double) node["yMetres"] - area.front) / (area.rear - area.front)) });
    }
    return seat;
}

inline stage::Source readSource (const juce::ValueTree& node)
{
    stage::Source source;
    source.height = (float) node.getProperty ("sourceHeight", source.height);
    source.orientation = (float) node.getProperty ("orientation", source.orientation);
    source.width = (float) node.getProperty ("sourceWidth", source.width);
    source.directDb = (float) node.getProperty ("directDb", source.directDb);
    source.direction = (float) node.getProperty ("directivity", source.direction);
    source.early = (float) node.getProperty ("sourceEarly", source.early);
    source.family = (stage::Family) (int) node.getProperty ("radiationFamily", (int) source.family);
    return stage::validate (source);
}

inline void writeSource (juce::ValueTree node, stage::Source raw, juce::UndoManager& undo)
{
    const auto s = stage::validate (raw);
    node.setProperty ("sourceHeight", s.height, &undo).setProperty ("orientation", s.orientation, &undo)
        .setProperty ("sourceWidth", s.width, &undo).setProperty ("directDb", s.directDb, &undo)
        .setProperty ("directivity", s.direction, &undo).setProperty ("sourceEarly", s.early, &undo)
        .setProperty ("radiationFamily", (int) s.family, &undo);
}

inline juce::ValueTree writeMic (const juce::String& key, const juce::String& name, Mic m)
{
    m = validate (m);
    return juce::ValueTree (micType).setProperty ("key", key, nullptr).setProperty ("name", name, nullptr)
        .setProperty ("x", m.position.x, nullptr).setProperty ("depth", m.position.depth, nullptr)
        .setProperty ("height", m.height, nullptr).setProperty ("azimuth", m.azimuth, nullptr)
        .setProperty ("elevation", m.elevation, nullptr).setProperty ("pattern", (int) m.pattern, nullptr)
        .setProperty ("gain", m.gain, nullptr).setProperty ("pan", m.pan, nullptr).setProperty ("delay", m.delay, nullptr)
        .setProperty ("mute", m.mute, nullptr).setProperty ("solo", m.solo, nullptr);
}

inline Mic readMic (const juce::ValueTree& v)
{
    Mic m;
    if (! v.hasType (micType)) return m;
    m.position = { (float) v.getProperty ("x", m.position.x), (float) v.getProperty ("depth", m.position.depth) };
    m.height = (float) v.getProperty ("height", m.height); m.azimuth = (float) v.getProperty ("azimuth", m.azimuth);
    m.elevation = (float) v.getProperty ("elevation", m.elevation); m.pattern = (Pattern) (int) v.getProperty ("pattern", (int) m.pattern);
    m.gain = (float) v.getProperty ("gain", m.gain); m.pan = (float) v.getProperty ("pan", m.pan);
    m.delay = (float) v.getProperty ("delay", m.delay); m.mute = (bool) v.getProperty ("mute", m.mute);
    m.solo = (bool) v.getProperty ("solo", m.solo);
    return validate (m);
}

inline void replaceChild (juce::ValueTree parent, juce::ValueTree previous, juce::ValueTree next, juce::UndoManager& undo)
{
    const int index = parent.indexOf (previous);
    if (previous.isValid()) parent.removeChild (previous, &undo);
    parent.addChild (next, index, &undo);
}
}
