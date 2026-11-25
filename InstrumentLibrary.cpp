// SPDX-License-Identifier: GPL-3.0-or-later
#include "InstrumentLibrary.h"
#include "ComposerFiles.h"
#include "InstrumentReaders.h"
#include "SamplerMapping.h"
#include <set>
#include <charconv>
#include <cmath>
#include <filesystem>
#include <map>
#include <optional>
#include <sstream>
#include <cctype>
#include <tuple>

namespace studio::instrumentimport
{
using namespace juce;
namespace fs = std::filesystem;
namespace
{
using Opcodes = std::map<String, String>;
constexpr auto pcm = sampler::audioExtensions;
constexpr auto otherSamples = "ogg;mp3;wv;ncw;nkx;nkr;audio;sample;samples";
constexpr auto readableDefinitions = "sfz;sf2;exs;dspreset;studiosampler";
constexpr auto originalFormats = "aria;ariax;gig;nki;nkm;aupreset;patch;adv;sxt;vstpreset;uvip;uvib;st3;st4;halion;akp;akai;p1;p3";
constexpr auto opaqueDefinitions = "bank;sfark;iso;img;nks;monolith;ufs;dspack;xml;instrument";
constexpr int maxFiles = 50000, maxDefinitions = 512;

fs::path pathOf (const File& file) { return fs::u8path (file.getFullPathName().toRawUTF8()); }
File fileOf (const fs::path& path) { return File (String::fromUTF8 (path.string().c_str())); }

File realPath (const File& file)
{
    std::error_code ec;
    auto path = fs::weakly_canonical (pathOf (file), ec);
    return ec || path.empty() ? File() : fileOf (path);
}

var object (std::initializer_list<std::pair<const char*, var>> fields)
{
    auto result = std::make_unique<DynamicObject>();
    for (const auto& [key, value] : fields) result->setProperty (key, value);
    return var (result.release());
}

Array<var> strings (const StringArray& source)
{
    Array<var> result;
    for (const auto& entry : source) result.add (entry);
    return result;
}

String value (const Opcodes& ops, const String& key, String fallback = {})
{
    auto found = ops.find (key);
    return found == ops.end() ? fallback : found->second;
}

// SFZ note names use C4 = MIDI 60. All other numbers must parse completely.
int integer (const Opcodes& ops, const String& key, int fallback, bool& ok, bool note = false)
{
    if (! ops.contains (key)) return fallback;
    const auto text = ops.at (key).toStdString();
    int number = 0;
    const auto parse = [&] (std::size_t offset)
    {
        const auto end = text.data() + text.size();
        const auto result = std::from_chars (text.data() + offset, end, number);
        return result.ec == std::errc() && result.ptr == end;
    };
    if (parse (0)) return number;
    if (note && text.size() >= 2)
    {
        const auto pitch = String ("c d ef g a b").indexOfChar (CharacterFunctions::toLowerCase (text[0]));
        const bool accidental = text[1] == '#' || text[1] == 'b';
        if (pitch >= 0 && text[0] != ' ' && parse (accidental ? 2 : 1) && number >= -1 && number <= 9)
            return (number + 1) * 12 + pitch + (accidental ? (text[1] == '#' ? 1 : -1) : 0);
    }
    ok = false;
    return fallback;
}

float numeric (const Opcodes& ops, const String& key, float fallback, float min, float max, bool& ok)
{
    if (! ops.contains (key)) return fallback;
    std::istringstream input (ops.at (key).toStdString());
    input.imbue (std::locale::classic());
    double number = 0;
    if (! (input >> number) || input.peek() != std::char_traits<char>::eof()
        || ! std::isfinite (number) || number < min || number > max)
    {
        ok = false;
        return fallback;
    }
    return static_cast<float> (number);
}

String stripComments (const String& source)
{
    String result;
    for (const auto& line : StringArray::fromLines (source))
    {
        bool quoted = false;
        int end = line.length();
        for (int i = 0; i + 1 < end; ++i)
        {
            if (line[i] == '"') quoted = ! quoted;
            if (! quoted && line[i] == '/' && line[i + 1] == '/') { end = i; break; }
        }
        result << line.substring (0, end) << "\n";
    }
    return result;
}

bool letter (unsigned char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

bool parseOpcodes (std::string_view text, Opcodes& target)
{
    String key;
    std::size_t start = 0;
    bool quoted = false;
    const auto append = [&] (std::size_t end)
    {
        auto field = String (std::string (text.substr (start, end - start))).trim();
        if (key.isEmpty()) return field.isEmpty();
        if (field.isEmpty() || field.length() > 2048) return false;
        target[key] = field.unquoted();
        return true;
    };
    for (std::size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] == '"') quoted = ! quoted;
        if (quoted || ! letter (text[i]) || (i > 0 && ! std::isspace (static_cast<unsigned char> (text[i - 1]))))
            continue;
        auto end = i + 1;
        while (end < text.size() && (letter (text[end]) || (text[end] >= '0' && text[end] <= '9') || text[end] == '_'))
            ++end;
        auto equal = end;
        while (equal < text.size() && std::isspace (static_cast<unsigned char> (text[equal]))) ++equal;
        if (equal == text.size() || text[equal] != '=') continue;
        if (! append (i) || end - i > 80) return false;
        key = String (std::string (text.substr (i, end - i))).toLowerCase();
        start = equal + 1;
        i = equal;
    }
    return ! quoted && append (text.size());
}

bool parse (const File& file, std::vector<Opcodes>& regions, StringArray& issues)
{
    if (file.getSize() <= 0 || file.getSize() > 512 * 1024)
    {
        issues.add ("Definition is empty or exceeds 512 KiB.");
        return false;
    }
    const auto content = stripComments (file.loadFileAsString());
    if (content.containsChar ('$') || content.containsIgnoreCase ("#include") || content.containsIgnoreCase ("#define"))
    {
        issues.add ("SFZ includes and macros require the source player.");
        return false;
    }
    const auto text = content.toStdString();
    const auto findHeader = [&] (std::size_t start)
    {
        bool quoted = false;
        for (auto i = start; i < text.size(); ++i)
        {
            if (text[i] == '"') quoted = ! quoted;
            if (! quoted && text[i] == '<') return i;
        }
        return text.size();
    };
    auto cursor = findHeader (0);
    if (cursor == text.size() || String (text.substr (0, cursor)).trim().isNotEmpty())
    {
        issues.add ("Missing readable SFZ headers.");
        return false;
    }

    Opcodes global, master, group, defaults;
    std::size_t effectiveBytes = 0;
    while (cursor < text.size())
    {
        auto close = cursor + 1;
        while (close < text.size() && close - cursor <= 32 && letter (text[close])) ++close;
        if (close >= text.size() || text[close] != '>' || close == cursor + 1)
        {
            issues.add ("Malformed SFZ header.");
            return false;
        }
        const auto name = String (text.substr (cursor + 1, close - cursor - 1)).toLowerCase();
        const auto first = close + 1;
        cursor = findHeader (first);
        Opcodes fields;
        if (! parseOpcodes (std::string_view (text).substr (first, cursor - first), fields))
        {
            issues.add ("Malformed opcodes in <" + name + ">.");
            return false;
        }
        auto inherited = [] (Opcodes base, const Opcodes& overrides)
        {
            for (const auto& [key, item] : overrides) base[key] = item;
            return base;
        };
        if (name == "control") defaults = std::move (fields);
        else if (name == "global") { global = std::move (fields); master = global; group = master; }
        else if (name == "master") { master = inherited (global, fields); group = master; }
        else if (name == "group") group = inherited (master, fields);
        else if (name == "region")
        {
            auto effective = inherited (inherited (defaults, group), fields);
            for (const auto& [key, field] : effective)
                effectiveBytes += 64 + key.getNumBytesAsUTF8() + field.getNumBytesAsUTF8();
            if (effective.size() > 128 || effectiveBytes > 128 * 1024)
            {
                issues.add ("Effective region opcodes exceed the preset inventory limit.");
                return false;
            }
            regions.push_back (std::move (effective));
            if (regions.size() > sampler::maxZones)
            {
                issues.add ("Exceeds Studio's 128-zone limit.");
                return false;
            }
        }
        else
        {
            issues.add ("Unsupported SFZ section <" + name + ">.");
            return false;
        }
    }
    if (regions.empty()) issues.add ("No SFZ regions were found.");
    return ! regions.empty();
}

struct SampleInfo
{
    int frames = 0;
    std::size_t bytes = 0;
    bool hasEmbeddedLoop = false;
    int loopStart = -1, loopEnd = -1, root = -1, channels = 0;
    double rate = 0;
};

SampleInfo probe (const File& file)
{
    if (! file.hasFileExtension (pcm)) return {};
    auto reader = sampler::openSampleReader (file);
    if (! reader || reader->numChannels < 1 || reader->numChannels > 2
        || reader->sampleRate < 1000 || reader->sampleRate > 768000 || ! std::isfinite (reader->sampleRate)
        || reader->lengthInSamples < 2 || reader->lengthInSamples > static_cast<int64> (sampler::maxSampleBytes / (sizeof (float) * reader->numChannels)))
        return {};
    const auto frames = static_cast<int> (reader->lengthInSamples);
    SampleInfo info { frames, static_cast<std::size_t> (frames) * sizeof (float) * reader->numChannels };
    info.channels = static_cast<int> (reader->numChannels);
    info.rate = reader->sampleRate;
    const auto& metadata = reader->metadataValues;
    const auto field = [&] (const char* name, const char* fallback = "0") { return metadata.getValue (name, fallback).getIntValue(); };
    for (const auto& key : { String ("MidiUnityNote"), String (AiffAudioFormat::appleRootNote) })
    {
        const auto note = metadata.getValue (key, {});
        if (note.isNotEmpty() && note.containsOnly ("0123456789") && note.getIntValue() <= 127)
        { info.root = note.getIntValue(); break; }
    }
    if (file.hasFileExtension ("wav;wave"))
    {
        const auto loops = field ("NumSampleLoops");
        info.hasEmbeddedLoop = loops > 0;
        if (loops == 1 && field ("Loop0Type", "-1") == 0 && field ("Loop0PlayCount", "-1") == 0)
        {
            info.loopStart = field ("Loop0Start", "-1");
            info.loopEnd = field ("Loop0End", "-1");
        }
    }
    else info.hasEmbeddedLoop = field ("Loop0Type") != 0 || field ("Loop1Type") != 0;
    return info;
}

void analyse (const File& root, Definition& definition, Progress& progress, const SampleLocator& locate)
{
    std::vector<Opcodes> regions;
    if (! parse (definition.source, regions, definition.issues)) return;
    auto& model = definition.instrument;
    model.groups.clear();
    std::map<File, SampleInfo> dependencies;
    using SequenceKey = std::tuple<int, int, int, int, int, int>;
    std::map<SequenceKey, std::map<int, sampler::SampleZone>> sequences;
    std::optional<std::array<float, 4>> envelope;
    StringArray caveats, errors;
    std::optional<int> defaultSwitch;
    for (const auto& ops : regions)
    {
        if (progress.cancelled.load()) return;
        const auto sample = value (ops, "sample").replaceCharacter ('\\', '/');
        const auto prefix = value (ops, "default_path").replaceCharacter ('\\', '/');
        auto raw = definition.source.getParentDirectory();
        if (prefix.isNotEmpty()) raw = raw.getChildFile (prefix);
        if (sample.isNotEmpty()) raw = raw.getChildFile (sample);
        auto file = sample.isEmpty() || sample.startsWithChar ('*') || File::isAbsolutePath (sample)
            || File::isAbsolutePath (prefix) ? File() : realPath (raw);
        if (file != File() && (! file.isAChildOf (root) || ! file.existsAsFile()))
            file = locate (prefix.isEmpty() ? sample : prefix + "/" + sample);
        const bool inside = file != File() && file.isAChildOf (root);
        auto opcodes = std::make_unique<DynamicObject>();
        for (const auto& [key, field] : ops) opcodes->setProperty (key, field);
        definition.regions.add (object ({ { "opcodes", var (opcodes.release()) },
            { "sample", inside ? file.getRelativePathFrom (root) : String() } }));
        std::error_code sourceError;
        if (! inside || ! fs::is_regular_file (pathOf (file), sourceError))
        {
            errors.addIfNotAlreadyThere ("Missing or out-of-library sample: " + sample);
            continue;
        }
        auto [found, inserted] = dependencies.try_emplace (file);
        if (inserted) { found->second = probe (file); definition.decodedBytes += found->second.bytes; }
        const auto& info = found->second;
        if (info.frames == 0)
        {
            errors.addIfNotAlreadyThere ("Unreadable, unsupported or oversized sample: " + file.getRelativePathFrom (root));
            continue;
        }
        bool ok = true;
        sampler::SampleZone entry { file, {}, {}, 0 };
        auto& zone = entry.mapping;
        const int key = integer (ops, "key", -1, ok, true);
        if (ops.contains ("key") && (key < 0 || key > 127)) ok = false;
        zone.root = integer (ops, "pitch_keycenter", key == -1 ? 60 : key, ok, true);
        zone.low = integer (ops, "lokey", key == -1 ? 0 : key, ok, true);
        zone.high = integer (ops, "hikey", key == -1 ? 127 : key, ok, true);
        zone.soft = integer (ops, "lovel", 1, ok);
        zone.hard = integer (ops, "hivel", 127, ok);
        zone.gainDb = numeric (ops, "volume", 0, -60, 12, ok);
        zone.cents = numeric (ops, "tune", 0, -100, 100, ok);
        zone.start = integer (ops, "offset", 0, ok);
        const auto inclusiveEnd = integer (ops, "end", info.frames - 1, ok);
        if (inclusiveEnd >= 0 && inclusiveEnd < info.frames) zone.end = inclusiveEnd + 1;
        else ok = false;
        const auto loopMode = value (ops, "loop_mode", info.hasEmbeddedLoop ? "loop_continuous" : "no_loop");
        zone.oneShot = loopMode == "one_shot";
        zone.loop = loopMode == "loop_continuous" || loopMode == "loop_sustain";
        if (loopMode == "loop_sustain") caveats.addIfNotAlreadyThere ("Sustain loops continue through the native release envelope.");
        if (! zone.loop && ! zone.oneShot && loopMode != "no_loop") ok = false;
        if (zone.loop)
        {
            zone.loopStart = integer (ops, "loop_start", info.loopStart, ok);
            const int last = integer (ops, "loop_end", info.loopEnd, ok);
            if (last >= 0 && last < info.frames) zone.loopEnd = last + 1;
            zone.loopCrossfade = 0;
            if (! sampler::validLoop (zone, info.frames))
                errors.addIfNotAlreadyThere ("Loop needs valid forward-loop metadata or explicit loop_start and loop_end: " + sample);
        }
        const std::array<float, 4> adsr { numeric (ops, "ampeg_attack", 0, 0, 10, ok),
            numeric (ops, "ampeg_decay", 0, 0, 10, ok), numeric (ops, "ampeg_sustain", 100, 0, 100, ok) / 100,
            numeric (ops, "ampeg_release", 0, 0, 15, ok) };
        if (! envelope) envelope = adsr;
        else if (*envelope != adsr) caveats.addIfNotAlreadyThere ("Multiple envelopes. The first region's ADSR is used.");

        sampler::Group gating;
        const auto trigger = value (ops, "trigger", "attack");
        gating.releaseTrigger = trigger == "release";
        if (! gating.releaseTrigger && trigger != "attack")
            errors.addIfNotAlreadyThere ("Unsupported trigger: " + trigger);
        gating.key = integer (ops, "sw_last", -1, ok, true);
        if (ops.contains ("sw_last"))
        {
            gating.trigger = sampler::GroupTrigger::keyswitch;
            const auto swLow = integer (ops, "sw_lokey", 0, ok, true);
            const auto swHigh = integer (ops, "sw_hikey", 127, ok, true);
            if (swLow < 0 || swHigh > 127 || gating.key < swLow || gating.key > swHigh) ok = false;
            if (ops.contains ("sw_default"))
            {
                const auto wanted = integer (ops, "sw_default", -1, ok, true);
                if (defaultSwitch && *defaultSwitch != wanted) ok = false;
                defaultSwitch = wanted;
            }
        }
        else gating.key = 24;
        int controller = -1;
        for (const auto& [opcode, field] : ops)
        {
            if (opcode.startsWith ("locc") || opcode.startsWith ("hicc"))
            {
                const auto suffix = opcode.substring (4);
                const int cc = suffix.getIntValue();
                if (suffix.isEmpty() || ! suffix.containsOnly ("0123456789") || cc < 0 || cc >= 120
                    || (controller >= 0 && controller != cc) || gating.trigger == sampler::GroupTrigger::keyswitch)
                    ok = false;
                controller = cc;
                if (opcode.startsWith ("locc")) gating.ccLow = integer (ops, opcode, 0, ok);
                else gating.ccHigh = integer (ops, opcode, 127, ok);
            }
            else if (! StringArray::fromTokens ("sample default_path key lokey hikey pitch_keycenter lovel hivel volume tune offset end loop_mode loop_start loop_end ampeg_attack ampeg_decay ampeg_sustain ampeg_release trigger sw_last sw_default sw_label sw_lokey sw_hikey seq_length seq_position group_label region_label label", " ", {}).contains (opcode))
            {
                if (opcode.startsWith ("sw_") || opcode.startsWith ("on_") || opcode.startsWith ("lo")
                    || opcode.startsWith ("hi") || opcode.startsWith ("start_") || opcode.startsWith ("stop_")
                    || opcode == "group" || opcode == "off_by" || opcode == "off_mode")
                    errors.addIfNotAlreadyThere ("Unsupported selection or choke opcode: " + opcode);
                else
                    caveats.addIfNotAlreadyThere ("Source opcode retained only in inventory: " + opcode);
            }
        }
        if (controller >= 0) { gating.trigger = sampler::GroupTrigger::controller; gating.controller = controller; }
        if (! sampler::validGroup (gating)) ok = false;
        const auto matches = [&] (const sampler::InstrumentGroup& group)
        {
            const auto& g = group.playback;
            return g.trigger == gating.trigger && g.key == gating.key && g.controller == gating.controller
                && g.ccLow == gating.ccLow && g.ccHigh == gating.ccHigh && g.releaseTrigger == gating.releaseTrigger;
        };
        auto existing = std::find_if (model.groups.begin(), model.groups.end(), matches);
        if (existing == model.groups.end())
        {
            gating.id = static_cast<int> (model.groups.size());
            String name = value (ops, "sw_label", value (ops, "group_label"));
            if (name.isEmpty()) name = gating.releaseTrigger ? "Release" : "Main";
            if (gating.trigger == sampler::GroupTrigger::keyswitch && ! ops.contains ("sw_label"))
                name = "Keyswitch " + String (gating.key);
            if (gating.trigger == sampler::GroupTrigger::controller) name = "CC " + String (controller);
            if (gating.id >= sampler::maxGroups) { errors.addIfNotAlreadyThere ("Exceeds Studio's 16-group limit."); continue; }
            model.groups.push_back ({ gating.id, name, gating });
            entry.folder = zone.folder = gating.id;
        }
        else entry.folder = zone.folder = existing->id;

        const int length = integer (ops, "seq_length", 1, ok);
        const int position = integer (ops, "seq_position", 1, ok);
        if (! ok || ! sampler::validMapping (zone) || zone.start < 0 || zone.start >= zone.end
            || length < 1 || length > sampler::maxZones || position < 1 || position > length)
        {
            errors.addIfNotAlreadyThere ("Invalid or unrepresentable mapping: " + sample);
            continue;
        }
        if (length == 1) model.zones.push_back (std::move (entry));
        else if (! sequences[{ zone.folder, zone.low, zone.high, zone.soft, zone.hard, length }].emplace (position, std::move (entry)).second)
            errors.addIfNotAlreadyThere ("Duplicate round-robin position.");
    }

    if (envelope)
        std::tie (model.settings.attack, model.settings.decay, model.settings.sustain, model.settings.release)
            = std::tie ((*envelope)[0], (*envelope)[1], (*envelope)[2], (*envelope)[3]);
    model.settings.gainDb = 0;
    int roundRobin = 0;
    for (auto& [key, parts] : sequences)
    {
        if (static_cast<int> (parts.size()) != std::get<5> (key) || ++roundRobin > sampler::maxGroups)
        {
            errors.addIfNotAlreadyThere ("Incomplete sequence or too many round-robin sets.");
            continue;
        }
        for (auto& [position, entry] : parts)
        {
            ignoreUnused (position);
            entry.mapping.group = roundRobin;
            model.zones.push_back (std::move (entry));
        }
    }
    const auto isSwitch = [] (const auto& group) { return group.playback.trigger == sampler::GroupTrigger::keyswitch; };
    if (auto first = std::find_if (model.groups.begin(), model.groups.end(), isSwitch); first != model.groups.end())
    {
        if (defaultSwitch)
        {
            const auto wanted = std::find_if (model.groups.begin(), model.groups.end(), [&] (const auto& group)
                { return isSwitch (group) && group.playback.key == *defaultSwitch; });
            if (wanted == model.groups.end()) errors.add ("Default keyswitch is missing.");
            else std::iter_swap (first, wanted);
        }
        else caveats.add ("Studio selects the first articulation before a keyswitch is played.");
    }
    for (const auto& group : model.groups)
        if (isSwitch (group))
            for (const auto& entry : model.zones)
                if (group.playback.key >= entry.mapping.low && group.playback.key <= entry.mapping.high)
                    errors.addIfNotAlreadyThere ("Keyswitch overlaps a playable key range.");

    if (definition.decodedBytes > sampler::maxBankBytes) errors.add ("Exceeds the 256 MiB decoded sampler bank limit.");
    definition.issues = errors;
    definition.issues.addArray (caveats);
    if (errors.isEmpty() && ! model.zones.empty())
        definition.compatibility = caveats.isEmpty() ? Compatibility::supported : Compatibility::partial;
}

bool safeDestination (const Inventory& inventory, const File& directory)
{
    if (directory == File() || inventory.root == File()) return false;
    const auto output = realPath (directory);
    const auto sourceParent = inventory.selection.existsAsFile() ? inventory.selection.getParentDirectory() : inventory.root;
    return output != File() && output != inventory.root && ! output.isAChildOf (inventory.root)
        && output != sourceParent && ! output.isAChildOf (sourceParent);
}

bool regularInside (const File& path, const File& root)
{
    std::error_code error;
    return path != File() && path.isAChildOf (root) && fs::is_regular_file (pathOf (path), error);
}

struct Discovered
{
    std::vector<File> definitions, audio;
    std::map<String, std::vector<File>> fileNames;
};

Discovered discover (Inventory& inventory, Progress& progress)
{
    Discovered result;
    std::error_code error;
    fs::recursive_directory_iterator it (pathOf (inventory.root), fs::directory_options::none, error), end;
    while (! error && it != end && ! progress.cancelled.load())
    {
        if (++progress.completed > maxFiles)
        {
            inventory.issues.add ("Scan stopped at the 50,000-entry limit.");
            break;
        }
        const auto type = it->symlink_status (error);
        if (error) break;
        if (fs::is_regular_file (type))
        {
            const auto file = fileOf (it->path());
            if (file.hasFileExtension (pcm) || file.hasFileExtension (otherSamples))
            {
                const bool audio = file.hasFileExtension (pcm);
                if (audio)
                {
                    result.audio.push_back (file);
                    result.fileNames[file.getFileName().toLowerCase()].push_back (file);
                }
                inventory.samples.add (object ({ { "path", file.getRelativePathFrom (inventory.root) },
                    { "format", file.getFileExtension().substring (1).toLowerCase() },
                    { "fileBytes", file.getSize() }, { "candidateForNativeDecode", audio } }));
            }
            else if (inventory.selection.isDirectory() && (file.hasFileExtension (readableDefinitions)
                     || file.hasFileExtension (originalFormats) || file.hasFileExtension (opaqueDefinitions)))
                result.definitions.push_back (file);
        }
        it.increment (error);
    }
    if (error) inventory.issues.add ("Traversal stopped at an unreadable entry: " + String (error.message()));
    if (inventory.selection.existsAsFile()) result.definitions.push_back (inventory.selection);
    std::sort (result.definitions.begin(), result.definitions.end());
    std::sort (result.audio.begin(), result.audio.end());
    StringArray listing;
    for (const auto& file : result.audio) listing.add (file.getRelativePathFrom (inventory.root));
    if (! progress.cancelled.load())
        inventory.listingFingerprint = String::toHexString (listing.joinIntoString ("\n").hashCode64());
    return result;
}

SampleLocator locator (const File& source, const File& root, const Discovered& files)
{
    return [source, root, &files] (const String& raw)
    {
        const auto path = raw.replaceCharacter ('\\', '/');
        if (path.isEmpty() || path.contains ("://") || path.startsWithChar ('*')) return File();
        const bool absolute = File::isAbsolutePath (path);
        const auto relative = absolute ? File (path).getFileName() : path;
        for (const auto& candidate : { absolute ? File (path) : source.getParentDirectory().getChildFile (path),
                                        root.getChildFile (relative) })
        {
            const auto resolved = realPath (candidate);
            if (regularInside (resolved, root) && resolved.hasFileExtension (pcm)) return resolved;
        }
        // A basename search is reserved for EXS references. A second matching
        // filename remains unresolved until the user chooses a narrower root.
        if (source.hasFileExtension ("exs"))
        {
            const auto found = files.fileNames.find (relative.fromLastOccurrenceOf ("/", false, false).toLowerCase());
            if (found != files.fileNames.end() && found->second.size() == 1) return found->second.front();
        }
        return File();
    };
}

void verifyMapping (Definition& item, const File& root, Progress& progress)
{
    if (! canConvert (item.compatibility, true)) return;
    item.decodedBytes = 0;
    std::map<File, SampleInfo> decoded;
    std::set<std::tuple<File, int64, int, int>> slices;
    const bool describeMappings = item.regions.isEmpty();
    for (const auto& sample : item.instrument.zones)
    {
        if (progress.cancelled.load()) return;
        const auto& z = sample.mapping;
        if (describeMappings)
            item.regions.add (object ({ { "sample", sample.file.getRelativePathFrom (root) },
                { "root", z.root }, { "keyLow", z.low }, { "keyHigh", z.high }, { "velocityLow", z.soft },
                { "velocityHigh", z.hard }, { "start", z.start }, { "end", z.end },
                { "loop", z.loop }, { "loopStart", z.loopStart }, { "loopEnd", z.loopEnd },
                { "loopCrossfade", z.loopCrossfade }, { "group", sample.folder }, { "roundRobin", z.group } }));
        int frames = 0;
        if (sample.soundFont && regularInside (realPath (sample.file), root))
        {
            const auto& slice = *sample.soundFont;
            if (slices.emplace (sample.file, slice.offset, slice.frames, slice.rate).second)
                item.decodedBytes += static_cast<std::size_t> (slice.frames) * sizeof (float);
            frames = slice.frames;
        }
        else if (regularInside (realPath (sample.file), root))
        {
            auto [found, inserted] = decoded.try_emplace (sample.file);
            if (inserted)
            {
                found->second = probe (sample.file);
                item.decodedBytes += found->second.bytes;
            }
            frames = found->second.frames;
        }
        auto loop = z;
        if (loop.loopEnd == 0) loop.loopEnd = frames;
        if (frames == 0 || z.start < 0 || z.start >= frames || z.end > frames
            || (z.end != 0 && z.end <= z.start) || (z.loop && ! sampler::validLoop (loop, frames)))
        {
            item.compatibility = Compatibility::unsupported;
            item.issues.addIfNotAlreadyThere ("Missing, unreadable or out-of-range sample: " + sample.file.getFileName());
        }
    }
    if (item.decodedBytes > sampler::maxBankBytes || item.instrument.zones.empty())
    {
        item.compatibility = Compatibility::unsupported;
        item.issues.addIfNotAlreadyThere ("No playable zones, or the decoded bank exceeds 256 MiB.");
    }
}

File indexFile (const Inventory& inventory, const File& directory)
{
    const auto key = String::toHexString ((inventory.root.getFullPathName() + "\n" + inventory.selection.getFullPathName()).hashCode64());
    return directory.getChildFile (key + ".json");
}

var stamp (const File& file)
{
    return object ({ { "path", file.getFullPathName() }, { "bytes", file.getSize() },
                     { "modified", file.getLastModificationTime().toMilliseconds() } });
}

bool matchesStamp (const var& record)
{
    const auto path = record.getProperty ("path", {}).toString();
    if (! File::isAbsolutePath (path)) return false;
    const File file (path);
    return file.existsAsFile() && realPath (file) == file
        && file.getSize() == static_cast<int64> (record.getProperty ("bytes", -1))
        && file.getLastModificationTime().toMilliseconds() == static_cast<int64> (record.getProperty ("modified", -1));
}

std::map<File, std::vector<var>> readIndex (const Inventory& inventory, const File& directory)
{
    std::map<File, std::vector<var>> result;
    if (directory == File()) return result;
    const auto path = indexFile (inventory, directory);
    if (! path.existsAsFile() || path.getSize() > 48 * 1024 * 1024) return result;
    const auto index = JSON::parse (path);
    if (static_cast<int> (index.getProperty ("version", 0)) != 1
        || index.getProperty ("root", {}).toString() != inventory.root.getFullPathName()
        || index.getProperty ("selection", {}).toString() != inventory.selection.getFullPathName()
        || index.getProperty ("listing", {}).toString() != inventory.listingFingerprint)
        return result;
    const auto entries = index.getProperty ("definitions", {});
    if (auto* rows = entries.getArray())
        for (const auto& entry : *rows)
        {
            const auto source = entry.getProperty ("source", {});
            const auto pathText = source.getProperty ("path", {}).toString();
            if (File::isAbsolutePath (pathText))
                result[File (pathText)].push_back (entry);
        }
    return result;
}

std::vector<Definition> recoverCached (const File& source, const File& root, const std::vector<var>& cached)
{
    std::vector<Definition> result;
    for (const auto& entry : cached)
    {
        if (! matchesStamp (entry.getProperty ("source", {}))) return {};
        const auto dependencies = entry.getProperty ("dependencies", {});
        const auto* stamps = dependencies.getArray();
        if (stamps == nullptr || ! std::all_of (stamps->begin(), stamps->end(), matchesStamp)) return {};
        Definition item;
        item.source = source;
        item.variant = entry.getProperty ("variant", {}).toString();
        item.compatibility = static_cast<Compatibility> (static_cast<int> (entry.getProperty ("status", 3)));
        if (! canConvert (item.compatibility, true)
            || sampler::readInstrumentDocument (entry.getProperty ("model", {}).toString(), root, item.instrument).failed())
            return {};
        if (const auto issues = entry.getProperty ("issues", {}); issues.isArray())
            for (const auto& issue : *issues.getArray()) item.issues.add (issue.toString());
        if (const auto regions = entry.getProperty ("regions", {}); regions.isArray()) item.regions = *regions.getArray();
        item.decodedBytes = static_cast<std::size_t> (static_cast<int64> (entry.getProperty ("bytes", 0)));
        result.push_back (std::move (item));
    }
    return result;
}
}

