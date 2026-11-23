// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace studio::film
{
// Frame indexes are signed so preroll and negative picture offsets remain representable.
enum class Rate { fps23976, fps24, fps25, fps2997NDF, fps2997DF, fps30 };

struct RateInfo { int numerator, denominator, nominal; bool drop; };
inline constexpr RateInfo info (Rate rate)
{
    switch (rate)
    {
        case Rate::fps23976: return { 24000, 1001, 24, false };
        case Rate::fps24: return { 24, 1, 24, false };
        case Rate::fps25: return { 25, 1, 25, false };
        case Rate::fps2997NDF: return { 30000, 1001, 30, false };
        case Rate::fps2997DF: return { 30000, 1001, 30, true };
        case Rate::fps30: return { 30, 1, 30, false };
    }
    return { 24, 1, 24, false };
}

struct Timecode { bool negative = false; int hours = 0, minutes = 0, seconds = 0, frames = 0; };

inline std::optional<int64_t> toFrames (Timecode tc, Rate rate)
{
    const auto r = info (rate);
    if (tc.hours < 0 || tc.hours > 23 || tc.minutes < 0 || tc.minutes >= 60
        || tc.seconds < 0 || tc.seconds >= 60 || tc.frames < 0 || tc.frames >= r.nominal)
        return {};
    if (r.drop && tc.minutes % 10 != 0 && tc.seconds == 0 && tc.frames < 2)
        return {}; // These two labels do not exist at non-tenth minute boundaries.
    const int64_t totalMinutes = tc.hours * 60 + tc.minutes;
    int64_t frames = ((tc.hours * 3600LL + tc.minutes * 60LL + tc.seconds) * r.nominal + tc.frames);
    if (r.drop) frames -= 2 * (totalMinutes - totalMinutes / 10);
    return tc.negative ? -frames : frames;
}

inline Timecode fromFrames (int64_t frame, Rate rate)
{
    const auto r = info (rate);
    const bool negative = frame < 0;
    // Avoid abs(INT64_MIN) undefined behavior; limit values to a 24-hour label cycle.
    const int64_t cycle = r.drop ? 2589408 : int64_t (r.nominal) * 86400;
    const uint64_t magnitude = negative ? uint64_t (-(frame + 1)) + 1ULL : uint64_t (frame);
    int64_t f = (int64_t) (magnitude % (uint64_t) cycle);
    if (r.drop)
    {
        const int64_t tenMinutes = 17982;
        const int64_t block = f / tenMinutes;
        const int64_t remainder = f % tenMinutes;
        f += 18 * block + (remainder >= 1800 ? 2 * (1 + (remainder - 1800) / 1798) : 0);
    }
    Timecode tc;
    tc.negative = negative;
    tc.frames = int (f % r.nominal);
    f /= r.nominal;
    tc.seconds = int (f % 60);
    tc.minutes = int (f / 60 % 60);
    tc.hours = int (f / 3600);
    return tc;
}

inline double frameSeconds (int64_t frames, Rate rate)
{
    const auto r = info (rate);
    return double (frames) * r.denominator / r.numerator;
}

inline std::optional<int64_t> secondsToFrames (double seconds, Rate rate)
{
    const auto r = info (rate);
    const double f = seconds * r.numerator / r.denominator;
    if (! std::isfinite (f) || std::abs (f) > double (std::numeric_limits<int64_t>::max()) / 2)
        return {};
    return (int64_t) std::llround (f);
}

struct Spot { std::string id, cue, name, notes; int64_t pictureFrame = 0; };

// Cue boundaries belong to source picture frames, never musical ticks.
// An unset end means the composer has not yet spotted the cue out.
struct Cue
{
    std::string id, title, notes;
    int64_t startPictureFrame = 0;
    std::optional<int64_t> endPictureFrame;
};
struct Project
{
    Rate rate = Rate::fps24;
    int64_t pictureStartFrame = 0; // Timeline frame where source picture frame zero starts.
    std::string picturePath;
    std::vector<Spot> spots;
    std::vector<Cue> cues;

    double timelineSeconds (int64_t pictureFrame) const
    {
        return frameSeconds (pictureFrame + pictureStartFrame, rate);
    }
    std::optional<int64_t> pictureFrameAt (double timelineTime) const
    {
        if (auto frame = secondsToFrames (timelineTime, rate)) return *frame - pictureStartFrame;
        return {};
    }
    bool addSpot (Spot spot)
    {
        if (spot.id.empty() || std::any_of (spots.begin(), spots.end(), [&] (const auto& s) { return s.id == spot.id; }))
            return false;
        spots.push_back (std::move (spot));
        return true;
    }
    const Spot* find (const std::string& id) const
    {
        const auto it = std::find_if (spots.begin(), spots.end(), [&] (const auto& s) { return s.id == id; });
        return it == spots.end() ? nullptr : &*it;
    }

    const Cue* findCue (const std::string& id) const
    {
        const auto it = std::find_if (cues.begin(), cues.end(), [&] (const auto& cue) { return cue.id == id; });
        return it == cues.end() ? nullptr : &*it;
    }

    bool validCue (const Cue& cue, const std::string& updatingId = {}) const
    {
        if (cue.id.empty() || cue.startPictureFrame < 0
            || (cue.endPictureFrame && *cue.endPictureFrame <= cue.startPictureFrame))
            return false;
        for (const auto& other : cues)
            if (other.id == cue.id && other.id != updatingId)
                return false;
        return true;
    }

    bool addCue (Cue cue)
    {
        if (! validCue (cue)) return false;
        cues.push_back (std::move (cue));
        return true;
    }

    bool updateCue (const std::string& currentId, Cue next)
    {
        const auto it = std::find_if (cues.begin(), cues.end(),
                                     [&] (const auto& cue) { return cue.id == currentId; });
        if (it == cues.end() || ! validCue (next, currentId)) return false;
        *it = std::move (next);
        return true;
    }

    bool removeCue (const std::string& id)
    {
        const auto it = std::find_if (cues.begin(), cues.end(),
                                     [&] (const auto& cue) { return cue.id == id; });
        if (it == cues.end()) return false;
        cues.erase (it);
        return true;
    }
};

struct TempoPoint { double startSecond = 0, bpm = 120; int numerator = 4, denominator = 4; };

// Tempo points are ordered in absolute timeline seconds. Beats are quarter-note beats.
class TempoMap
{
public:
    explicit TempoMap (std::vector<TempoPoint> points) : entries (std::move (points))
    {
        std::sort (entries.begin(), entries.end(), [] (auto a, auto b) { return a.startSecond < b.startSecond; });
    }

    bool valid() const
    {
        if (entries.empty() || entries.front().startSecond != 0) return false;
        for (size_t i = 0; i < entries.size(); ++i)
        {
            const auto& p = entries[i];
            if (! std::isfinite (p.startSecond) || ! std::isfinite (p.bpm) || p.bpm <= 0
                || p.numerator <= 0 || p.denominator <= 0 || (p.denominator & (p.denominator - 1)) != 0
                || (i > 0 && p.startSecond <= entries[i - 1].startSecond)) return false;
        }
        return true;
    }

    std::optional<double> beatAt (double second) const
    {
        if (! valid() || ! std::isfinite (second)) return {};
        double beats = 0;
        for (size_t i = 0; i < entries.size(); ++i)
        {
            const auto& p = entries[i];
            if (second < p.startSecond) break;
            const double end = i + 1 < entries.size() ? std::min (second, entries[i + 1].startSecond) : second;
            beats += (end - p.startSecond) * p.bpm / 60;
            if (i + 1 == entries.size() || second < entries[i + 1].startSecond) break;
        }
        if (second < 0) beats = second * entries.front().bpm / 60;
        return beats;
    }

    std::optional<double> secondAt (double beat) const
    {
        if (! valid() || ! std::isfinite (beat)) return {};
        if (beat < 0) return beat * 60 / entries.front().bpm;
        double elapsedBeats = 0;
        for (size_t i = 0; i < entries.size(); ++i)
        {
            const auto& p = entries[i];
            if (i + 1 == entries.size()) return p.startSecond + (beat - elapsedBeats) * 60 / p.bpm;
            const double segmentBeats = (entries[i + 1].startSecond - p.startSecond) * p.bpm / 60;
            if (beat < elapsedBeats + segmentBeats) return p.startSecond + (beat - elapsedBeats) * 60 / p.bpm;
            elapsedBeats += segmentBeats;
        }
        return {};
    }

    // 1-based bar and beat, plus fractional position in the beat. Meter changes
    // begin a new bar. An incomplete prior bar is intentionally not carried over.
    struct BarBeat { int64_t bar = 1; int beat = 1; double fraction = 0; };
    std::optional<BarBeat> barBeatAt (double second) const
    {
        if (! valid() || ! std::isfinite (second) || second < 0) return {};
        int64_t completedBars = 0;
        double quartersInMeter = 0;
        for (size_t i = 0; i < entries.size(); ++i)
        {
            const auto& p = entries[i];
            if (i > 0 && (p.numerator != entries[i - 1].numerator
                          || p.denominator != entries[i - 1].denominator))
            {
                // A signature change begins a new bar; a tempo-only change
                // must NOT discard the accumulated position within a bar.
                const auto& prior = entries[i - 1];
                const double oldBarQuarters = double (prior.numerator) * 4 / prior.denominator;
                completedBars += (int64_t) std::ceil (quartersInMeter / oldBarQuarters - 1e-9);
                quartersInMeter = 0;
            }
            const double boundary = i + 1 < entries.size() ? entries[i + 1].startSecond : second;
            const double elapsed = std::max (0.0, std::min (second, boundary) - p.startSecond);
            quartersInMeter += elapsed * p.bpm / 60;
            if (i + 1 == entries.size() || second < boundary)
            {
                const double quartersPerBar = double (p.numerator) * 4 / p.denominator;
                const double quartersPerBeat = 4.0 / p.denominator;
                const auto bars = (int64_t) std::floor (quartersInMeter / quartersPerBar + 1e-9);
                const double into = std::max (0.0, quartersInMeter - bars * quartersPerBar);
                const auto unitIndex = (int) std::floor (into / quartersPerBeat + 1e-9);
                return BarBeat { completedBars + bars + 1, unitIndex + 1,
                                 std::max (0.0, into / quartersPerBeat - unitIndex) };
            }
        }
        return {};
    }

private:
    std::vector<TempoPoint> entries;
};

inline std::optional<double> suggestedBpm (double secondsBetweenHits, double quarterBeats)
{
    if (! std::isfinite (secondsBetweenHits) || ! std::isfinite (quarterBeats)
        || secondsBetweenHits <= 0 || quarterBeats <= 0) return {};
    const double bpm = 60 * quarterBeats / secondsBetweenHits;
    if (bpm <= 0 || ! std::isfinite (bpm)) return {};
    return bpm;
}

struct CueTiming
{
    double startSecond = 0, endSecond = 0, durationSeconds = 0;
    double startBeat = 0, endBeat = 0;
    double suggestedTempo = 0;
};

inline std::optional<CueTiming> calculateCue (const Project& project, const TempoMap& map,
                                               const std::string& startId, const std::string& endId,
                                               double desiredQuarterBeats)
{
    const auto* start = project.find (startId);
    const auto* end = project.find (endId);
    if (! start || ! end || end->pictureFrame <= start->pictureFrame) return {};
    CueTiming cue;
    cue.startSecond = project.timelineSeconds (start->pictureFrame);
    cue.endSecond = project.timelineSeconds (end->pictureFrame);
    cue.durationSeconds = cue.endSecond - cue.startSecond;
    auto firstBeat = map.beatAt (cue.startSecond);
    auto lastBeat = map.beatAt (cue.endSecond);
    auto suggestion = suggestedBpm (cue.durationSeconds, desiredQuarterBeats);
    if (! firstBeat || ! lastBeat || ! suggestion) return {};
    cue.startBeat = *firstBeat;
    cue.endBeat = *lastBeat;
    cue.suggestedTempo = *suggestion;
    return cue;
}
} // namespace studio::film
