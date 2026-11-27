// SPDX-License-Identifier: GPL-3.0-or-later
#include "../AriaCatalog.h"
#include "../InstrumentLibraryReconciliation.h"
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
        require (violin.setLastModificationTime (Time (1'700'000'000'321LL)), "set fixture timestamp");
        require (include.replaceWithText ("<region> sample=violin A3.wav lokey=55 hikey=88 sw_label=Legato sw_last=24\n"
                                          "// sample=ignored.wav sw_label=Ghost sw_last=99\n"
                                          "<group> sw_label=Pizzicato sw_last=25\n"
                                          "<group> sw_label=Conflict sw_last=26\n"
                                          "<group> sw_label=Conflict sw_last=27\n"
                                          "<group> sw_label=NameOnly sw_last=C3\n"
                                          "<group> sw_label=Range sw_lolast=28 sw_hilast=29\n"
                                          "<group> sw_label=DuplicateNote sw_last=25\n"), "write include");
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
        require (patch.modified == violin.getLastModificationTime().toMilliseconds(), "preserve file timestamp precision");
        require (patch.library == "Personal Orchestra 5" && patch.family == "Strings", "library and family");
        require (patch.health == "Literal sample references present", "include and path with spaces");
        require (patch.midiRange == "55-88" && patch.articulations.contains ("Legato"), "explicit SFZ metadata");
        require (resumed.status (patch).contains ("choose patch"), "SFZ recognition does not prove playback");
        require(patch.mappingInspected && patch.switchHints.size()==5,"explicit numeric pairs only; no comment/range/note-name hints");
        studio::designer::Catalog vocabulary;
        require(vocabulary.load(R"json({"schemaVersion":1,"instruments":[{"id":"violin","family":"strings","candidateTechniqueGroups":["strings"]}],"families":[{"id":"strings"}],"techniques":[{"id":"legato","name":"Legato","aliases":["Connected"]}],"controls":[{"id":"dynamics"}],"techniqueGroups":{"strings":["legato"]},"documentedPatchHints":[{"libraryId":"garritan.gpo5","patchName":"Violin","instrumentId":"violin","enabled":false,"bindings":[{"techniqueId":"legato","printedNote":"C2"}]}]})json").wasOk(),"load vocabulary fixture");
        require(vocabulary.exactTechnique("Connected")=="legato","unique exact alias");
        const auto suggestions=studio::designer::reconcilePatch(patch,vocabulary);
        const auto find=[&](const String& label)->const studio::designer::PatchSuggestion*{
            for(const auto& h:suggestions)if(h.label==label)return &h;return nullptr;
        };
        require(find("Legato")&&find("Legato")->note==24&&find("Legato")->semantic=="legato"&&find("Legato")->printedNote=="C2","include literal switch and independent printed note");
        require(find("Pizzicato")&&find("Pizzicato")->note==-1,"shared numeric key conflict");
        require(find("Conflict")&&find("Conflict")->note==-1,"same label multiple keys");
        require(find("NameOnly")&&find("NameOnly")->note==-1&&find("Range")&&find("Range")->note==-1&&!find("Ghost"),"labels only and comment ignored");
        studio::designer::Profile profile;profile.instrument="bowed-strings.violin";
        require(studio::designer::linkPatch(profile,patch).wasOk(),"link exact patch");
        require(studio::designer::addSuggestion(profile,*find("Legato"))==0&&profile.elements[0].binding.type=="unassigned","never enable discovered note");
        profile.elements[0].binding={"note",0,40,100};
        require(studio::designer::addSuggestion(profile,*find("Legato"))==0&&profile.elements.size()==1&&profile.elements[0].binding.number==40,"manual override preserved");
        studio::designer::Profile loaded;
        require(studio::designer::Profile::read(*profile.xml(),loaded).wasOk()&&loaded==profile,"profile identity round trip");
        require(studio::designer::linkPatch(profile,resumed.patches().at(duplicateName.getFullPathName())).failed(),"reject different exact patch");
        
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
        const auto registered = resumed.registerPreset (patch, capture);
        require (registered.wasOk(), registered.getErrorMessage().toRawUTF8());
        require (resumed.registerPreset (patch, capture).failed(), "refuse silent overwrite");
        require (resumed.report().contains ("user-auditioned capture): 1"), "report registered state");
        require (resumed.save().wasOk(), "write registered catalog");
        Catalog reopened (local);
        reopened.read();
        const auto& restored = reopened.patches().at (patch.id);
        require(restored.switchHints.size()==5&&restored.mappingInspected,"persist literal hints");
        require(include.appendText("\\n// changed include"),"change include");
        const auto staleHints=studio::designer::reconcilePatch(restored,vocabulary);
        for(const auto& h:staleHints)if(h.label=="Legato")require(h.note==-1,"stale include not proposed");
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