const char* statusName (Compatibility status)
{
    switch (status)
    {
        case Compatibility::supported: return "Native Studio compatible";
        case Compatibility::partial: return "Convertible with limitations";
        case Compatibility::originalPlayer: return "Requires original player";
        case Compatibility::unsupported: return "Unsupported or protected";
    }
    return "Unsupported or protected";
}

bool canConvert (Compatibility status, bool allowPartial)
{
    return status == Compatibility::supported || (allowPartial && status == Compatibility::partial);
}

var Inventory::manifest() const
{
    Array<var> instruments;
    for (const auto& item : definitions)
    {
        Array<var> dependencies;
        StringArray paths;
        for (const auto& zone : item.instrument.zones) paths.addIfNotAlreadyThere (zone.file.getRelativePathFrom (root));
        dependencies = strings (paths);
        Array<var> groups;
        for (const auto& group : item.instrument.groups)
        {
            const auto& gate = group.playback;
            groups.add (object ({ { "id", group.id }, { "name", group.name },
                { "trigger", static_cast<int> (gate.trigger) }, { "keyswitch", gate.key }, { "controller", gate.controller },
                { "controllerLow", gate.ccLow }, { "controllerHigh", gate.ccHigh }, { "release", gate.releaseTrigger } }));
        }
        const auto& s = item.instrument.settings;
        instruments.add (object ({ { "source", item.source.getRelativePathFrom (root) }, { "variant", item.variant },
            { "name", item.instrument.name }, { "format", item.source.getFileExtension().substring (1).toLowerCase() },
            { "compatibility", statusName (item.compatibility) }, { "unresolved", strings (item.issues) },
            { "estimatedDecodedBytes", static_cast<int64> (item.decodedBytes) }, { "regions", item.regions },
            { "dependencies", dependencies }, { "groups", groups }, { "envelope", object ({ { "attack", s.attack }, { "decay", s.decay },
                { "sustain", s.sustain }, { "release", s.release } }) } }));
    }
    return object ({ { "schemaVersion", 2 }, { "library", root.getFileName() }, { "complete", complete },
        { "reusedDefinitions", reused }, { "instruments", instruments }, { "samples", samples }, { "unresolved", strings (issues) } });
}

