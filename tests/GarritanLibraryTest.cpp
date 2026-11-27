// SPDX-License-Identifier: GPL-3.0-or-later
#include "../InstrumentLibrary.h"
#include "../InstrumentBrowser.h"
#include <cstdio>
#include <filesystem>

namespace studio::instrumentimport
{
using namespace juce;

int runLibraryTest()
{
    const auto fail = [] (const char* reason)
    {
        std::fprintf (stderr, "Garritan library test: %s\n", reason);
        return 1;
    };

    TemporaryFile work ("-library-test");
    const auto sandbox = work.getFile();
    const ScopeGuard cleanup { [&] { sandbox.deleteRecursively(); } };
    const auto library = sandbox.getChildFile ("Synthetic Garritan");
    const auto samples = library.getChildFile ("Samples");
    const auto patches = library.getChildFile ("Programs");
    const auto catalog = sandbox.getChildFile ("Imported Instruments");
    if (samples.createDirectory().failed() || patches.createDirectory().failed())
        return fail ("fixture directories");
    const auto sample = samples.getChildFile ("Violin tone.wav");
    {
        std::unique_ptr<OutputStream> stream = sample.createOutputStream();
        WavAudioFormat format;
        auto writer = format.createWriterFor (stream, AudioFormatWriterOptions().withSampleRate (48000)
            .withNumChannels (1).withBitsPerSample (16));
        AudioBuffer<float> audio (1, 4800);
        for (int i = 0; i != audio.getNumSamples(); ++i)
            audio.setSample (0, i, 0.25f);
        if (! writer || ! writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples()))
            return fail ("write synthetic PCM");
    }
    MemoryBlock originalAudio;
    if (! sample.loadFileAsData (originalAudio)) return fail ("read synthetic PCM before discovery");
    const auto originalAudioTime = sample.getLastModificationTime();
    const auto source = patches.getChildFile ("Synthetic Violin.sfz");
    const auto text = R"SFZ(
// Synthetic SFZ fixture
<control> default_path=../Samples/
<global> ampeg_attack=0.01 ampeg_decay=0.2 ampeg_sustain=80 ampeg_release=0.3
<group> sw_last=C2 sw_default=C2 sw_label=Sustain
<region> sample="Violin tone.wav" key=C4 lovel=1 hivel=99 loop_mode=loop_continuous loop_start=12 loop_end=300
<region> sample=Violin tone.wav key=C4 lovel=100 hivel=127 seq_length=2 seq_position=2 tune=20
<region> sample=Violin tone.wav key=C4 lovel=100 hivel=127 seq_length=2 seq_position=1 tune=10
<group> sw_last=D2 sw_default=C2 sw_label=Release trigger=release
<region> sample=Violin tone.wav key=D4 loop_mode=one_shot
<group> locc21=64 hicc21=127
<region> sample=Violin tone.wav key=E4
)SFZ";
    if (! source.replaceWithText (text)
        || ! patches.getChildFile ("Partial.sfz").replaceWithText ("<region> sample=../Samples/Violin tone.wav cutoff=4000")
        || ! patches.getChildFile ("Broken.sfz").replaceWithText ("<region> sample=missing.wav")
        || ! patches.getChildFile ("Included.sfz").replaceWithText ("#include \"other.sfz\"")
        || ! patches.getChildFile ("Opaque.aria").replaceWithText ("opaque fixture")
        || ! samples.getChildFile ("Locked.audio").replaceWithText ("opaque fixture")
        || ! samples.getChildFile ("Fake.wav").replaceWithText ("invalid PCM")
        || ! patches.getChildFile ("BadAudio.sfz").replaceWithText ("<region> sample=../Samples/Fake.wav"))
        return fail ("write synthetic definition files");
    const auto outside = sandbox.getChildFile ("outside.wav");
    if (! sample.copyFileTo (outside)
        || ! patches.getChildFile ("Escape.sfz").replaceWithText ("<region> sample=../../outside.wav"))
        return fail ("out-of-library fixture");
    std::error_code ec;
    std::filesystem::create_symlink (std::filesystem::u8path (outside.getFullPathName().toRawUTF8()),
        std::filesystem::u8path (samples.getChildFile ("Link.wav").getFullPathName().toRawUTF8()), ec);
    if (ec || ! patches.getChildFile ("Symlink.sfz").replaceWithText ("<region> sample=../Samples/Link.wav"))
        return fail ("symlink fixture");

    MemoryBlock originalDefinition;
    if (! source.loadFileAsData (originalDefinition)) return fail ("read SFZ before discovery");
    const auto originalDefinitionTime = source.getLastModificationTime();

    Progress discovery;
    const auto inventory = scan (library, discovery);
    if (inventory.samples.size() != 3 || inventory.definitions.size() != 8 || discovery.completed.load() < 10)
        return fail ("recursive inventory excludes symlinks");
    const auto find = [&] (const char* name) -> const Definition&
    {
        return *std::find_if (inventory.definitions.begin(), inventory.definitions.end(), [name] (const auto& entry)
            { return entry.instrument.name == name; });
    };
    const auto& playable = find ("Synthetic Violin");
    const auto& partial = find ("Partial");
    for (const auto& item : inventory.definitions)
        if (&item != &playable && &item != &partial
            && (item.compatibility != (item.source.hasFileExtension ("aria") ? Compatibility::originalPlayer
                                                                              : Compatibility::unsupported)
                || item.issues.isEmpty()))
            return fail ("missing, protected, out-of-root and malformed inputs were not rejected");
    if (playable.compatibility != Compatibility::supported || partial.compatibility != Compatibility::partial
        || playable.regions.size() != 5 || playable.decodedBytes != 4800 * sizeof (float))
        return fail ("compatibility and unique sample memory estimate");

    File report, preset, unused;
    if (saveInventory (inventory, library.getChildFile ("disallowed"), unused).wasOk()
        || saveInventory (inventory, sandbox.getChildFile ("Reports"), report).failed()
        || JSON::parse (report.loadFileAsString()).getProperty ("schemaVersion", 0) != var (2))
        return fail ("JSON manifest and source protection");
    if (convert (inventory, partial, catalog, false, discovery, unused).wasOk()
        || convert (inventory, playable, library.getChildFile ("disallowed"), false, discovery, unused).wasOk()
        || library.getChildFile ("disallowed").exists())
        return fail ("partial opt-in and source write protection");
    Progress stopped;
    stopped.cancelled.store (true);
    if (! scan (library, stopped).definitions.empty()
        || convert (inventory, playable, catalog, false, stopped, unused).wasOk())
        return fail ("cooperative cancellation");

    if (const auto result = convert (inventory, playable, catalog, false, discovery, preset); result.failed())
    {
        std::fprintf (stderr, "%s\n", result.getErrorMessage().toRawUTF8());
        return fail ("convert to native sampler");
    }
    {
        PropertiesFile::Options options;
        options.applicationName = "Studio Garritan Test";
        PropertiesFile preferences (sandbox.getChildFile ("Settings.xml"), options);
        KnownPluginList known;
        InstrumentBrowser browser (known, &preferences);
        browser.setLoadContext (true, "Test Track", {});
        File selected;
        browser.loadImportedSampler = [&] (const File& file, bool addNew) { if (addNew) selected = file; };
        for (auto* component : browser.getChildren())
            if (auto* results = dynamic_cast<ListBox*> (component); results != nullptr && results->getName() == "Instrument results")
                for (int row = 0; row < results->getModel()->getNumRows(); ++row)
                    if (results->getModel()->getNameForRow (row).contains ("Synthetic Violin")) results->selectRow (row);
        for (auto* component : browser.getChildren())
            if (auto* add = dynamic_cast<TextButton*> (component); add != nullptr
                && add->getButtonText() == "Add as New Track" && add->isEnabled()) add->onClick();
        if (selected != preset) return fail ("native preset is registered and selectable in the Instrument Browser");
    }

    sampler::Instrument instrument;
    if (sampler::Plugin::preparePreset (preset, {}, instrument).failed() || instrument.zones.size() != 5
        || ! instrument.zones[0].mapping.sample || instrument.groups.size() != 3
        || instrument.zones[0].mapping.root != 60
        || instrument.zones[0].mapping.loopStart != 12 || instrument.zones[0].mapping.loopEnd != 301
        || instrument.zones[0].mapping.hard != 99 || instrument.settings.sustain != 0.8f
        || instrument.zones[3].mapping.cents != 10 || instrument.zones[4].mapping.cents != 20)
        return fail ("native mapping round trip");
    sampler::Bank bank;
    bank.settings = instrument.settings;
    for (const auto& group : instrument.groups) bank.groups.push_back (group.playback);
    for (const auto& zone : instrument.zones)
    {
        if (! std::filesystem::equivalent (std::filesystem::u8path (zone.file.getFullPathName().toRawUTF8()),
                std::filesystem::u8path (sample.getFullPathName().toRawUTF8()), ec)
            || zone.mapping.sample != instrument.zones[0].mapping.sample)
            return fail ("sample reference and in-memory sharing");
        auto mapping = zone.mapping;
        mapping.folder = zone.folder;
        bank.zones.push_back (std::move (mapping));
    }
    sampler::Engine engine;
    engine.prepare (48000);
    engine.use (bank);
    std::array<float, 512> left {}, right {};
    const auto amplitude = [&]
    {
        left.fill (0);
        right.fill (0);
        engine.render (left.data(), right.data(), static_cast<int> (left.size()));
        return *std::max_element (left.begin(), left.end());
    };
    engine.midi (0x90, 60, 90);
    if (amplitude() < 0.01f) return fail ("default articulation playback");
    engine.midi (0xb0, 11, 0);
    if (amplitude() > 0.0001f) return fail ("MIDI expression");
    engine.panic();
    engine.midi (0xb0, 11, 127);
    engine.midi (0x90, 64, 90);
    if (amplitude() > 0.0001f) return fail ("controller gate is closed");
    engine.midi (0xb0, 21, 100);
    engine.midi (0x90, 64, 90);
    if (amplitude() < 0.01f) return fail ("controller gate is open");
    engine.panic();
    engine.midi (0x90, 38, 100);
    engine.midi (0x90, 62, 90);
    if (amplitude() > 0.0001f) return fail ("release zone remains silent on note-on");
    engine.midi (0x80, 62, 0);
    if (amplitude() < 0.01f) return fail ("keyswitch and release zone playback");

    MemoryBlock currentAudio, currentDefinition;
    if (! sample.loadFileAsData (currentAudio) || ! source.loadFileAsData (currentDefinition)
        || currentAudio != originalAudio || currentDefinition != originalDefinition
        || sample.getLastModificationTime() != originalAudioTime || source.getLastModificationTime() != originalDefinitionTime)
        return fail ("source files changed during discovery or import");
    if (! sample.deleteFile()
        || convert (inventory, playable, catalog, false, discovery, unused).wasOk()
        || catalog.findChildFiles (File::findFiles, false, "*.studiosampler").size() != 1)
        return fail ("missing source fails without publishing a preset");

    std::puts ("Garritan read-only inventory, sampler conversion, MIDI and memory sharing passed.");
    return 0;
}
}
