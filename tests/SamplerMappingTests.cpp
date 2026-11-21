// SPDX-License-Identifier: GPL-3.0-or-later
#include "../SamplerMapping.h"
#include <cstdlib>
#include <iostream>
using namespace studio::sampler;
void check (bool ok) { if (! ok) { std::cerr << "Sampler mapping check failed\n"; std::exit (1); } }
int main()
{
    check (filenameNote ("Piano_C4.wav") == 60);
    check (filenameNote ("Violin-F#3.aiff") == 54);
    check (filenameNote ("Soft_Db4_take2.wav") == 61);
    check (filenameNote ("tone_C-1.wav") == 0);
    check (! filenameNote ("PianoC4.wav"));
    check (! filenameNote ("take_C40.wav"));
    check (! filenameNote ("C4_D4.wav"));
    check (! filenameNote ("recording_01.wav"));
    std::vector<Zone> z (3); z[0].root = 60; z[1].root = 67; z[2].root = 60;
    spreadMapping (z);
    check (z[0].low == 0 && z[0].high == 63 && z[1].low == 64 && z[1].high == 127);
    check (z[0].low == z[2].low && z[0].high == z[2].high);
    check (validMapping (z[0]));
    z[0].start = 100; z[0].end = 200; z[0].loopStart = 99; z[0].loopEnd = 150;
    check (! validLoop (z[0], 300)); z[0].loopStart = 100; check (validLoop (z[0], 300));
    z[0].loopEnd = 201; check (! validLoop (z[0], 300));
    std::cout << "Filename note detection, deterministic boundaries and trimmed loop validation passed.\n";
}