Inventory scan (const File& chosen, Progress& progress, const File& indexDirectory, const File& sampleRoot)
{
    if (chosen.existsAsFile() && chosen.hasFileExtension (pcm))
        return proposeSampleFolder (chosen, progress);
    Inventory inventory;
    if (chosen != File()) inventory.selection = realPath (chosen);
    const auto selection = inventory.selection;
    if (selection == File() || ! selection.exists())
    {
        inventory.issues.add ("Select an existing library folder or instrument.");
        return inventory;
    }
    inventory.root = sampleRoot == File() ? (selection.isDirectory() ? selection : selection.getParentDirectory())
                                          : realPath (sampleRoot);
    if (! inventory.root.isDirectory())
    {
        inventory.issues.add ("The sample library folder is unavailable.");
        return inventory;
    }
    const auto files = discover (inventory, progress);
    const auto cached = readIndex (inventory, indexDirectory);
    for (const auto& file : files.definitions)
    {
        if (progress.cancelled.load()) break;
        if (inventory.definitions.size() >= maxDefinitions)
        {
            inventory.issues.add ("Inventory includes the first 512 recognized instrument definitions.");
            break;
        }
        std::vector<Definition> definitions;
        if (const auto found = cached.find (file); found != cached.end())
            definitions = recoverCached (file, inventory.root, found->second);
        if (! definitions.empty()) inventory.reused += static_cast<int> (definitions.size());
        else
        {
            auto locate = locator (file, inventory.root, files);
            if (file.hasFileExtension ("sf2")) definitions = readSoundFont (file, progress);
            else if (file.hasFileExtension ("exs")) definitions.push_back (readExs (file, locate));
            else if (file.hasFileExtension ("dspreset")) definitions.push_back (readDecent (file, locate));
            else
            {
                Definition item;
                item.source = file;
                item.instrument.name = file.getFileNameWithoutExtension().substring (0, 120);
                if (file.hasFileExtension ("sfz")) analyse (inventory.root, item, progress, locate);
                else if (file.hasFileExtension ("studiosampler"))
                {
                    if (file.getSize() <= 2 * 1024 * 1024 && sampler::readInstrumentDocument (
                        file.loadFileAsString(), file.getParentDirectory(), item.instrument).wasOk())
                        item.compatibility = Compatibility::supported;
                    else item.issues.add ("Unsupported Studio instrument document.");
                }
                else if (file.hasFileExtension (originalFormats))
                {
                    item.compatibility = Compatibility::originalPlayer;
                    item.issues.add ("Recognized instrument format. No native reader is enabled for this container.");
                }
                else item.issues.add ("No readable supported instrument mapping.");
                definitions.push_back (std::move (item));
            }
            for (auto& item : definitions)
                if (! file.hasFileExtension ("sfz")) verifyMapping (item, inventory.root, progress);
        }
        // A partly interpreted multi-preset bank is re-read on resume.
        if (progress.cancelled.load()) break;
        for (auto& item : definitions)
        {
            if (inventory.definitions.size() >= maxDefinitions)
            {
                inventory.issues.addIfNotAlreadyThere ("Inventory includes the first 512 recognized instrument definitions.");
                break;
            }
            inventory.definitions.push_back (std::move (item));
        }
    }
    for (const auto& [name, paths] : files.fileNames)
        if (paths.size() > 1)
            inventory.issues.addIfNotAlreadyThere ("Duplicate sample filename (kept separate): " + name);
    inventory.complete = ! progress.cancelled.load();
    return inventory;
}

