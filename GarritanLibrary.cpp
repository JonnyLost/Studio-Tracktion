// SPDX-License-Identifier: GPL-3.0-or-later
#include "GarritanLibrary.h"
#include "ComposerFiles.h"
#include <charconv>
#include <cmath>
#include <filesystem>
#include <map>
#include <optional>
#include <sstream>
#include <cctype>
#include <tuple>

namespace studio::garritan
{
using namespace juce;
namespace fs = std::filesystem;
namespace
{
using Opcodes = std::map<String, String>;
constexpr auto pcm = "wav;wave;aif;aiff;aifc";
constexpr auto otherSamples = "flac;ogg;mp3;wv;ncw;audio;sample;samples;caf";
constexpr auto opaqueDefinitions = "aria;ariax;bank;sf2;sfark;gig;nki;nkm;exs;aupreset;xml;patch;instrument";
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
            if (effective.size() > 128)
            {
                issues.add ("A region exceeds the 128-opcode inventory limit.");
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
    int loopStart = -1, loopEnd = -1;
};

SampleInfo probe (const File& file)
{
    if (! file.hasFileExtension (pcm)) return {};
    AudioFormatManager formats;
    formats.registerFormat (new WavAudioFormat(), true);
    formats.registerFormat (new AiffAudioFormat(), false);
    std::unique_ptr<AudioFormatReader> reader (formats.createReaderFor (file));
    if (! reader || reader->numChannels < 1 || reader->numChannels > 2
        || reader->sampleRate < 1000 || reader->sampleRate > 768000 || ! std::isfinite (reader->sampleRate)
        || reader->lengthInSamples < 2 || reader->lengthInSamples > static_cast<int64> (sampler::maxSampleBytes / (sizeof (float) * reader->numChannels)))
        return {};
    const auto frames = static_cast<int> (reader->lengthInSamples);
    SampleInfo info { frames, static_cast<std::size_t> (frames) * sizeof (float) * reader->numChannels };
    const auto& metadata = reader->metadataValues;
    const auto field = [&] (const char* name, const char* fallback = "0") { return metadata.getValue (name, fallback).getIntValue(); };
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

void analyse (const File& root, Definition& definition, Progress& progress)
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
        const auto file = sample.isEmpty() || sample.startsWithChar ('*') || File::isAbsolutePath (sample)
            || File::isAbsolutePath (prefix) ? File() : realPath (raw);
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
    const auto output = realPath (directory);
    return output != File() && output != inventory.root && ! output.isAChildOf (inventory.root);
}
}

const char* statusName (Compatibility status)
{
    switch (status)
    {
        case Compatibility::supported: return "Fully supported";
        case Compatibility::partial: return "Partially supported";
        case Compatibility::unsupported: return "Unsupported";
    }
    return "Unsupported";
}

var Inventory::manifest() const
{
    Array<var> instruments;
    for (const auto& item : definitions)
        instruments.add (object ({ { "source", item.source.getRelativePathFrom (root) },
            { "name", item.instrument.name }, { "format", item.source.getFileExtension().substring (1).toLowerCase() },
            { "compatibility", statusName (item.compatibility) }, { "unresolved", strings (item.issues) },
            { "estimatedDecodedBytes", static_cast<int64> (item.decodedBytes) }, { "regions", item.regions } }));
    return object ({ { "schemaVersion", 1 }, { "library", root.getFileName() }, { "instruments", instruments },
                     { "samples", samples }, { "unresolved", strings (issues) } });
}

Inventory scan (const File& chosen, Progress& progress)
{
    Inventory result;
    if (chosen != File()) result.root = realPath (chosen);
    if (result.root == File() || ! result.root.isDirectory())
    {
        result.issues.add ("Select an existing local library directory.");
        return result;
    }
    std::error_code error;
    fs::recursive_directory_iterator it (pathOf (result.root), fs::directory_options::none, error), end;
    while (! error && it != end)
    {
        if (progress.cancelled.load()) return result;
        if (++progress.completed > maxFiles) { result.issues.add ("Scan stopped at the 50,000-entry limit."); break; }
        const auto type = it->symlink_status (error);
        if (error) break;
        if (fs::is_regular_file (type))
        {
            const auto file = fileOf (it->path());
            if (file.hasFileExtension (pcm) || file.hasFileExtension (otherSamples))
                result.samples.add (object ({ { "path", file.getRelativePathFrom (result.root) },
                    { "format", file.getFileExtension().substring (1).toLowerCase() },
                    { "fileBytes", file.getSize() }, { "candidateForNativeDecode", file.hasFileExtension (pcm) } }));
            else if (file.hasFileExtension ("sfz") || file.hasFileExtension (opaqueDefinitions))
            {
                if (result.definitions.size() >= maxDefinitions)
                    result.issues.addIfNotAlreadyThere ("Inventory includes the first 512 recognized definitions.");
                else
                {
                    Definition definition;
                    definition.source = file;
                    definition.instrument.name = file.getFileNameWithoutExtension().substring (0, 120);
                    if (file.hasFileExtension ("sfz")) analyse (result.root, definition, progress);
                    else definition.issues.add ("No supported plain-text sample mapping. Use the source player.");
                    result.definitions.push_back (std::move (definition));
                }
            }
        }
        it.increment (error);
    }
    if (error) result.issues.add ("Traversal stopped at an unreadable entry: " + String (error.message()));
    return result;
}

Result saveInventory (const Inventory& inventory, const File& directory, File& output)
{
    if (! safeDestination (inventory, directory)) return Result::fail ("Inventory destination overlaps the source library.");
    if (auto result = directory.createDirectory(); result.failed()) return result;
    const auto target = directory.getNonexistentChildFile ("Garritan inventory", ".json");
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
    if (definition.compatibility == Compatibility::unsupported
        || (definition.compatibility == Compatibility::partial && ! allowPartial))
        return Result::fail ("Review the inventory. This definition is not enabled for conversion.");
    if (! safeDestination (inventory, directory)) return Result::fail ("Preset destination overlaps the source library.");
    for (const auto& entry : definition.instrument.zones)
    {
        std::error_code error;
        if (realPath (entry.file) != entry.file || ! entry.file.isAChildOf (inventory.root)
            || ! fs::is_regular_file (pathOf (entry.file), error))
            return Result::fail ("A sample reference changed after scanning. Rescan the library.");
    }
    if (auto result = directory.createDirectory(); result.failed()) return result;
    const auto name = File::createLegalFileName (definition.instrument.name);
    const auto target = directory.getNonexistentChildFile (name.isNotEmpty() ? name : "Imported instrument", ".studiosampler");
    auto result = sampler::writeImportedPreset (definition.instrument, target, progress.cancelled);
    if (result.wasOk()) output = target;
    return result;
}
}
