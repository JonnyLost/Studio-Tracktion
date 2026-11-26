// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The Apple AudioToolbox API is deliberately isolated from Studio.cpp.
// Including it in a JUCE translation unit triggers MacTypes Point and
// CoreAudio AudioBuffer collisions on current macOS SDKs.
namespace studio::dls
{
    // Opaque handle returned by JUCE's AudioUnitClient::getAudioUnitHandle().
    // Returns false for invalid programs, missing AU handles or non-macOS builds.
    bool selectGeneralMidiProgram (void* nativeAudioUnit, int program) noexcept;
}