Inventory proposeSampleFolder (const File& selected, Progress& progress)
{
    Inventory inventory;
    inventory.selection = selected != File() ? realPath (selected) : File();
    const bool single = inventory.selection.existsAsFile() && inventory.selection.hasFileExtension (pcm);
    inventory.root = single ? inventory.selection.getParentDirectory() : inventory.selection;
    if (! inventory.root.isDirectory()) return inventory;
    Discovered files;
    if (single)
    {
        auto file = inventory.selection;
        files.audio.push_back (file);
        ++progress.completed;
        inventory.samples.add (object ({ { "path", file.getFileName() }, { "format", file.getFileExtension().substring (1).toLowerCase() },
                                        { "fileBytes", file.getSize() }, { "candidateForNativeDecode", true } }));
    }
    else files = discover (inventory, progress);
    Definition item;
    item.source = inventory.selection;
    item.instrument.name = (single ? item.source.getFileNameWithoutExtension() : item.source.getFileName()).substring (0, 120);
    item.compatibility = Compatibility::partial;
    item.issues.add ("Review this inferred keyboard map. Uncertain pitches, overlapping takes, or ambiguous variants require manual editing.");
    int fallback = 48;
    std::vector<sampler::Zone> zones;
    const auto tag = [] (const StringArray& tokens, std::initializer_list<const char*> candidates)
    {
        for (auto* candidate : candidates)
            if (tokens.contains (candidate)) return String (candidate);
        return String();
    };
    for (const auto& file : files.audio)
    {
        if (progress.cancelled.load()) return inventory;
        if (zones.size() == sampler::maxZones) { item.issues.add ("Only the first 128 readable recordings fit one instrument."); break; }
        const auto info = probe (file);
        if (info.frames == 0) { item.issues.addIfNotAlreadyThere ("Unreadable recording: " + file.getFileName()); continue; }
        sampler::Zone z;
        const auto named = sampler::filenameNote (file.getFileName().toStdString());
        z.root = named.value_or (info.root >= 0 ? info.root : fallback);
        if (! named && info.root < 0)
        {
            item.issues.addIfNotAlreadyThere ("Provisional root " + String (z.root) + ": " + file.getRelativePathFrom (inventory.root));
            fallback = jmin (127, fallback + 1);
        }
        if (named && info.root >= 0 && info.root != *named)
            item.issues.addIfNotAlreadyThere ("Filename and embedded pitch disagree: " + file.getFileName());
        if (const auto vel = sampler::filenameVelocity (file.getFileName().toStdString()))
        { z.soft = vel->first; z.hard = vel->second; }
        if (info.loopStart >= 0 && info.loopEnd >= info.loopStart && info.loopEnd < info.frames)
        { z.loop = true; z.loopStart = info.loopStart; z.loopEnd = info.loopEnd + 1; z.loopCrossfade = 0; }
        else if (info.hasEmbeddedLoop)
            item.issues.addIfNotAlreadyThere ("Review unsupported embedded loop: " + file.getFileName());
        z.end = info.frames;
        const auto label = file.getRelativePathFrom (inventory.root);
        const auto tokens = StringArray::fromTokens (label.toLowerCase(), " /_.-", "");
        const auto articulation = tag (tokens, { "sustain", "legato", "staccato", "spiccato", "pizzicato",
                                                "tremolo", "trill", "marcato", "release" });
        const auto microphone = tag (tokens, { "close", "room", "decca", "ambient", "overhead", "spot" });
        auto groupName = articulation;
        if (microphone.isNotEmpty()) groupName += (groupName.isEmpty() ? String() : " / ") + microphone;
        int folder = 0;
        if (groupName.isNotEmpty())
        {
            auto& groups = item.instrument.groups;
            auto group = std::find_if (groups.begin(), groups.end(), [&] (const auto& candidate) { return candidate.name == groupName; });
            if (group != groups.end()) folder = group->id;
            else if (groups.size() < sampler::maxGroups)
            {
                folder = static_cast<int> (groups.size());
                sampler::Group playback;
                playback.id = folder;
                groups.push_back ({ folder, groupName, playback });
            }
            else item.issues.addIfNotAlreadyThere ("Additional labels share the Main group because the native group limit was reached.");
            item.issues.addIfNotAlreadyThere ("Filename and folder tags organize groups. Switches, mic routing and release triggers need review.");
        }
        item.regions.add (object ({ { "sample", label }, { "root", z.root }, { "velocityLow", z.soft },
            { "velocityHigh", z.hard }, { "frames", info.frames }, { "durationSeconds", info.frames / info.rate },
            { "channels", info.channels }, { "articulationTag", articulation }, { "microphoneTag", microphone },
            { "group", folder }, { "loopStart", z.loopStart }, { "loopEnd", z.loopEnd } }));
        z.folder = folder;
        zones.push_back (z);
        item.instrument.zones.push_back ({ file, z, {}, folder, {} });
    }
    sampler::spreadMapping (zones);
    for (std::size_t i = 0; i < zones.size(); ++i)
    {
        auto& z = zones[i];
        item.instrument.zones[i].mapping = z;
        if (auto* row = item.regions[static_cast<int> (i)].getDynamicObject())
        { row->setProperty ("keyLow", z.low); row->setProperty ("keyHigh", z.high); }
        for (std::size_t j = 0; j < i; ++j)
            if (zones[j].root == z.root && zones[j].soft <= z.hard && z.soft <= zones[j].hard)
                item.issues.addIfNotAlreadyThere ("Overlapping layers at MIDI " + String (z.root) + " will play together.");
    }
    verifyMapping (item, inventory.root, progress);
    inventory.definitions.push_back (std::move (item));
    inventory.complete = ! progress.cancelled.load();
    return inventory;
}

