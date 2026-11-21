// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "SamplerEngine.h"
#include <algorithm>
#include <cctype>
#include <optional>
#include <string>

namespace studio::sampler
{
// Scientific pitch notation: MIDI 60 = C4. Only complete delimited tokens qualify.
inline std::optional<int> filenameNote (std::string name)
{
    if (auto dot = name.rfind ('.'); dot != std::string::npos) name.resize (dot);
    std::optional<int> result;
    for (std::size_t i = 0; i < name.size(); ++i)
    {
        if (i && std::isalnum (static_cast<unsigned char> (name[i - 1]))) continue;
        const auto letter = static_cast<char> (std::toupper (static_cast<unsigned char> (name[i])));
        const std::string letters = "C D EF G A B";
        const auto pitch = letters.find (letter);
        if (pitch == std::string::npos || letter == ' ') continue;
        auto j = i + 1;
        int accidental = 0;
        if (j < name.size() && (name[j] == '#' || name[j] == 'b')) accidental = name[j++] == '#' ? 1 : -1;
        int sign = 1;
        if (j < name.size() && name[j] == '-') { sign = -1; ++j; }
        if (j == name.size() || ! std::isdigit (static_cast<unsigned char> (name[j]))) continue;
        const int octave = (name[j++] - '0') * sign;
        if (j < name.size() && std::isalnum (static_cast<unsigned char> (name[j]))) continue;
        const int note = (octave + 1) * 12 + static_cast<int> (pitch) + accidental;
        if (note < 0 || note > 127) continue;
        if (result && *result != note) return {}; // Ambiguous filename.
        result = note;
    }
    return result;
}
// Explicit velocity suffixes, e.g. Piano_C4_v1-63.wav or C4_vel64-127.aiff.
inline std::optional<std::pair<int, int>> filenameVelocity (std::string name)
{
    for (auto& c : name) c = static_cast<char> (std::tolower (static_cast<unsigned char> (c)));
    for (std::size_t i = 0; i < name.size(); ++i)
    {
        if ((i && std::isalnum (static_cast<unsigned char> (name[i - 1]))) || name[i] != 'v') continue;
        auto j = i + (name.compare (i, 3, "vel") == 0 ? 3 : 1);
        const auto number = [&] (std::size_t& at)
        {
            int value = 0, digits = 0;
            while (at < name.size() && std::isdigit (static_cast<unsigned char> (name[at]))) { value = value * 10 + name[at++] - '0'; if (++digits > 3) return -1; }
            return digits ? value : -1;
        };
        const int low = number (j);
        if (j >= name.size() || name[j++] != '-') continue;
        const int high = number (j);
        if (low >= 1 && high >= low && high <= 127 && (j == name.size() || ! std::isalnum (static_cast<unsigned char> (name[j])))) return std::pair { low, high };
    }
    return {};
}
inline void spreadMapping (std::vector<Zone>& zones)
{
    std::vector<int> roots;
    for (const auto& z : zones) roots.push_back (z.root);
    std::sort (roots.begin(), roots.end());
    roots.erase (std::unique (roots.begin(), roots.end()), roots.end());
    for (auto& z : zones)
    {
        const auto i = static_cast<std::size_t> (std::lower_bound (roots.begin(), roots.end(), z.root) - roots.begin());
        z.low = i == 0 ? 0 : (roots[i - 1] + z.root) / 2 + 1;
        z.high = i + 1 == roots.size() ? 127 : (z.root + roots[i + 1]) / 2;
    }
}
}
