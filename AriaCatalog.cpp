// SPDX-License-Identifier: GPL-3.0-or-later
#include "AriaCatalog.h"
#include <regex>
#include <set>

using namespace juce;

namespace studio::aria
{
namespace
{
constexpr int64 maxDefinitionSize = 4 * 1024 * 1024;
constexpr size_t maxStateSize = 4 * 1024 * 1024;

bool validState (const String& encoded)
{
    const auto separator = encoded.indexOfChar ('.');
    if (! isPositiveAndBelow (separator, 9) || separator == 0)
        return false;
    const auto prefix = encoded.substring (0, separator);
    if (! prefix.containsOnly ("0123456789"))
        return false;
    const auto size = prefix.getIntValue();
    return size > 0 && size <= int (maxStateSize)
        && encoded.length() - separator - 1 == (size * 8 + 5) / 6
        && encoded.substring (separator + 1).containsOnly (
            ".ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+");
}

var object (std::initializer_list<std::pair<const char*, var>> values)
{
    auto result = std::make_unique<DynamicObject>();
    for (const auto& [key, value] : values)
        result->setProperty (key, value);
    return var (result.release());
}

var readJson (const File& file, int64 limit)
{
    return file.existsAsFile() && file.getSize() <= limit ? JSON::parse (file) : var();
}

Result writeJson (const File& file, const var& value)
{
    TemporaryFile temporary (file);
    if (! temporary.getFile().replaceWithText (JSON::toString (value))
        || ! temporary.overwriteTargetFileWithTemporary())
        return Result::fail ("Could not write " + file.getFileName());
    return Result::ok();
}

String libraryName (const File& source, const File& root)
{
    const auto path = source.getParentDirectory().getFullPathName().toLowerCase();
    for (const auto& [token, name] : std::initializer_list<std::pair<const char*, const char*>> {
             { "gpo5", "Personal Orchestra 5" }, { "personal orchestra 5", "Personal Orchestra 5" },
             { "gpo4", "Personal Orchestra 4" }, { "personal orchestra 4", "Personal Orchestra 4" },
             { "instant orchestra", "Instant Orchestra" }, { "/gio", "Instant Orchestra" },
             { "jazz", "Jazz & Big Band" }, { "/jabb", "Jazz & Big Band" },
             { "marching", "Concert & Marching Band" }, { "/camb", "Concert & Marching Band" },
             { "harp", "Harps" }, { "orchestral strings", "Orchestral Strings" } })
        if (path.contains (token))
            return name;
    const auto parts = StringArray::fromTokens (source.getRelativePathFrom (root), "/", {});
    return parts.size() > 1 ? parts[0] : root.getFileName();
}

String familyName (const File& source)
{
    const auto path = source.getFullPathName().toLowerCase();
    for (const auto& [token, name] : std::initializer_list<std::pair<const char*, const char*>> {
             { "violin", "Strings" }, { "viola", "Strings" }, { "cello", "Strings" },
             { "strings", "Strings" }, { "harp", "Harp" }, { "brass", "Brass" },
             { "trumpet", "Brass" }, { "trombone", "Brass" }, { "horn", "Brass" },
             { "woodwind", "Woodwinds" }, { "flute", "Woodwinds" }, { "clarinet", "Woodwinds" },
             { "oboe", "Woodwinds" }, { "bassoon", "Woodwinds" }, { "sax", "Woodwinds" },
             { "percussion", "Percussion" }, { "drum", "Percussion" }, { "piano", "Keys" },
             { "organ", "Keys" }, { "choir", "Voices" } })
        if (path.contains (token))
            return name;
    return "Other";
}

String classify (const File& file)
{
    if (file.hasFileExtension ("sfz"))       return "sfz";
    if (file.hasFileExtension ("aria;ariax")) return "snapshot";
    if (file.hasFileExtension ("bank;instrument;ariaxml")) return "bank";
    if (file.hasFileExtension ("nki;nkx;nkm;exs;vsldat;vsl;gig;sfark;aupreset;vstpreset;sf2"))
        return "original";
    return {};
}

struct SfzInspection
{
    bool unresolved = false;
    int samples = 0, missing = 0, low = 128, high = -1, filesInspected = 0;
    StringArray articulations;
    std::set<String> includes;