void writeIndex (const Inventory& inventory, const File& directory)
{
    if (inventory.listingFingerprint.isEmpty() || ! safeDestination (inventory, directory)
        || directory.createDirectory().failed()) return;
    Array<var> rows;
    for (const auto& item : inventory.definitions)
    {
        if (! canConvert (item.compatibility, true) || ! item.source.existsAsFile()) continue;
        // Do not reuse just the supported pieces of a multi-preset bank that
        // also had an unsupported patch. Keep the complete bank inventory.
        if (item.source.hasFileExtension ("sf2")
            && std::any_of (inventory.definitions.begin(), inventory.definitions.end(), [&] (const auto& other)
                { return other.source == item.source && ! canConvert (other.compatibility, true); })) continue;
        std::set<File> dependencies;
        for (const auto& zone : item.instrument.zones) dependencies.insert (zone.file);
        Array<var> stamps;
        for (const auto& file : dependencies) stamps.add (stamp (file));
        rows.add (object ({ { "source", stamp (item.source) }, { "dependencies", stamps }, { "variant", item.variant },
            { "status", static_cast<int> (item.compatibility) }, { "issues", strings (item.issues) },
            { "regions", item.regions }, { "model", sampler::instrumentDocument (item.instrument, inventory.root) },
            { "bytes", static_cast<int64> (item.decodedBytes) } }));
    }
    TemporaryFile output (indexFile (inventory, directory));
    if (output.getFile().replaceWithText (JSON::toString (object ({ { "version", 1 },
        { "root", inventory.root.getFullPathName() }, { "selection", inventory.selection.getFullPathName() },
        { "listing", inventory.listingFingerprint },
        { "definitions", rows } }), true))
        && output.getFile().getSize() <= 48 * 1024 * 1024)
        output.overwriteTargetFileWithTemporary();
}

