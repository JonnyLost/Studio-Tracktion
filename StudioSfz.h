// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cctype>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace studio::sfz
{
using Path = std::filesystem::path;
using Opcodes = std::map<std::string, std::string>;
struct Region { Opcodes ops; int line = 0; };
struct Document
{
    std::vector<Region> regions;
    std::set<std::string> warnings;
    std::string error;
};
using Loader = std::function<std::optional<std::string>(const Path&)>;
inline bool space (char c) { return std::isspace (static_cast<unsigned char> (c)) != 0; }
inline bool ident (char c) { return std::isalnum (static_cast<unsigned char> (c)) != 0 || c == '_'; }
inline std::string trim (std::string_view s)
{
    while (! s.empty() && space (s.front())) s.remove_prefix (1);
    while (! s.empty() && space (s.back())) s.remove_suffix (1);
    return std::string (s);
}
inline std::string lower (std::string s)
{
    for (auto& c : s) c = static_cast<char> (std::tolower (static_cast<unsigned char> (c)));
    return s;
}
inline Path path (std::string s)
{
    std::replace (s.begin(), s.end(), '\\', '/');
    return std::filesystem::u8path (s);
}
inline std::optional<int> integer (std::string_view s)
{
    if (! s.empty() && s.front() == '+') s.remove_prefix (1);
    int v = 0;
    auto r = std::from_chars (s.data(), s.data() + s.size(), v);
    if (r.ec != std::errc() || r.ptr != s.data() + s.size()) return {};
    return v;
}
inline std::optional<double> number (std::string_view s)
{
    // Locale independent, bounded decimal grammar (no NaN, infinity or suffix).
    if (s.empty()) return {};
    bool negative = false;
    if (s.front() == '+' || s.front() == '-') { negative = s.front() == '-'; s.remove_prefix (1); }
    double v = 0, scale = 1;
    bool dot = false, digit = false;
    for (char c : s)
    {
        if (c == '.' && ! dot) { dot = true; continue; }
        if (c < '0' || c > '9') return {};
        digit = true;
        if (dot) { scale *= 0.1; v += (c - '0') * scale; }
        else v = v * 10 + c - '0';
        if (! std::isfinite (v)) return {};
    }
    if (! digit) return {};
    return negative ? -v : v;
}
inline std::optional<int> note (std::string_view s)
{
    if (auto n = integer (s); n && *n >= 0 && *n <= 127) return n;
    if (s.size() < 2) return {};
    const auto c = static_cast<char> (std::tolower (static_cast<unsigned char> (s.front())));
    constexpr int pitch[] { 9, 11, 0, 2, 4, 5, 7 };
    if (c < 'a' || c > 'g') return {};
    int p = pitch[c - 'a'];
    s.remove_prefix (1);
    if (! s.empty() && (s.front() == '#' || s.front() == 'b' || s.front() == 'B'))
    { p += s.front() == '#' ? 1 : -1; s.remove_prefix (1); }
    const auto octave = integer (s);
    if (! octave || *octave < -2 || *octave > 10) return {};
    p += 12 * (*octave + 1);
    if (p < 0 || p > 127) return {};
    return p;
}
inline std::string get (const Opcodes& o, std::string_view key, std::string fallback = {})
{
    const auto found = o.find (std::string (key));
    return found == o.end() ? fallback : found->second;
}

class Parser
{
public:
    Document parse (const Path& source, const Loader& load, int limit = 128)
    {
        doc = {}; defines.clear(); active.clear(); bytes = files = 0;
        global.clear(); master.clear(); group.clear(); region.clear(); control.clear();
        section = Section::none; line = regionLine = 1; root = source.parent_path();
        regionLimit = std::clamp (limit, 1, 4096);
        std::string flat;
        if (! expand (source.lexically_normal(), load, flat, 0)) return std::move (doc);
        lex (flat);
        finish();
        if (doc.error.empty() && doc.regions.empty()) doc.error = "No SFZ regions found.";
        return std::move (doc);
    }
private:
    enum class Section { none, control, global, master, group, region, ignored };
    Section section = Section::none;
    Document doc;
    Path root;
    std::map<std::string, std::string> defines;
    std::set<Path> active;
    Opcodes control, global, master, group, region;
    int line = 1, regionLine = 1, regionLimit = 128, files = 0;
    std::size_t bytes = 0;
    static constexpr std::size_t maxBytes = 8 * 1024 * 1024;

    bool fail (std::string message)
    {
        if (doc.error.empty()) doc.error = std::move (message);
        return false;
    }
    bool substitute (std::string& s)
    {
        // Expand whole variable names only, including macros used in paths.
        for (int pass = 0; pass < 16; ++pass)
        {
            std::string next;
            bool changed = false;
            for (std::size_t p = 0; p < s.size();)
            {
                if (s[p] != '$') { next += s[p++]; continue; }
                auto end = p + 1;
                while (end < s.size() && ident (s[end])) ++end;
                if (end == p + 1) { next += s[p++]; continue; }
                const auto name = s.substr (p, end - p);
                const auto it = defines.find (name);
                if (it == defines.end()) return fail ("Undefined SFZ macro: " + name);
                next += it->second; p = end; changed = true;
                if (next.size() > maxBytes) return fail ("Expanded SFZ exceeds size limit.");
            }
            s = std::move (next);
            if (! changed) return true;
        }
        return fail ("Recursive SFZ macro.");
    }
    bool expand (const Path& source, const Loader& load, std::string& out, int depth)
    {
        if (depth >= 16 || ++files > 64 || active.count (source))
            return fail ("SFZ include cycle or include limit: " + source.string());
        const auto data = load (source);
        if (! data) return fail ("Cannot read SFZ definition: " + source.string());
        if (data->size() > maxBytes - bytes) return fail ("SFZ definition exceeds size limit.");
        bytes += data->size(); active.insert (source);
        std::string clean;
        clean.reserve (data->size());
        bool quote = false;
        for (std::size_t p = 0; p < data->size();)
        {
            const char c = (*data)[p];
            if (p == 0 && data->compare (0, 3, "\xef\xbb\xbf") == 0) { p += 3; continue; }
            if (! quote && c == '/' && p + 1 < data->size() && (*data)[p + 1] == '/')
            { while (p < data->size() && (*data)[p] != '\n') ++p; continue; }
            if (! quote && c == '/' && p + 1 < data->size() && (*data)[p + 1] == '*')
            {
                auto end = data->find ("*/", p + 2);
                if (end == std::string::npos) return fail ("Unclosed SFZ comment.");
                clean += ' ';
                for (; p < end + 2; ++p) if ((*data)[p] == '\n') clean += '\n';
                continue;
            }
            if (c == '"') quote = ! quote;
            clean += c; ++p;
        }
        if (quote) return fail ("Unclosed SFZ quote.");
        for (std::size_t pos = 0; pos < clean.size();)
        {
            auto end = clean.find ('\n', pos);
            if (end == std::string::npos) end = clean.size();
            auto row = trim (std::string_view (clean).substr (pos, end - pos));
            pos = end == clean.size() ? end : end + 1;
            if (row.rfind ("#define", 0) == 0 && row.size() > 7 && space (row[7]))
            {
                auto body = trim (std::string_view (row).substr (7));
                auto split = body.find_first_of (" \t");
                if (split == std::string::npos || body.empty() || body[0] != '$' || split < 2
                    || ! std::all_of (body.begin() + 1, body.begin() + static_cast<std::ptrdiff_t> (split), ident))
                    return fail ("Invalid SFZ #define.");
                if (defines.size() >= 1024 && ! defines.count (body.substr (0, split)))
                    return fail ("SFZ macro limit.");
                defines[body.substr (0, split)] = trim (std::string_view (body).substr (split));
                continue;
            }
            if (! substitute (row)) return false;
            if (row.rfind ("#include", 0) == 0 && row.size() > 8 && space (row[8]))
            {
                const auto file = trim (std::string_view (row).substr (8));
                if (file.size() < 3 || file.front() != '"' || file.back() != '"')
                    return fail ("SFZ #include requires a quoted path.");
                const auto target = path (file.substr (1, file.size() - 2));
                if (! expand ((root / target).lexically_normal(), load, out, depth + 1)) return false;
                continue;
            }
            if (! row.empty() && row.front() == '#') return fail ("Unsupported SFZ preprocessor directive: " + row);
            if (row.size() + 1 > maxBytes - out.size()) return fail ("Expanded SFZ exceeds size limit.");
            out += row; out += '\n';
        }
        active.erase (source);
        return true;
    }

    static void overlay (Opcodes& target, const Opcodes& source)
    {
        for (const auto& [k,v] : source) target[k] = v;
    }
    void finish()
    {
        if (section != Section::region || ! doc.error.empty()) return;
        if (static_cast<int> (doc.regions.size()) >= regionLimit)
        { fail ("SFZ exceeds Studio's region limit (" + std::to_string (regionLimit) + "). No partial instrument imported."); return; }
        Opcodes all = control; overlay (all, global); overlay (all, master); overlay (all, group); overlay (all, region);
        doc.regions.push_back ({ std::move (all), regionLine });
        region.clear();
        section = Section::none;
    }
    void header (std::string name)
    {
        finish();
        name = lower (trim (name));
        if (name == "control") { section = Section::control; }
        else if (name == "global") { section = Section::global; global.clear(); master.clear(); group.clear(); }
        else if (name == "master") { section = Section::master; master.clear(); group.clear(); }
        else if (name == "group") { section = Section::group; group.clear(); }
        else if (name == "region") { section = Section::region; region.clear(); regionLine = line; }
        else { section = Section::ignored; doc.warnings.insert ("Unsupported header <" + name + ">"); }
    }
    void assign (std::string key, std::string value)
    {
        key = lower (key);
        if (key == "loopmode") key = "loop_mode";
        if (key == "loopstart") key = "loop_start";
        if (key == "loopend") key = "loop_end";
        Opcodes* target = nullptr;
        switch (section)
        {
            case Section::control: target = &control; break;
            case Section::global: target = &global; break;
            case Section::master: target = &master; break;
            case Section::group: target = &group; break;
            case Section::region: target = &region; break;
            case Section::ignored: return;
            default: fail ("Opcode before SFZ header at line " + std::to_string (line)); return;
        }
        // 'key' is shorthand for three distinct inherited opcodes. Expand
        // at assignment time, so later lower-level values override correctly.
        if (key == "key")
        { (*target)["lokey"] = value; (*target)["hikey"] = value; (*target)["pitch_keycenter"] = value; }
        else (*target)[key] = value;
    }
    void lex (const std::string& s)
    {
        const auto isAssignment = [&] (std::size_t p)
        {
            const auto start = p;
            while (p < s.size() && ident (s[p])) ++p;
            if (p == start) return false;
            while (p < s.size() && (s[p] == ' ' || s[p] == '\t')) ++p;
            return p < s.size() && s[p] == '=';
        };
        for (std::size_t p = 0; p < s.size() && doc.error.empty();)
        {
            if (space (s[p])) { if (s[p++] == '\n') ++line; continue; }
            if (s[p] == '<')
            {
                const auto end = s.find ('>', p + 1);
                if (end == std::string::npos) { fail ("Unclosed SFZ header."); return; }
                header (s.substr (p + 1, end - p - 1)); p = end + 1; continue;
            }
            if (! isAssignment (p)) { fail ("Malformed SFZ opcode at line " + std::to_string (line)); return; }
            const auto start = p;
            while (p < s.size() && ident (s[p])) ++p;
            auto key = s.substr (start, p - start);
            while (p < s.size() && s[p] != '=') ++p;
            ++p;
            while (p < s.size() && (s[p] == ' ' || s[p] == '\t')) ++p;
            std::string value;
            if (p < s.size() && s[p] == '"')
            {
                const auto end = s.find ('"', ++p);
                if (end == std::string::npos) { fail ("Unclosed SFZ value."); return; }
                value = s.substr (p, end - p); p = end + 1;
            }
            else
            {
                const auto begin = p;
                while (p < s.size() && s[p] != '<' && s[p] != '\n')
                {
                    if (space (s[p]))
                    {
                        auto look = p;
                        while (look < s.size() && (s[look] == ' ' || s[look] == '\t')) ++look;
                        if (isAssignment (look)) break;
                    }
                    ++p;
                }
                value = trim (std::string_view (s).substr (begin, p - begin));
            }
            assign (std::move (key), std::move (value));
        }
    }
};
}
