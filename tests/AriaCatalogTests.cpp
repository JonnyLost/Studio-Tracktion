// SPDX-License-Identifier: GPL-3.0-or-later
#include "../AriaCatalog.h"
#include <iostream>
#include <stdexcept>

using namespace juce;
using namespace studio::aria;

static void require (bool success, const char* message)
{
    if (! success) throw std::runtime_error (message);
}

int main()
{
    const auto temp = File::getSpecialLocation (File::tempDirectory).getNonexistentChildFile ("studio-aria-fixture", "");
    const auto library = temp.getChildFile ("GPO5");
    const auto strings = library.getChildFile ("Strings");
    const auto extras = library.getChildFile ("Extras");
    const auto local = temp.getChildFile ("Registry");
    try
    {
        require (strings.createDirectory().wasOk() && extras.createDirectory().wasOk(), "create fixture");
        const auto violin = strings.getChildFile ("Violin.sfz");
        const auto duplicateName = extras.getChildFile ("Violin.sfz");
        const auto include = strings.getChildFile ("violin.inc");
        const auto sample = strings.getChildFile ("Samples/violin A3.wav");
        require (sample.getParentDirectory().createDirectory().wasOk(), "create sample folder");
        require (sample.replaceWithText ("synthetic sample placeholder"), "write sample");
        const String definition = "<control> default_path=Samples/\n#include \"violin.inc\"\n";
        require (violin.replaceWithText (definition), "write definition");
        const auto originalBytes = violin.loadFileAsString();
        require (include.replaceWithText ("<region> sample=violin A3.wav lokey=55 hikey=88 sw_label=Legato\n"
                                          "// sample=ignored.wav\n"), "write include");
        require (duplicateName.replaceWithText ("<region> sample=absent.wav key=60"), "write missing sample");
        require (extras.getChildFile ("Strings.ariax").replaceWithText ("snapshot"), "write snapshot");
        require (extras.getChildFile ("Protected.nki").replaceWithText ("container"), "write other format");

        Catalog catalog (local);
        bool cancelled = false;
        catalog.scan ({ library }, [&] (const Progress& p) { return cancelled |= p.examined >= 2; });
        require (cancelled && catalog.pendingRoots().contains (library), "interrupt scan");
        require (catalog.save().wasOk(), "write checkpoint");
        Catalog resumed (local);
        resumed.read();
        require (resumed.pendingRoots().contains (library), "read checkpoint");
        resumed.scan (resumed.pendingRoots(), [] (const Progress&) { return false; });
        require (resumed.pendingRoots().isEmpty(), "finish resume");
        require (resumed.patches().size() == 4, "preserve duplicate display names and classify distinct formats");
        const auto& patch = resumed.patches().at (violin.getFullPathName());
        require (patch.library == "Personal Orchestra 5" && patch.family == "Strings", "library and family");
        require (patch.health == "Literal sample references present", "include and path with spaces");
        require (patch.midiRange == "55-88" && patch.articulations.contains ("Legato"), "explicit SFZ metadata");
        require (resumed.status (patch).contains ("choose patch"), "SFZ recognition does not prove playback");
        const auto& missing = resumed.patches().at (duplicateName.getFullPathName());
        require (resumed.status (missing).contains ("check samples"), "missing literal dependency");
        const auto& snapshot = resumed.patches().at (extras.getChildFile ("Strings.ariax").getFullPathName());
        const auto& other = resumed.patches().at (extras.getChildFile ("Protected.nki").getFullPathName());
        require (resumed.status (snapshot).contains ("ensemble"), "snapshot is not a single patch");
        require (resumed.status (other).startsWith ("Original player"), "other player filtered from ARIA");
        require (resumed.registerPreset (patch, { "fake-test-au", "1", "malformed", 0 }).failed(), "reject invalid state");
        require (resumed.registerPreset (patch, { "fake-test-au", "1", "-1.A", 0 }).failed(), "reject invalid state size");
        const Preset capture { "fake-test-au", "1", MemoryBlock ("state", 5).toBase64Encoding(), 2 };
        require (resumed.registerPreset (other, capture).failed(), "refuse other-player files as ARIA");
        const auto singleSlot = extras.getChildFile ("Confirmed single.aria");
        require (singleSlot.replaceWithText ("synthetic single-slot snapshot"), "write single-slot fixture");
        require (resumed.preset (snapshot).state.isEmpty(), "snapshot needs an explicit capture");
        Catalog snapshotCatalog (temp.getChildFile ("Snapshot registry"));
        snapshotCatalog.scan ({ extras }, [] (const Progress&) { return false; });
        const auto& single = snapshotCatalog.patches().at (singleSlot.getFullPathName());
        require (snapshotCatalog.registerPreset (single, capture).wasOk()
                 && snapshotCatalog.preset (single).state == capture.state, "accept an explicitly confirmed single-slot snapshot");
        require (resumed.registerPreset (patch, capture).wasOk(), "register fixture state");
        require (resumed.registerPreset (patch, capture).failed(), "refuse silent overwrite");
        require (resumed.report().contains ("user-auditioned capture): 1"), "report registered state");
        require (resumed.save().wasOk(), "write registered catalog");
        Catalog reopened (local);
        reopened.read();
        const auto& restored = reopened.patches().at (patch.id);
        const auto preset = reopened.preset (restored);
        require (preset.pluginId == capture.pluginId && preset.pluginVersion == capture.pluginVersion
                 && preset.state == capture.state && preset.program == capture.program, "reopen fixture state");
        const auto json = local.getChildFile ("catalog.json").loadFileAsString();
        require (! json.contains (capture.state) && json.contains ("Audition-confirmed"), "keep state outside inventory");
        require (violin.loadFileAsString() == originalBytes, "source preserved");
        require (violin.appendText ("// changed"), "change source stamp");
        require (reopened.preset (restored).state.isEmpty(), "reject stale capture");
        require (violin.deleteFile(), "remove fixture definition");
        const auto known = reopened.scannedRoots();
        reopened.scan (known, [] (const Progress&) { return false; });
        require (! reopened.patches().at (patch.id).present, "mark vanished source unavailable");

        temp.deleteRecursively();
        std::cout << "ARIA catalog fixtures passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        temp.deleteRecursively();
        std::cerr << error.what() << '\n';
        return 1;
    }
}