Result saveInventory (const Inventory& inventory, const File& directory, File& output)
{
    if (! safeDestination (inventory, directory)) return Result::fail ("Inventory destination overlaps the source library.");
    if (auto result = directory.createDirectory(); result.failed()) return result;
    const auto target = directory.getNonexistentChildFile ("Instrument inventory", ".json");
    TemporaryFile writing (target);
    if (! writing.getFile().replaceWithText (JSON::toString (inventory.manifest())))
        return Result::fail ("Could not write inventory.");
    auto result = publishNewFile (writing.getFile(), target);
    if (result.wasOk()) output = target;
    return result;
}

Result convert (const Inventory& inventory, const Definition& definition, const File& directory,
                bool allowPartial, Progress& progress, File& output)
{
    if (progress.cancelled.load()) return Result::fail ("Conversion cancelled.");
    if (! canConvert (definition.compatibility, allowPartial))
        return Result::fail ("Review the inventory. This definition is not enabled for conversion.");
    if (! safeDestination (inventory, directory)) return Result::fail ("Preset destination overlaps the source library.");
    auto model = definition.instrument;
    for (auto& entry : model.zones)
    {
        const auto resolved = realPath (entry.file);
        if (! regularInside (resolved, inventory.root))
            return Result::fail ("A sample reference changed after scanning. Rescan the library.");
        entry.file = resolved;
    }
    model.preset = model.name;
    model.dirty = false;
    model.sourceLibrary = inventory.root.getFullPathName();
    if (model.sourceInstrument.isEmpty())
        model.sourceInstrument = definition.source.getFullPathName()
            + (definition.variant.isEmpty() ? String() : " [" + definition.variant + "]");
    if (auto result = directory.createDirectory(); result.failed()) return result;
    const auto name = File::createLegalFileName (model.name).substring (0, 120);
    const auto stem = name.isNotEmpty() ? name : String ("Imported instrument");
    const auto document = sampler::instrumentDocument (model, directory);
    // Stable names make repeated batches idempotent for unchanged models.
    const auto target = directory.getChildFile (stem + "-" + String::toHexString (document.hashCode64()).paddedLeft ('0', 16) + ".studiosampler");
    if (target.existsAsFile())
    {
        sampler::Instrument existing;
        if (target.isSymbolicLink() || target.getSize() > 2 * 1024 * 1024
            || sampler::readInstrumentDocument (target.loadFileAsString(), directory, existing).failed()
            || sampler::instrumentDocument (existing, directory) != document)
            return Result::fail ("An existing catalog preset uses this identifier.");
        output = target;
        return Result::ok();
    }
    auto result = sampler::writeImportedPreset (std::move (model), target, progress.cancelled);
    if (result.wasOk()) output = target;
    return result;
}

