// SPDX-License-Identifier: GPL-3.0-or-later
#include "../InstrumentLibrary.h"
#include <cstdio>

namespace studio::instrumentimport
{
using namespace juce;
namespace
{
using Bytes = std::vector<unsigned char>;

void number (Bytes& data, std::size_t offset, uint32_t value, int width = 2)
{
    for (int i = 0; i < width; ++i) data.at (offset + static_cast<std::size_t> (i)) = static_cast<unsigned char> (value >> (i * 8));
}

void name (Bytes& data, std::size_t offset, std::string_view text)
{
    std::copy (text.begin(), text.end(), data.begin() + static_cast<std::ptrdiff_t> (offset));
}

Bytes chunk (std::string_view id, const Bytes& payload)
{
    Bytes result (8);
    name (result, 0, id);
    number (result, 4, static_cast<uint32_t> (payload.size()), 4);
    result.insert (result.end(), payload.begin(), payload.end());
    if (payload.size() % 2 != 0) result.push_back (0);
    return result;
}

Bytes list (std::string_view kind, std::initializer_list<Bytes> children)
{
    Bytes result (kind.begin(), kind.end());
    for (const auto& child : children) result.insert (result.end(), child.begin(), child.end());
    return result;
}

Bytes soundFont()
{
    Bytes phdr (38 * 3), pbag (4 * 3), pgen (4 * 3), inst (22 * 2), ibag (4 * 2), igen (4 * 5), shdr (46 * 2);
    name (phdr, 0, "Test Strings");
    name (phdr, 38, "Second Strings");
    number (phdr, 38 + 20, 1); number (phdr, 38 + 24, 1);
    name (phdr, 76, "EOP"); number (phdr, 76 + 24, 2);
    number (pbag, 4, 1); number (pbag, 8, 2);
    number (pgen, 0, 41); number (pgen, 4, 41);
    name (inst, 0, "Instrument");
    name (inst, 22, "EOI"); number (inst, 42, 1);
    number (ibag, 4, 4);
    number (igen, 0, 43); number (igen, 2, 60 | (72 << 8));
    number (igen, 4, 44); number (igen, 6, 30 | (110 << 8));
    number (igen, 8, 54); number (igen, 10, 1);
    number (igen, 12, 53);
    name (shdr, 0, "Mono tone");
    number (shdr, 24, 2400, 4); number (shdr, 28, 60, 4); number (shdr, 32, 2000, 4);
    number (shdr, 36, 48000, 4); shdr[40] = 60; number (shdr, 44, 1);
    name (shdr, 46, "EOS");
    Bytes pcm (2 * 2446);
    for (std::size_t i = 0; i < 2400; ++i) number (pcm, i * 2, 8192);
    return chunk ("RIFF", list ("sfbk", {
        chunk ("LIST", list ("INFO", { chunk ("ifil", { 2, 0, 1, 0 }) })),
        chunk ("LIST", list ("sdta", { chunk ("smpl", pcm) })),
        chunk ("LIST", list ("pdta", { chunk ("phdr", phdr), chunk ("pbag", pbag), chunk ("pgen", pgen),
            chunk ("inst", inst), chunk ("ibag", ibag), chunk ("igen", igen), chunk ("shdr", shdr) }))
    }));
}

Bytes exs (bool big = false)
{
    Bytes result;
    const auto append = [&] (int type, int size, const char* label)
    {
        Bytes block (static_cast<std::size_t> (size) + 84);
        block[0] = big ? 0 : 1;
        block[1] = 1; block[3] = static_cast<unsigned char> (type);
        name (block, 16, big ? "SOBT" : "TBOS"); name (block, 20, label);
        const auto offset = result.size();
        result.insert (result.end(), block.begin(), block.end());
        return offset;
    };
    const auto word = [&] (std::size_t at, uint32_t value)
    {
        if (big) value = ByteOrder::swap (value);
        number (result, at, value, 4);
    };
    append (0, 0, "EXS fixture");
    const auto zone = append (1, 104, "Zone");
    const auto sample = append (3, 336, "Piano_C4_v35-95.flac");
    word (zone + 4, 104);
    word (sample + 4, 336);
    result[zone + 84] = 8; result[zone + 85] = 60;
    result[zone + 90] = 55; result[zone + 91] = 64;
    result[zone + 93] = 35; result[zone + 94] = 95;
    word (zone + 96, 10); word (zone + 100, 1990);
    word (zone + 104, 20); word (zone + 108, 1900); word (zone + 112, 10);
    result[zone + 117] = 5;
    return result;
}

bool put (const File& file, const Bytes& bytes) { return file.replaceWithData (bytes.data(), bytes.size()); }

template <typename Format> bool writeAudio (const File& file)
{
    Format format;
    std::unique_ptr<OutputStream> stream = file.createOutputStream();
    auto writer = format.createWriterFor (stream, AudioFormatWriterOptions()
        .withSampleRate (48000).withNumChannels (1).withBitsPerSample (16));
    AudioBuffer<float> buffer (1, 2400);
    for (int i = 0; i < buffer.getNumSamples(); ++i) buffer.setSample (0, i, 0.2f);
    return writer != nullptr && writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
}
}

int runUniversalImportTest()
{
    const auto fail = [] (const char* error)
    {
        std::fprintf (stderr, "Universal import: %s\n", error);
        return 1;
    };
    TemporaryFile scratch ("-universal-import");
    const auto home = scratch.getFile();
    ScopeGuard cleanup { [&] { home.deleteRecursively(); } };
    const auto library = home.getChildFile ("Library");
    const auto catalog = home.getChildFile ("Catalog");
    const auto index = home.getChildFile ("Index");
    const auto audio = library.getChildFile ("Audio");
    if (audio.createDirectory().failed() || ! writeAudio<FlacAudioFormat> (audio.getChildFile ("Piano_C4_v35-95.flac"))
        || ! writeAudio<AiffAudioFormat> (audio.getChildFile ("Unknown.aiff")))
        return fail ("create synthetic recordings");
   #if JUCE_MAC
    {
        const auto caf = home.getChildFile ("Synthetic CAF.caf");
        MemoryOutputStream bytes;
        bytes.write ("caff", 4); bytes.writeShortBigEndian (1); bytes.writeShortBigEndian (0);
        bytes.write ("desc", 4); bytes.writeInt64BigEndian (32);
        bytes.writeDoubleBigEndian (48000.0);
        bytes.write ("lpcm", 4);
        for (const auto field : { 2, 2, 1, 1, 16 }) bytes.writeIntBigEndian (field);
        bytes.write ("data", 4); bytes.writeInt64BigEndian (4804); bytes.writeIntBigEndian (0);
        for (int i = 0; i < 2400; ++i) bytes.writeShort (8192);
        if (! caf.replaceWithData (bytes.getData(), bytes.getDataSize())) return fail ("write CAF fixture");
        Progress cafProgress;
        const auto cafInventory = scan (caf, cafProgress);
        if (cafInventory.definitions.size() != 1 || cafInventory.definitions[0].decodedBytes != 2400 * sizeof (float)
            || cafInventory.definitions[0].compatibility != Compatibility::partial)
            return fail ("inspect CAF through the macOS decoder");
    }
   #endif
    const auto bank = library.getChildFile ("Bank.sf2");
    const auto legacy = library.getChildFile ("Legacy.exs");
    const auto big = library.getChildFile ("Big.exs");
    const auto decent = library.getChildFile ("Decent.dspreset");
    if (! put (bank, soundFont()) || ! put (legacy, exs()) || ! put (big, exs (true))
        || ! decent.replaceWithText (R"(<DecentSampler><groups><group loVel="35" hiVel="95"><sample path="Audio/Piano_C4_v35-95.flac" rootNote="60" loNote="50" hiNote="65" loopEnabled="true" loopStart="20" loopEnd="1900"/></group></groups></DecentSampler>)")
        || ! library.getChildFile ("Kontakt.nki").replaceWithText ("opaque fixture")
        || ! library.getChildFile ("Broken.sf2").replaceWithText ("not RIFF"))
        return fail ("write definitions");

    Progress progress;
    auto inventory = scan (library, progress);
    if (! inventory.complete || inventory.definitions.size() != 7 || inventory.samples.size() != 2)
        return fail ("scan format catalog and both bank presets");
    const auto select = [&] (const File& file) -> const Definition&
    {
        return *std::find_if (inventory.definitions.begin(), inventory.definitions.end(),
            [&] (const Definition& item) { return item.source.getFileName() == file.getFileName(); });
    };
    const auto& sf = select (bank);
    const auto& ex = select (legacy);
    const auto& ds = select (decent);
    if (sf.compatibility != Compatibility::supported || sf.instrument.zones.front().mapping.low != 60
        || sf.instrument.zones.front().mapping.soft != 30 || sf.instrument.zones.front().mapping.loopEnd != 2000
        || sf.decodedBytes != 2400 * sizeof (float))
        return fail ("SoundFont embedded PCM, keys, velocity and loop");
    if (ex.compatibility != Compatibility::partial || ex.instrument.zones.front().mapping.loopCrossfade != 10
        || ex.instrument.zones.front().mapping.end != 1990 || select (big).compatibility != Compatibility::partial
        || ds.compatibility != Compatibility::partial || ds.instrument.zones.front().mapping.soft != 35)
        return fail ("EXS byte orders and Decent Sampler reconstruction");

    writeIndex (inventory, index);
    auto rescanned = scan (library, progress, index);
    if (rescanned.reused != 5 || rescanned.definitions.size() != inventory.definitions.size())
        return fail ("persistent incremental index");
    if (! audio.getChildFile ("Piano_C4_v35-95.flac").setLastModificationTime (Time::getCurrentTime() - RelativeTime::hours (1)))
        return fail ("change dependency fingerprint");
    rescanned = scan (library, progress, index);
    if (rescanned.reused != 2) return fail ("invalidate changed dependencies only");

    File output, duplicate, convertedExs, convertedDecent;
    if (convert (inventory, sf, catalog, false, progress, output).failed()
        || convert (inventory, sf, catalog, false, progress, duplicate).failed() || output != duplicate
        || convert (inventory, ex, catalog, false, progress, convertedExs).wasOk()
        || convert (inventory, ex, catalog, true, progress, convertedExs).failed()
        || convert (inventory, ds, catalog, true, progress, convertedDecent).failed())
        return fail ("PCM-reference conversion, duplicate detection and partial approval");

    const auto native = scan (output, progress, {}, library);
    if (native.definitions.size() != 1 || native.definitions.front().compatibility != Compatibility::supported)
        return fail ("native preset identification");
    sampler::Instrument restored;
    if (sampler::Plugin::preparePreset (output, {}, restored).failed() || restored.zones.size() != 1
        || restored.zones.front().file.getFileName() != bank.getFileName()
        || ! restored.zones.front().soundFont || ! restored.zones.front().mapping.sample)
        return fail ("native SoundFont references survive round trip");
    sampler::Bank voices;
    voices.settings = restored.settings;
    voices.zones.push_back (restored.zones.front().mapping);
    sampler::Engine engine;
    engine.prepare (48000);
    engine.use (voices);
    std::array<float, 512> left {}, right {};
    engine.midi (0x90, 61, 90);
    engine.render (left.data(), right.data(), static_cast<int> (left.size()));
    if (*std::max_element (left.begin(), left.end()) < 0.01f)
        return fail ("sample-backed native MIDI playback");

    {
        tracktion::Engine tracktionEngine ("Studio Universal Import Test");
        sampler::registerPlugin (tracktionEngine);
        const auto projectFile = home.getChildFile ("SoundFont project.tracktionedit");
        auto edit = tracktion::createEmptyEdit (tracktionEngine, projectFile);
        if (edit == nullptr) return fail ("create SoundFont project");
        {
            auto plugin = edit->getPluginCache().createNewPlugin (sampler::Plugin::xmlTypeName, {});
            auto* instrument = dynamic_cast<sampler::Plugin*> (plugin.get());
            auto* track = tracktion::getAudioTracks (*edit).getFirst();
            if (instrument == nullptr || track == nullptr) return fail ("create native sampler track");
            instrument->commitImport (restored, false);
            track->pluginList.insertPlugin (plugin, 0, nullptr);
            if (! tracktion::EditFileOperations (*edit).writeToFile (projectFile, false))
                return fail ("save project with SoundFont sample slices");
        }
        edit.reset();
        edit = tracktion::loadEditFromFile (tracktionEngine, projectFile);
        if (edit == nullptr) return fail ("reload SoundFont project");
        auto* track = tracktion::getAudioTracks (*edit).getFirst();
        auto* instrument = track == nullptr ? nullptr : dynamic_cast<sampler::Plugin*> (track->pluginList[0]);
        if (instrument == nullptr || instrument->instrument().zones.size() != 1
            || ! instrument->instrument().zones[0].soundFont || ! instrument->instrument().zones[0].mapping.sample)
            return fail ("project reload restores playable SoundFont zones");
    }

    const auto recording = scan (audio.getChildFile ("Piano_C4_v35-95.flac"), progress);
    if (recording.definitions.size() != 1 || recording.definitions[0].instrument.zones.size() != 1)
        return fail ("import one recording as one instrument");
    const auto proposed = proposeSampleFolder (audio, progress);
    if (proposed.definitions.size() != 1 || proposed.definitions.front().compatibility != Compatibility::partial
        || proposed.definitions.front().instrument.zones.size() != 2
        || proposed.definitions.front().instrument.zones.front().mapping.soft != 35
        || ! proposed.definitions.front().issues.joinIntoString (" ").contains ("Provisional root"))
        return fail ("deterministic sample-folder proposal");

    const auto tagged = home.getChildFile ("Tagged");
    if (tagged.getChildFile ("Staccato").createDirectory().failed()
        || ! writeAudio<WavAudioFormat> (tagged.getChildFile ("Staccato/Horn_F4_close.wav")))
        return fail ("create tagged sample folder");
    const auto tags = proposeSampleFolder (tagged, progress);
    if (tags.definitions.size() != 1 || tags.definitions[0].instrument.groups.size() != 2
        || tags.definitions[0].instrument.groups[1].name != "staccato / close"
        || tags.definitions[0].instrument.zones.front().folder != 1)
        return fail ("articulation and microphone tags organize an editable proposal");

    const auto wide = home.getChildFile ("Wide");
    if (wide.createDirectory().failed()) return fail ("create wide fixture folder");
    for (int i = 0; i < 1200; ++i)
        if (! wide.getChildFile (String (i) + ".wav").replaceWithText ("unreadable sample candidate"))
            return fail ("populate wide fixture");
    Progress wideProgress;
    const auto discovered = scan (wide, wideProgress);
    if (! discovered.complete || wideProgress.completed.load() != 1200 || discovered.samples.size() != 1200
        || ! discovered.definitions.empty())
        return fail ("wide discovery inventories metadata without decoding sample candidates");

    const auto relocated = home.getChildFile ("Relocated");
    if (! library.moveFileTo (relocated)) return fail ("move test library");
    Progress stopped;
    stopped.cancelled.store (true);
    if (scan (relocated, stopped, index).complete || convert (inventory, sf, catalog, false, stopped, duplicate).wasOk())
        return fail ("cancellation leaves catalog untouched");
    File relinked;
    if (relinkPreset (output, {}, relocated, catalog, progress, relinked).failed()
        || relinked == output || ! output.existsAsFile())
        return fail ("relink relocated library to a new version of the preset");
    restored = {};
    if (sampler::Plugin::preparePreset (relinked, {}, restored).failed() || ! restored.zones.front().mapping.sample
        || ! restored.zones.front().file.getParentDirectory().getFileName().equalsIgnoreCase ("Relocated"))
        return fail ("play relinked SoundFont after relocation");

    std::puts ("Universal format discovery, reconstruction, cached rescans, and native playback passed.");
    return 0;
}
}
