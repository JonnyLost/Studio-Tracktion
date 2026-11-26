// SPDX-License-Identifier: GPL-3.0-or-later
#include "StudioDlsAudioUnit.h"

#if defined (__APPLE__)
 #include <AudioToolbox/AudioToolbox.h>
#endif

namespace studio::dls
{
bool selectGeneralMidiProgram (void* nativeAudioUnit, int program) noexcept
{
   #if defined (__APPLE__)
    if (nativeAudioUnit == nullptr || program < 0 || program > 127)
        return false;

    auto unit = static_cast<AudioUnit> (nativeAudioUnit);
    // General MIDI melodic bank zero, channel 1. Index is MIDI 0...127.
    // This is called by Studio's stopped-transport UI logic, never the
    // real-time audio processing callback.
    const auto msb = MusicDeviceMIDIEvent (unit, 0xb0, 0, 0, 0);
    const auto lsb = MusicDeviceMIDIEvent (unit, 0xb0, 32, 0, 0);
    const auto change = MusicDeviceMIDIEvent (unit, 0xc0,
                                             static_cast<UInt32> (program), 0, 0);
    return msb == noErr && lsb == noErr && change == noErr;
   #else
    (void) nativeAudioUnit;
    (void) program;
    return false;
   #endif
}
}