Result relinkPreset (const File& preset, const File& originalRoot, const File& newRoot, const File& catalog,
                     Progress& progress, File& output)
{
    if (! preset.hasFileExtension ("studiosampler") || ! preset.existsAsFile() || preset.getSize() > 2 * 1024 * 1024)
        return Result::fail ("Select a Studio sampler preset.");
    Inventory inventory;
    inventory.selection = preset;
    inventory.root = realPath (newRoot);
    if (! inventory.root.isDirectory()) return Result::fail ("Select an existing sample library folder.");
    Definition item;
    item.source = preset;
    item.compatibility = Compatibility::supported;
    auto result = sampler::readInstrumentDocument (preset.loadFileAsString(), preset.getParentDirectory(), item.instrument);
    if (result.failed()) return result;
    const auto libraryPath = item.instrument.sourceLibrary;
    const auto oldRoot = originalRoot != File() ? realPath (originalRoot)
        : File::isAbsolutePath (libraryPath) ? File (libraryPath) : File();
    if (oldRoot == File()) return Result::fail ("This preset has no recorded original root. Import its source definition again.");
    for (auto& zone : item.instrument.zones)
    {
        if (! zone.file.isAChildOf (oldRoot)) return Result::fail ("The selected original root does not contain every preset reference.");
        zone.file = inventory.root.getChildFile (zone.file.getRelativePathFrom (oldRoot));
    }
    // The original preset may already be in the app's catalog. All output
    // still goes to a new file and the selected sample tree stays read-only.
    inventory.selection = inventory.root;
    verifyMapping (item, inventory.root, progress);
    return convert (inventory, item, catalog, false, progress, output);
}
}
