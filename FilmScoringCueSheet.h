// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "FilmScoringCore.h"
#include <iomanip>
#include <sstream>

namespace studio::film
{
inline std::string cueTimecode (int64_t frame, Rate rate)
{
    const auto tc = fromFrames (frame, rate);
    std::ostringstream out;
    out << (tc.negative ? "-" : "") << std::setfill ('0')
        << std::setw (2) << tc.hours << ':' << std::setw (2) << tc.minutes
        << ':' << std::setw (2) << tc.seconds << (info (rate).drop ? ';' : ':')
        << std::setw (2) << tc.frames;
    return out.str();
}

inline std::string csvText (const std::string& raw)
{
    // Quote every user field, including embedded newlines and double quotes.
    // Spreadsheet applications can evaluate formula-like content even when
    // CSV fields are quoted. Protect leading whitespace + formula prefixes.
    size_t n = 0;
    while (n < raw.size() && (raw[n] == ' ' || raw[n] == '\t')) ++n;
    const bool formula = n < raw.size() && (raw[n] == '=' || raw[n] == '+'
                                        || raw[n] == '-' || raw[n] == '@');
    std::string out = "\"";
    if (formula) out += "'";
    for (char c : raw)
    {
        if (c == '"') out += "\"\"";
        else out += c;
    }
    out += "\"";
    return out;
}

// Entirely offline, independent of the media decoder and DAW automation.
// Export uses source-picture SMPTE, NOT bar/beat positions or presentation time.
inline std::string cueSheetCSV (const Project& project)
{
    std::vector<const Cue*> ordered;
    ordered.reserve (project.cues.size());
    for (const auto& cue : project.cues) ordered.push_back (&cue);
    std::stable_sort (ordered.begin(), ordered.end(), [] (const Cue* a, const Cue* b)
    {
        if (a->startPictureFrame != b->startPictureFrame)
            return a->startPictureFrame < b->startPictureFrame;
        return a->id < b->id;
    });

    std::ostringstream out;
    out << "Cue ID,Title,Start SMPTE,End SMPTE,Start Picture Frame,End Picture Frame,Duration Seconds,Notes\r\n";
    for (const Cue* cue : ordered)
    {
        out << csvText (cue->id) << ',' << csvText (cue->title) << ','
            << cueTimecode (cue->startPictureFrame, project.rate) << ',';
        if (cue->endPictureFrame)
        {
            out << cueTimecode (*cue->endPictureFrame, project.rate) << ','
                << cue->startPictureFrame << ',' << *cue->endPictureFrame << ','
                << std::fixed << std::setprecision (3)
                << frameSeconds (*cue->endPictureFrame - cue->startPictureFrame, project.rate);
        }
        else
        {
            out << ',' << cue->startPictureFrame << ",,";
        }
        out << ',' << csvText (cue->notes) << "\r\n";
    }
    return out.str();
}
} // namespace studio::film