    void inspect (const File& file, const File& root, File& sampleBase,
                  const std::function<bool()>& cancelled, int depth = 0)
    {
        if (depth >= 12 || ++filesInspected > 128 || ! file.isAChildOf (root)
            || ! file.existsAsFile() || file.getSize() > maxDefinitionSize)
        {
            unresolved = true;
            return;
        }
        for (auto path = file; path.isAChildOf (root); path = path.getParentDirectory())
        {
            if (! path.isSymbolicLink()) continue;
            unresolved = true;
            return;
        }
        if (! includes.insert (file.getFullPathName()).second)
        {
            unresolved = true;
            return;
        }

        // Inventory literal references and explicit numeric keys. ARIA remains
        // responsible for interpreting macros, player extensions and key maps.
        static const std::regex token (
            R"sfz(//[^\r\n]*|/\*[\s\S]*?\*/|#include\s+"([^"\r\n]+)"|<[^>\r\n]+>|([a-zA-Z_][a-zA-Z_0-9]*)\s*=\s*([^\r\n<]*?)(?=\s+[a-zA-Z_][a-zA-Z_0-9]*\s*=|//|/\*|<|\r|\n|$))sfz");
        auto stream = file.createInputStream();
        if (stream == nullptr)
        {
            unresolved = true;
            includes.erase (file.getFullPathName());
            return;
        }
        // Limit both include expansion and regex input for malformed definitions.
        BufferedInputStream input (*stream, 8192);
        bool inComment = false;
        while (! input.isExhausted() && input.getPosition() < maxDefinitionSize)
        {
            if (cancelled()) return;
            const auto line = input.readNextLine();
            if (line.getNumBytesAsUTF8() > 4096) { unresolved = true; continue; }
            std::string data = line.toStdString();
            for (size_t offset = 0; offset < data.size();)
            {
                if (inComment)
                {
                    const auto end = data.find ("*/", offset);
                    data.erase (offset, end == std::string::npos ? end : end + 2 - offset);
                    if (end == std::string::npos) break;
                    inComment = false;
                }
                const auto start = data.find ("/*", offset);
                const auto lineComment = data.find ("//", offset);
                if (start == std::string::npos || lineComment < start) break;
                data.erase (start, 2);
                offset = start;
                inComment = true;
            }
            if (line.trimStart().startsWith ("#define")) unresolved = true;
            for (std::sregex_iterator it (data.begin(), data.end(), token), end; it != end; ++it)
            {
                const auto& match = *it;
                if (match[1].matched)
                {
                    const auto path = String (match[1].str()).replaceCharacter ('\\', '/');
                    inspect (file.getSiblingFile (path), root, sampleBase, cancelled, depth + 1);
                    continue;
                }
                const auto opcode = String (match[2].str());
                const auto value = String (match[3].str()).trim().unquoted().replaceCharacter ('\\', '/');
                if (opcode == "sample" || opcode == "default_path")
                {
                    if (value.isEmpty() || value.containsAnyOf ("$%") || value.startsWithChar ('*'))
                    {
                        unresolved = true;
                        continue;
                    }
                    if (opcode == "default_path")
                        sampleBase = file.getSiblingFile (value);
                    else
                    {
                        ++samples;
                        if (! sampleBase.getChildFile (value).existsAsFile()) ++missing;
                    }
                }
                else if ((opcode == "key" || opcode == "lokey" || opcode == "hikey")
                         && value.isNotEmpty() && value.containsOnly ("0123456789"))
                {
                    const auto note = value.getIntValue();
                    if (isPositiveAndBelow (note, 128))
                    {
                        if (opcode != "hikey") low = jmin (low, note);
                        if (opcode != "lokey") high = jmax (high, note);
                    }
                }
                else if ((opcode == "sw_label" || opcode == "region_label") && articulations.size() < 64)
                    articulations.addIfNotAlreadyThere (value);
            }
        }
        unresolved |= inComment;
        includes.erase (file.getFullPathName());
    }
};

bool matchesSource (const Patch& patch)
{
    return patch.present && patch.source.existsAsFile()
        && patch.source.getSize() == patch.size
        && patch.source.getLastModificationTime().toMilliseconds() == patch.modified;
}
}

Catalog::Catalog (File location) : directory (std::move (location)) {}

File Catalog::localDirectory()
{
   #if JUCE_MAC
    return File::getSpecialLocation (File::userHomeDirectory)
        .getChildFile ("Library/Application Support/Studio/ARIA Catalog");
   #else
    return File::getSpecialLocation (File::userApplicationDataDirectory).getChildFile ("Studio/ARIA Catalog");
   #endif
}

Array<File> Catalog::defaultRoots()
{
    Array<File> result;
   #if JUCE_MAC
    const File system ("/Library/Application Support");
    result.add (system.getChildFile ("Garritan"));
    result.add (File::getSpecialLocation (File::userHomeDirectory)
                    .getChildFile ("Library/Application Support/Garritan"));
    result.add (system.getChildFile ("Plogue/Aria/Presets"));
    result.add (File ("/Users/Shared"));
    // The ARIA Player/Presets folder is covered by the system Garritan root.
   #endif
    return result;
}

void Catalog::read()
{
    const auto data = readJson (directory.getChildFile ("catalog.json"), 128 * 1024 * 1024);
    entries.clear();
    pending.clear();
    roots.clear();
    examined = errors = 0;
    if (int (data["schema"]) != 1) return;
    examined = int (data["examined"]);
    errors = int (data["errors"]);
    const auto readRoots = [] (const var& value, Array<File>& destination)
    {
        if (auto* rows = value.getArray())
            for (const auto& row : *rows)
                if (File::isAbsolutePath (row.toString()))
                    destination.addIfNotAlreadyThere (File (row.toString()));
    };
    readRoots (data["pendingRoots"], pending);
    readRoots (data["roots"], roots);
    const auto patches = data["patches"];
    if (auto* rows = patches.getArray())
        for (const auto& row : *rows)
        {
            const auto path = row["source"].toString();
            if (! File::isAbsolutePath (path)) continue;
            const File file (path);
            const auto id = file.getFullPathName();
            Patch patch { id, row["name"].toString(), row["library"].toString(), row["family"].toString(),
                          classify (file), row["health"].toString(), row["midiRange"].toString(),
                          file, {}, int64 (row["modified"]), int64 (row["size"]), bool (row["present"]),
                          int (row["literalSamples"]), int (row["missingSamples"]), row["libraryVersion"].toString() };
            const auto labels = row["articulations"];
            if (auto* values = labels.getArray())
                for (const auto& label : *values) patch.articulations.add (label.toString());
            if (patch.kind.isNotEmpty()) entries.emplace (id, std::move (patch));
        }
}

Result Catalog::save() const
{
    if (const auto result = directory.createDirectory(); result.failed()) return result;
    Array<var> patches, scanned, unfinished;
    for (const auto& root : roots) scanned.add (root.getFullPathName());
    for (const auto& root : pending) unfinished.add (root.getFullPathName());
    for (const auto& [id, patch] : entries)
    {
        Array<var> labels;
        for (const auto& label : patch.articulations) labels.add (label);
        const auto availability = status (patch);
        const auto manifest = availability.startsWith ("Audition-confirmed")
            ? presetFile (patch).getRelativePathFrom (directory) : String();
        patches.add (object ({ { "id", id }, { "source", patch.source.getFullPathName() },
            { "name", patch.name }, { "library", patch.library }, { "family", patch.family },
            { "kind", patch.kind }, { "health", patch.health }, { "midiRange", patch.midiRange },
            { "articulations", var (labels) }, { "modified", patch.modified }, { "size", patch.size },
            { "present", patch.present }, { "status", availability }, { "presetManifest", manifest },
            { "libraryVersion", patch.libraryVersion },
            { "literalSamples", patch.literalSamples }, { "missingSamples", patch.missingSamples } }));
    }
    const auto result = writeJson (directory.getChildFile ("catalog.json"),
        object ({ { "schema", 1 }, { "roots", var (scanned) }, { "pendingRoots", var (unfinished) },
                  { "examined", examined }, { "errors", errors }, { "patches", var (patches) } }));
    if (result.failed()) return result;
    TemporaryFile output (directory.getChildFile ("compatibility.txt"));
    if (! output.getFile().replaceWithText (report()) || ! output.overwriteTargetFileWithTemporary())
        return Result::fail ("Could not write compatibility.txt");
    return Result::ok();
}

void Catalog::scan (Array<File> selected, const std::function<bool (const Progress&)>& shouldCancel)
{
    // A cancelled root is enumerated again. Already inspected definitions are
    // reused during resume if the definition itself has not changed.
    const auto resuming = pending;
    for (const auto& root : selected) pending.addIfNotAlreadyThere (root);
    Progress progress;
    for (const auto& root : selected)
    {
        progress.root = root;
        const auto cancelled = [&] { return shouldCancel (progress); };
        if (cancelled()) break;
        if (! root.isDirectory() || ! root.hasReadAccess())
        {
            ++progress.errors;
            continue;
        }
        std::set<String> seen;
        for (const auto& entry : RangedDirectoryIterator (root, true, "*", File::findFiles, File::FollowSymlinks::no))
        {
            if (cancelled()) break;
            ++progress.examined;
            const auto file = entry.getFile();
            const auto kind = classify (file);
            if (kind.isEmpty()) continue;
            const auto id = file.getFullPathName();
            if (! seen.insert (id).second) continue;
            const auto size = entry.getFileSize();
            const auto modified = entry.getModificationTime().toMilliseconds();
            if (const auto it = entries.find (id); resuming.contains (root) && it != entries.end()
                && it->second.present && it->second.modified == modified && it->second.size == size)
            {
                ++progress.discovered;
                if (it->second.missingSamples > 0) ++progress.missing;
                continue;
            }

            Patch patch { id, file.getFileNameWithoutExtension(), libraryName (file, root), familyName (file),
                          kind, "Unknown", {}, file, {}, modified, size, true, 0, 0, {} };
            if (patch.library == "Personal Orchestra 5") patch.libraryVersion = "5";
            if (patch.library == "Personal Orchestra 4") patch.libraryVersion = "4";
            if (kind == "sfz")
            {
                auto base = file.getParentDirectory();
                SfzInspection inspection;
                inspection.inspect (file, root, base, cancelled);
                if (cancelled()) break;
                patch.health = inspection.missing > 0 ? "Missing literal sample references"
                    : inspection.unresolved || inspection.samples == 0 ? "Dependencies unresolved"
                    : "Literal sample references present";
                patch.literalSamples = inspection.samples;
                patch.missingSamples = inspection.missing;
                patch.articulations = std::move (inspection.articulations);
                if (inspection.low <= inspection.high)
                    patch.midiRange = String (inspection.low) + "–" + String (inspection.high);
            }
            ++progress.discovered;
            if (patch.missingSamples > 0) ++progress.missing;
            entries.insert_or_assign (id, std::move (patch));
        }
        if (cancelled()) break;
        for (auto& [id, patch] : entries)
            if (patch.source.isAChildOf (root) && seen.count (id) == 0)
                patch.present = patch.source.existsAsFile();
        pending.removeAllInstancesOf (root);
        roots.addIfNotAlreadyThere (root);
    }
    examined = progress.examined;
    errors = progress.errors;
}

File Catalog::presetFile (const Patch& patch) const
{
    // The full source identity is also checked inside the manifest.
    return directory.getChildFile ("Presets").getChildFile (String::toHexString (patch.id.hashCode64()) + ".json");
}

Preset Catalog::preset (const Patch& patch) const
{
    if (patch.kind != "sfz" || ! patch.present) return {};
    const auto data = readJson (presetFile (patch), int64 (maxStateSize * 2));
    if (int (data["schema"]) != 1 || data["id"].toString() != patch.id
        || int64 (data["modified"]) != patch.modified || int64 (data["size"]) != patch.size)
        return {};
    const auto state = data["state"].toString();
    if (! validState (state) || data["plugin"].toString().isEmpty() || ! matchesSource (patch))
        return {};
    return { data["plugin"].toString(), data["pluginVersion"].toString(), state, int (data["program"]) };
}

Result Catalog::registerPreset (const Patch& patch, const Preset& value)
{
    if (patch.kind != "sfz" || ! matchesSource (patch) || value.pluginId.isEmpty()
        || ! validState (value.state))
        return Result::fail ("The source changed or the Audio Unit did not supply a reusable state.");
    const auto file = presetFile (patch);
    if (file.exists())
        return Result::fail ("A capture already exists for this source. Review or move its manifest before recapturing.");
    if (const auto result = file.getParentDirectory().createDirectory(); result.failed()) return result;
    return writeJson (file, object ({ { "schema", 1 }, { "id", patch.id }, { "modified", patch.modified },
            { "size", patch.size }, { "plugin", value.pluginId }, { "pluginVersion", value.pluginVersion },
            { "state", value.state }, { "program", value.program }, { "midiChannel", 1 } }));
}

String Catalog::status (const Patch& patch) const
{
    if (! patch.present) return "Source unavailable";
    if (patch.kind == "original") return "Original player / native importer";
    if (patch.kind == "snapshot") return "ARIA snapshot / possible ensemble";
    if (patch.kind == "bank") return "Bank or instrument container / program mapping unresolved";
    if (preset (patch).state.isNotEmpty()) return "Audition-confirmed ARIA capture";
    if (patch.health.startsWith ("Missing")) return "ARIA required / check samples";
    return "ARIA required / choose patch in ARIA";
}

String Catalog::report() const
{
    std::map<String, int> libraries;
    int captured = 0, original = 0, unresolved = 0, missing = 0, snapshots = 0, selectable = 0;
    for (const auto& [id, patch] : entries)
    {
        if (patch.present) ++libraries[patch.library];
        const auto state = status (patch);
        if (state.startsWith ("Audition-confirmed")) ++captured;
        else if (patch.present && patch.kind == "original") ++original;
        else ++unresolved;
        if (patch.present && patch.kind == "snapshot") ++snapshots;
        if (patch.present && patch.kind == "sfz" && ! state.startsWith ("Audition-confirmed")) ++selectable;
        if (patch.present && patch.health.startsWith ("Missing")) ++missing;
    }
    String summary = "Studio ARIA compatibility report\n"
        "Files examined in last pass: " + String (examined)
        + "\nDiscovered: " + String (entries.size())
        + "\nARIA-loadable (user-auditioned capture): " + String (captured)
        + "\nNative-convertible (verified by this scan): 0"
        + "\nOriginal-player / native-importer candidates: " + String (original)
        + "\nUnresolved: " + String (unresolved)
        + "\nARIA required / manual selection: " + String (selectable)
        + "\nPossible ensemble snapshots: " + String (snapshots)
        + "\nMissing literal sample references: " + String (missing)
        + "\nUnavailable roots: " + String (errors)
        + "\nPending roots: " + String (pending.size()) + "\n";
    for (const auto& [library, count] : libraries)
        summary << "\n" << library << " — " << count;
    return summary;
}
}
