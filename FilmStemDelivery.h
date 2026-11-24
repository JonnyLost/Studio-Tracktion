// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <string>
#include <string_view>
#include <vector>

namespace studio::film
{
// Track assignments are explicit and keyed by Tracktion's stable EditItemID.
// No instrument-name heuristics, global track muting or auto-changing of routes.
enum class StemFamily
{
    unassigned = 0, strings, woodwinds, brass, percussion,
    keyboards, choir, electronic, other
};

inline constexpr std::array<StemFamily, 8> stemFamilies {
    StemFamily::strings, StemFamily::woodwinds, StemFamily::brass,
    StemFamily::percussion, StemFamily::keyboards, StemFamily::choir,
    StemFamily::electronic, StemFamily::other
};

inline constexpr const char* stemFamilyName (StemFamily family)
{
    switch (family)
    {
        case StemFamily::strings:     return "Strings";
        case StemFamily::woodwinds:   return "Woodwinds";
        case StemFamily::brass:       return "Brass";
        case StemFamily::percussion:  return "Percussion";
        case StemFamily::keyboards:   return "Keyboards & Harps";
        case StemFamily::choir:       return "Choir & Voices";
        case StemFamily::electronic:  return "Synths & Hybrid";
        case StemFamily::other:       return "Other";
        case StemFamily::unassigned:  return "Unassigned";
    }
    return "Unassigned";
}

inline constexpr const char* stemFamilyFileName (StemFamily family)
{
    switch (family)
    {
        case StemFamily::strings:     return "Strings";
        case StemFamily::woodwinds:   return "Woodwinds";
        case StemFamily::brass:       return "Brass";
        case StemFamily::percussion:  return "Percussion";
        case StemFamily::keyboards:   return "Keyboards-Harps";
        case StemFamily::choir:       return "Choir-Voices";
        case StemFamily::electronic:  return "Synths-Hybrid";
        case StemFamily::other:       return "Other";
        case StemFamily::unassigned:  return "Unassigned";
    }
    return "Unassigned";
}

inline constexpr bool validStemFamily (int index)
{
    return index >= 0 && index <= static_cast<int> (StemFamily::other);
}

struct StemAssignment
{
    std::string trackId;
    StemFamily family = StemFamily::unassigned;
};

struct StemGroup
{
    StemFamily family;
    std::vector<std::string> trackIds;
};

inline bool validStemTrackId (std::string_view id)
{
    return ! id.empty() && id.size() <= 32
        && std::all_of (id.begin(), id.end(), [] (unsigned char ch) { return ch >= '0' && ch <= '9'; })
        && std::any_of (id.begin(), id.end(), [] (char ch) { return ch != '0'; });
}

// Assigning Unassigned removes the mapping. A track can belong to one
// family only; replacing an assignment never silently copies it to another.
inline bool setStemFamily (std::vector<StemAssignment>& assignments,
                           const std::string& trackId, StemFamily family)
{
    if (! validStemTrackId (trackId) || ! validStemFamily (static_cast<int> (family)))
        return false;
    assignments.erase (std::remove_if (assignments.begin(), assignments.end(),
        [&trackId] (const StemAssignment& entry) { return entry.trackId == trackId; }),
        assignments.end());
    if (family != StemFamily::unassigned)
        assignments.push_back ({ trackId, family });
    return true;
}

inline StemFamily familyForTrack (const std::vector<StemAssignment>& assignments,
                                  const std::string& id)
{
    for (const auto& entry : assignments)
        if (entry.trackId == id && validStemFamily (static_cast<int> (entry.family)))
            return entry.family;
    return StemFamily::unassigned;
}

// Only live tracks, in current Edit order, enter the batch. Stale metadata for
// deleted tracks never causes a stem to include all tracks accidentally.
inline std::vector<StemGroup> planFamilyStems (
    const std::vector<StemAssignment>& assignments,
    const std::vector<std::string>& liveTrackIds)
{
    std::vector<StemGroup> result;
    for (auto family : stemFamilies)
    {
        StemGroup group { family, {} };
        for (const auto& id : liveTrackIds)
        {
            if (! validStemTrackId (id) || familyForTrack (assignments, id) != family
                || std::find (group.trackIds.begin(), group.trackIds.end(), id) != group.trackIds.end())
                continue;
            group.trackIds.push_back (id);
        }
        if (! group.trackIds.empty())
            result.push_back (std::move (group));
    }
    return result;
}
} // namespace studio::film
