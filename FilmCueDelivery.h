// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "FilmScoringCore.h"
#include <cctype>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace studio::film
{
// A delivery is always anchored to source picture frames. Its audio timeline
// boundaries are derived on demand; tempo changes never change the cue range.
struct DeliveryRange
{
    std::string cueId;
    int64_t startFrame = 0;
    int64_t endFrame = 0;
    double timelineStart = 0;
    double timelineEnd = 0;
};

inline std::optional<DeliveryRange> deliveryRange (const Project& project, const Cue& cue)
{
    if (cue.id.empty() || cue.startPictureFrame < 0 || ! cue.endPictureFrame
        || *cue.endPictureFrame <= cue.startPictureFrame)
        return {};

    const double start = project.timelineSeconds (cue.startPictureFrame);
    const double end = project.timelineSeconds (*cue.endPictureFrame);
    if (! std::isfinite (start) || ! std::isfinite (end) || start < 0
        || end <= start || end - start > 24 * 3600.0)
        return {};

    return DeliveryRange { cue.id, cue.startPictureFrame, *cue.endPictureFrame, start, end };
}

// Sanitise labels for use as *one* filename component. Never accept a user's
// cue name as a directory path. Deterministic and locale-independent.
inline std::string safeDeliveryName (std::string_view input)
{
    std::string out;
    out.reserve (std::min<size_t> (input.size(), 72));
    bool separator = false;
    for (unsigned char ch : input)
    {
        if (out.size() >= 72) break;
        const bool allowed = (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z')
                          || (ch >= '0' && ch <= '9') || ch == '_' || ch == '-';
        if (allowed)
        {
            if (separator && ! out.empty() && out.back() != '-') out += '-';
            separator = false;
            out += char (ch);
        }
        else if (ch == ' ' || ch == '.' || ch == '/' || ch == '\\' || ch == ':'
                 || ch < 0x20 || ch == 0x7f)
        {
            separator = ! out.empty();
        }
    }
    while (! out.empty() && (out.back() == '-' || out.back() == '.')) out.pop_back();
    // Avoid Windows device-name collisions for projects moved to another OS.
    std::string upper = out;
    for (char& ch : upper) ch = char (std::toupper (static_cast<unsigned char> (ch)));
    const bool reserved = upper == "CON" || upper == "PRN" || upper == "AUX" || upper == "NUL"
                       || (upper.size() == 4 && (upper.substr (0, 3) == "COM"
                                                || upper.substr (0, 3) == "LPT")
                           && upper[3] >= '1' && upper[3] <= '9');
    if (reserved) out.insert (0, "Cue-");
    return out.empty() ? "Untitled" : out;
}

inline std::string deliveryStem (const Cue& cue)
{
    return safeDeliveryName (cue.id) + "-" + safeDeliveryName (cue.title);
}
} // namespace studio::film
