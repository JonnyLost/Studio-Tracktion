// SPDX-License-Identifier: GPL-3.0-or-later
#include "InstrumentReaders.h"
#include <bit>
#include <charconv>
#include <cmath>
#include <map>

namespace studio::instrumentimport
{
using namespace juce;
namespace
{
Definition basic (const File& file)
{
    Definition result;
    result.source = file;
    result.instrument.name = file.getFileNameWithoutExtension().substring (0, 120);
    result.instrument.settings.gainDb = 0;
    return result;
}

Definition failed (const File& file, const String& reason)
{
    auto result = basic (file);
    result.issues.add (reason);
    return result;
}

String binaryName (const unsigned char* bytes, int length)
{
    auto size = 0;
    while (size < length && bytes[size] != 0) ++size;
    return String::fromUTF8 (reinterpret_cast<const char*> (bytes), size).trim();
}

int byteSigned (unsigned char value) { return value < 128 ? value : value - 256; }

using Generators = std::map<int, uint16_t>;
int signedValue (uint16_t bits) { return std::bit_cast<int16_t> (bits); }

struct HydraTable
{
    MemoryBlock bytes;
    int width;
    int count() const { return static_cast<int> (bytes.getSize()) / width; }
    const unsigned char* operator[] (int row) const
    {
        return static_cast<const unsigned char*> (bytes.getData()) + row * width;
    }
    int word (int row, int offset) const { return ByteOrder::littleEndianShort ((*this)[row] + offset); }

