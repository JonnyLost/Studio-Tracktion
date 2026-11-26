// SPDX-License-Identifier: GPL-3.0-or-later
#include "../StudioSfz.h"
#include <cassert>
#include <cstdio>

using namespace studio::sfz;
int main()
{
    const Path main = "/library/main.sfz";
    std::map<Path, std::string> fixtures {
        { main, R"sfz(
#define $base Samples
<control> default_path=$base/Orchestra
<global> ampeg_attack=0.025 volume=-3
<master> hivel=100
<group> key=C4 lovel=20
#include "parts/notes.inc"
<group> lokey=F#4 hikey=Gb4
<region> sample=Violin Long G4.wav
<global> key=36
<region> sample=kick.wav
<curve> curve_index=9 v0=0 v127=1
)sfz" },
        { Path ("/library/parts/notes.inc"), R"sfz(
/* comment <region> sample=bad */
<region> sample=First Chair.wav pitch_keycenter=61 // comment
<region> sample="Second Chair.wav" key=D4 hivel=127
)sfz" }
    };
    const Loader load = [&] (const Path& p) -> std::optional<std::string>
    {
        const auto it = fixtures.find (p);
        return it == fixtures.end() ? std::nullopt : std::optional<std::string> (it->second);
    };
    Parser parser;
    auto d = parser.parse (main, load);
    assert (d.error.empty() && d.regions.size() == 4);
    const auto& first = d.regions[0].ops;
    assert (get (first, "sample") == "First Chair.wav");
    assert (get (first, "default_path") == "Samples/Orchestra");
    assert (get (first, "lokey") == "C4" && get (first, "pitch_keycenter") == "61");
    assert (get (first, "lovel") == "20" && get (first, "hivel") == "100");
    assert (get (first, "ampeg_attack") == "0.025");
    assert (get (d.regions[1].ops, "sample") == "Second Chair.wav");
    assert (get (d.regions[1].ops, "hikey") == "D4" && get (d.regions[1].ops, "hivel") == "127");
    assert (get (d.regions[2].ops, "lovel").empty()); // new group clears inherited previous group
    assert (get (d.regions[2].ops, "hivel") == "100"); // master remains
    assert (get (d.regions[3].ops, "hivel").empty()); // new global clears lower scopes
    assert (d.warnings.count ("Unsupported header <curve>"));
    assert (note ("C4") == 60 && note ("c#4") == 61 && note ("Db4") == 61 && note ("C-1") == 0);
    assert (note ("G9") == 127 && ! note ("G#9") && ! note ("60garbage"));
    assert (integer ("+12") == 12 && ! integer ("10.2") && ! integer ("9999999999999"));
    assert (number ("-.5") == -0.5 && ! number ("nan") && ! number ("1.2.3"));
    assert (path ("foo\\bar.wav").generic_string() == "foo/bar.wav");
    assert (! parser.parse (main, load, 2).error.empty());
    fixtures[main] = "#include \"main.sfz\"\n";
    assert (! parser.parse (main, load).error.empty());
    fixtures[main] = "<region> sample=$missing/test.wav";
    assert (! parser.parse (main, load).error.empty());
    fixtures[main] = "#define $a $a\n<region> sample=$a";
    assert (! parser.parse (main, load).error.empty());
    fixtures[main] = "<region> sample=\"unclosed.wav";
    assert (! parser.parse (main, load).error.empty());
    fixtures[main] = "<region> sample=one.wav /* unclosed";
    assert (! parser.parse (main, load).error.empty());
    fixtures[main] = "<group> key=60";
    assert (! parser.parse (main, load).error.empty());
    fixtures[main] = "\xef\xbb\xbf<region> sample=hello world.wav offset=123 loopmode=one_shot";
    d = parser.parse (main, load);
    assert (d.error.empty() && d.regions.size() == 1 && get (d.regions[0].ops, "sample") == "hello world.wav");
    assert (get (d.regions[0].ops, "loop_mode") == "one_shot");
    std::puts ("SFZ parser: inheritance, includes, macros, spaces, MIDI notes, aliases and malformed input passed.");
}