    bool read (InputStream& stream, sampler::SoundFontChunk pdta, const char* tag)
    {
        const auto chunk = sampler::findRiffChunk (stream, pdta.offset, pdta.offset + pdta.size, tag);
        return chunk && chunk.size >= width && chunk.size % width == 0
            && chunk.size / width <= 65536 && stream.setPosition (chunk.offset)
            && stream.readIntoMemoryBlock (bytes, static_cast<ssize_t> (chunk.size)) == chunk.size;
    }
};

bool bags (const HydraTable& bag, const HydraTable& gen, int start, int end, int terminalOp,
           std::vector<Generators>& zones, bool& modulators)
{
    if (start >= end || end >= bag.count()) return false;
    Generators global;
    for (auto i = start; i < end; ++i)
    {
        auto first = bag.word (i, 0), last = bag.word (i + 1, 0);
        if (first > last || last >= gen.count() || zones.size() >= sampler::maxZones) return false;
        if (bag.word (i, 2) != bag.word (i + 1, 2)) modulators = true;
        Generators local;
        for (auto j = first; j < last; ++j)
            local[gen.word (j, 0)] = static_cast<uint16_t> (gen.word (j, 2));
        if (! local.contains (terminalOp))
        {
            if (i != start) return false;
            global = std::move (local);
        }
        else
        {
            auto combined = global;
            for (auto [op, amount] : local) combined[op] = amount;
            zones.push_back (std::move (combined));
        }
    }
    return ! zones.empty();
}

int generator (const Generators& zone, int op, int fallback = 0)
{
    const auto found = zone.find (op);
    return found == zone.end() ? fallback : signedValue (found->second);
}

bool addSoundFontZone (Definition& result, const File& file, sampler::SoundFontChunk pcm,
                       const HydraTable& samples, const Generators& preset, const Generators& source,
                       std::optional<std::array<float, 4>>& envelope)
{
    const auto sampleId = generator (source, 53, -1);
    if (sampleId < 0 || sampleId >= samples.count() - 1) return false;
    const auto* header = samples[sampleId];
    const auto u32 = [&] (int offset) { return ByteOrder::littleEndianInt (header + offset); };
    if (samples.word (sampleId, 44) != 1)
    {
        result.issues.addIfNotAlreadyThere ("Linked stereo and ROM SoundFont samples require another player.");
        return false;
    }
    const auto first = u32 (20), after = u32 (24), rate = u32 (36);
    if (first >= after || uint64_t (after) * 2 > static_cast<uint64_t> (pcm.size)
        || after - first > sampler::maxSampleBytes / sizeof (float) || after - first < 2
        || rate < 1000 || rate > 768000)
        return false;

    sampler::SampleZone item;
    item.file = file;
    item.soundFont = sampler::SoundFontSample { pcm.offset + int64 (first) * 2,
        static_cast<int> (after - first), static_cast<int> (rate) };
    auto& zone = item.mapping;
    const auto add = [&] (int op, int fallback = 0) { return generator (source, op, fallback) + generator (preset, op); };
    const auto intersection = [&] (int op, int& low, int& high)
    {
        const auto get = [op] (const Generators& values)
        {
            auto it = values.find (op);
            return it == values.end() ? 0x7f00 : static_cast<int> (it->second);
        };
        const auto a = get (preset), b = get (source);
        low = jmax (low, jmax (a & 255, b & 255));
        high = jmin (high, jmin (a >> 8, b >> 8));
    };
    intersection (43, zone.low, zone.high);
    intersection (44, zone.soft, zone.hard);
    if (zone.low > zone.high || zone.soft > zone.hard) return true; // empty intersection

    zone.root = generator (source, 58, header[40]);
    if (zone.root == 255) zone.root = 60;
    const auto coarse = add (51) * 100;
    const auto tuning = coarse + add (52) + byteSigned (header[41]);
    zone.cents = static_cast<float> (jlimit (-100, 100, tuning));
    if (tuning != zone.cents) result.issues.addIfNotAlreadyThere ("Tuning outside the native zone range was limited.");
    const auto attenuation = add (48) * -0.1f;
    zone.gainDb = jlimit (-60.0f, 12.0f, attenuation);
    if (zone.gainDb != attenuation) result.issues.addIfNotAlreadyThere ("Attenuation outside the native range was limited.");

    // The stored PCM slice includes address offsets so signed offsets can use
    // adjacent PCM frames while remaining inside the RIFF sample chunk.
    const int64 start = int64 (first) + add (0) + int64 (add (4)) * 32768;
    const int64 end = int64 (after) + add (1) + int64 (add (12)) * 32768;
    if (start < 0 || end - start < 2 || end * 2 > pcm.size
        || end - start > static_cast<int64> (sampler::maxSampleBytes / sizeof (float)))
        return false;
    item.soundFont->offset = pcm.offset + start * 2;
    item.soundFont->frames = static_cast<int> (end - start);
    zone.end = item.soundFont->frames;
    const auto mode = generator (source, 54);
    zone.loop = mode == 1 || mode == 3;
    if (mode == 3) result.issues.addIfNotAlreadyThere ("Sustain loops continue through the native release envelope.");
    if (zone.loop)
    {
        const int64 loopStart = int64 (u32 (28)) + add (2) + int64 (add (45)) * 32768 - start;
        const int64 loopEnd = int64 (u32 (32)) + add (3) + int64 (add (50)) * 32768 - start;
        if (loopStart < 0 || loopStart + 2 > loopEnd || loopEnd > zone.end) return false;
        zone.loopStart = static_cast<int> (loopStart);
        zone.loopEnd = static_cast<int> (loopEnd);
        zone.loopCrossfade = 0;
    }
    const auto seconds = [&] (int op, double cap)
    {
        return static_cast<float> (jlimit (0.0, cap, std::exp2 (add (op, -12000) / 1200.0)));
    };
    const std::array<float, 4> adsr { seconds (34, 10), seconds (36, 10),
        static_cast<float> (jlimit (0.0, 1.0, std::pow (10.0, -add (37) / 200.0))), seconds (38, 15) };
    if (! envelope) envelope = adsr;
    else if (*envelope != adsr) result.issues.addIfNotAlreadyThere ("Multiple SoundFont envelopes use the first supported zone's ADSR.");
    constexpr int handled[] { 0,1,2,3,4,12,34,36,37,38,41,43,44,45,48,50,51,52,53,54,58 };
    for (const auto* values : { &source, &preset })
        for (const auto& [op, amount] : *values)
            if (amount != 0 && std::find (std::begin (handled), std::end (handled), op) == std::end (handled))
                result.issues.addIfNotAlreadyThere ("SoundFont generator " + String (op) + " requires the original player for exact playback.");
    if (! sampler::validMapping (zone) || result.instrument.zones.size() >= sampler::maxZones) return false;
    result.instrument.zones.push_back (std::move (item));
    return true;
}
}

std::vector<Definition> readSoundFont (const File& file, Progress& progress)
{
    auto stream = file.createInputStream();
    const auto end = stream ? sampler::soundFontEnd (*stream) : 0;
    if (! end) return { failed (file, "Invalid SoundFont RIFF header.") };
    const auto pcm = sampler::soundFontPCM (*stream, end);
    const auto pdta = sampler::findRiffChunk (*stream, 12, end, "LIST", "pdta");
    const auto sdta = sampler::findRiffChunk (*stream, 12, end, "LIST", "sdta");
    const auto info = sampler::findRiffChunk (*stream, 12, end, "LIST", "INFO");
    const auto version = info ? sampler::findRiffChunk (*stream, info.offset, info.offset + info.size, "ifil") : sampler::SoundFontChunk {};
    if (! version || version.size < 4 || ! stream->setPosition (version.offset) || stream->readShort() != 2)
        return { failed (file, "Only SoundFont 2 with readable PCM is supported.") };
    HydraTable phdr { {}, 38 }, pbag { {}, 4 }, pgen { {}, 4 }, inst { {}, 22 },
               ibag { {}, 4 }, igen { {}, 4 }, shdr { {}, 46 };
    if (! pcm || ! pdta || ! phdr.read (*stream, pdta, "phdr") || ! pbag.read (*stream, pdta, "pbag")
        || ! pgen.read (*stream, pdta, "pgen") || ! inst.read (*stream, pdta, "inst")
        || ! ibag.read (*stream, pdta, "ibag") || ! igen.read (*stream, pdta, "igen")
        || ! shdr.read (*stream, pdta, "shdr") || phdr.count() < 2 || inst.count() < 2 || shdr.count() < 2)
        return { failed (file, "Incomplete or invalid SoundFont tables.") };

    const bool extraPrecision = sdta && sampler::findRiffChunk (*stream, sdta.offset, sdta.offset + sdta.size, "sm24");
    std::vector<Definition> result;
    for (int p = 0; p < phdr.count() - 1 && p < 512 && ! progress.cancelled.load(); ++p)
    {
        auto item = basic (file);
        const auto name = binaryName (phdr[p], 20);
        if (name.isNotEmpty()) item.instrument.name = name;
        item.variant = String (phdr.word (p, 22)) + ":" + String (phdr.word (p, 20)) + ":" + String (p);
        if (extraPrecision) item.issues.add ("Converted using the 16-bit PCM portion of a 24-bit SoundFont.");
        std::optional<std::array<float, 4>> envelope;
        std::vector<Generators> layers;
        bool modulators = false;
        bool valid = bags (pbag, pgen, phdr.word (p, 24), phdr.word (p + 1, 24), 41, layers, modulators);
        for (const auto& layer : layers)
        {
            const auto index = generator (layer, 41, -1);
            std::vector<Generators> zones;
            if (index < 0 || index >= inst.count() - 1 || ! bags (ibag, igen, inst.word (index, 20),
                inst.word (index + 1, 20), 53, zones, modulators)) { valid = false; break; }
            for (const auto& zone : zones)
                if (! addSoundFontZone (item, file, pcm, shdr, layer, zone, envelope)) { valid = false; break; }
            if (! valid || progress.cancelled.load()) break;
        }
        if (modulators) item.issues.add ("Explicit SoundFont modulators are recorded as a conversion limitation.");
        if (valid && ! item.instrument.zones.empty() && envelope)
        {
            const auto& a = *envelope;
            auto& s = item.instrument.settings;
            s.attack = a[0]; s.decay = a[1]; s.sustain = a[2]; s.release = a[3];
            item.compatibility = item.issues.isEmpty() ? Compatibility::supported : Compatibility::partial;
        }
        else item.issues.add ("Invalid ranges, unsupported samples, or too many zones in this preset.");
        result.push_back (std::move (item));
    }
    return result;
}

Definition readExs (const File& file, const SampleLocator& locate)
{
    MemoryBlock bytes;
    if (file.getSize() < 84 || file.getSize() > 8 * 1024 * 1024 || ! file.loadFileAsData (bytes))
        return failed (file, "Invalid or oversized EXS definition.");
    auto result = basic (file);
    const auto* data = static_cast<const unsigned char*> (bytes.getData());
    std::vector<File> samples;
    std::vector<std::pair<sampler::Zone, unsigned>> mappings;
    bool instrument = false;
    for (std::size_t i = 0; i < bytes.getSize();)
    {
        if (bytes.getSize() - i < 84) return failed (file, "Truncated EXS block.");
        const auto* block = data + i;
        const bool big = block[0] == 0;
        const auto magic = std::string_view (reinterpret_cast<const char*> (block + 16), 4);
        if (block[1] != 1 || block[2] != 0
            || (big ? (magic != "SOBT" && magic != "SOBJ") : (magic != "TBOS" && magic != "JBOS")))
            return failed (file, "Unrecognized EXS container.");
        const auto word = [=] (int offset)
        { return big ? ByteOrder::bigEndianInt (block + offset) : ByteOrder::littleEndianInt (block + offset); };
        const auto size = (block[3] & 128) ? word (4) & 32767u : word (4);
        if (size > bytes.getSize() - i - 84) return failed (file, "EXS block exceeds the file.");
        const int type = block[3] & 15;
        if (i == 0 && type != 0) return failed (file, "EXS instrument header is missing.");
        i += 84 + size;
        if (type == 0)
        {
            if (instrument) return failed (file, "Multiple EXS instrument headers.");
            instrument = true;
            const auto name = binaryName (block + 20, 64);
            if (name.isNotEmpty()) result.instrument.name = name;
        }
        else if (type == 1)
        {
            if (size < 104 || mappings.size() >= sampler::maxZones) return failed (file, "Unsupported EXS zone layout or zone count.");
            const auto flags = block[84];
            if ((flags & 16) != 0) { result.issues.addIfNotAlreadyThere ("Muted EXS zones were omitted."); continue; }
            if ((flags & 6) != 0) return failed (file, "Reverse or fixed-pitch EXS zones require the original player.");
            sampler::Zone zone;
            zone.oneShot = (flags & 1) != 0;
            zone.root = block[85];
            zone.low = block[90]; zone.high = block[91];
            zone.soft = jmax (1, static_cast<int> (block[93]));
            zone.hard = block[94] == 0 && (flags & 8) == 0 ? 127 : block[94];
            const int tuning = byteSigned (block[86]) + byteSigned (block[164]) * 100;
            zone.cents = static_cast<float> (jlimit (-100, 100, tuning));
            zone.gainDb = static_cast<float> (jlimit (-60, 12, byteSigned (block[88])));
            if (zone.cents != tuning || zone.gainDb != byteSigned (block[88]) || block[87] || block[89] || (flags & 64))
                result.issues.addIfNotAlreadyThere ("Some EXS tuning, gain, pan, or output settings exceed native mapping.");
            zone.start = static_cast<int> (word (96)); zone.end = static_cast<int> (word (100));
            zone.loop = (block[117] & 1) != 0;
            if (zone.loop)
            {
                zone.loopStart = static_cast<int> (word (104)); zone.loopEnd = static_cast<int> (word (108));
                zone.loopCrossfade = static_cast<int> (word (112));
                if (block[116] || (block[117] & 6) != 4 || block[118])
                    result.issues.addIfNotAlreadyThere ("Advanced EXS loop playback uses Studio's native forward loop behavior.");
            }
            if ((block[161] & 1) != 0 || (size >= 128 && word (196) > word (192))
                || (size >= 104 && word (172) != 0 && word (172) != 0xffffffffu))
                result.issues.addIfNotAlreadyThere ("EXS Flex, tails and group controls are not translated.");
            if (! sampler::validMapping (zone)) return failed (file, "Invalid EXS key, velocity or loop crossfade.");
            mappings.emplace_back (zone, word (176));
        }
        else if (type == 3)
        {
            if (size < 336 || samples.size() >= 4096) return failed (file, "Unsupported EXS sample directory.");
            auto name = size >= 592 ? binaryName (block + 420, 256) : String();
            if (name.isEmpty()) name = binaryName (block + 20, 64);
            const auto path = binaryName (block + 164, 256);
            auto sample = name.isEmpty() || path.isEmpty() ? File() : locate (path + "/" + name);
            if (sample == File() && name.isNotEmpty()) sample = locate (name);
            if (sample == File()) result.issues.addIfNotAlreadyThere ("Missing or ambiguous EXS sample: " + name);
            samples.push_back (sample);
        }
        else if (type == 2 || type == 4)
            result.issues.addIfNotAlreadyThere ("EXS group processing and instrument parameters require review.");
        else if (type != 5 && type != 6 && type != 7 && type != 8 && type != 9 && type != 10 && type != 11)
            result.issues.addIfNotAlreadyThere ("Unrecognized EXS extension block.");
    }
    for (auto& [zone, index] : mappings)
    {
        if (index >= samples.size() || samples[index] == File())
        {
            result.issues.addIfNotAlreadyThere ("An EXS zone references a missing sample.");
            return result;
        }
        result.instrument.zones.push_back ({ samples[index], zone, {}, 0, {} });
    }
    if (result.instrument.zones.empty()) result.issues.add ("No eligible EXS zones.");
    else
    {
        result.compatibility = Compatibility::partial;
        result.issues.add ("Experimental EXS zone reconstruction. Verify playback against Logic.");
    }
    return result;
}

Definition readDecent (const File& file, const SampleLocator& locate)
{
    auto result = basic (file);
    if (file.getSize() <= 0 || file.getSize() > 2 * 1024 * 1024)
        return failed (file, "Invalid or oversized Decent Sampler definition.");
    const auto text = file.loadFileAsString();
    const auto xml = text.containsIgnoreCase ("<!DOCTYPE") ? nullptr : XmlDocument::parse (text);
    if (xml == nullptr || ! xml->hasTagName ("DecentSampler"))
        return failed (file, "A readable, standalone DecentSampler XML document is required.");
    const auto* groups = xml->getChildByName ("groups");
    if (groups == nullptr) return failed (file, "Decent Sampler groups are missing.");
    const auto authoredName = xml->getStringAttribute ("name");
    if (authoredName.isNotEmpty()) result.instrument.name = authoredName.substring (0, 120);
    bool valid = true;
    for (auto* group : groups->getChildIterator())
    {
        if (! group->hasTagName ("group")) { valid = false; continue; }
        for (auto* child : group->getChildIterator())
        {
            if (! child->hasTagName ("sample")) { valid = false; continue; }
            if (result.instrument.zones.size() >= sampler::maxZones)
                return failed (file, "Decent Sampler mapping exceeds Studio's zone limit.");
            const auto attribute = [&] (const char* key, String fallback = {})
            { return child->getStringAttribute (key, group->getStringAttribute (key, groups->getStringAttribute (key, fallback))); };
            const auto number = [&] (const char* key, int fallback)
            {
                const auto input = attribute (key);
                if (input.isEmpty()) return fallback;
                const auto chars = input.toStdString();
                int value = 0;
                const auto end = chars.data() + chars.size();
                const auto parsed = std::from_chars (chars.data(), end, value);
                if (parsed.ec != std::errc() || parsed.ptr != end) valid = false;
                return value;
            };
            const auto path = attribute ("path");
            const auto sample = path.isNotEmpty() ? locate (path) : File();
            if (sample == File()) { result.issues.addIfNotAlreadyThere ("Missing or ambiguous sample: " + path); valid = false; continue; }
            sampler::Zone zone;
            zone.low = number ("loNote", 0); zone.high = number ("hiNote", 127); zone.root = number ("rootNote", 60);
            zone.soft = number ("loVel", 1); zone.hard = number ("hiVel", 127);
            zone.start = number ("start", 0); zone.end = number ("end", 0);
            zone.loop = attribute ("loopEnabled") == "true";
            if (zone.loop)
            {
                zone.loopStart = number ("loopStart", 0); zone.loopEnd = number ("loopEnd", -1);
                zone.loopCrossfade = number ("loopCrossfade", 0);
            }
            if (! sampler::validMapping (zone)) valid = false;
            result.instrument.zones.push_back ({ sample, zone, {}, 0, {} });
        }
    }
    if (! valid || result.instrument.zones.empty())
        result.issues.add ("The preset has missing samples or unsupported mapping data.");
    else
    {
        result.compatibility = Compatibility::partial;
        result.issues.add ("Experimental Decent Sampler subset. Bindings, effects, group conditions and custom playback controls require review.");
    }
    return result;
}
}
