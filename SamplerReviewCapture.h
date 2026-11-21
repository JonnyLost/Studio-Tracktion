// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <JuceHeader.h>
#include <cstdio>

namespace studio::sampler
{
// Supplement application renders with real WindowServer screen captures on Intel CI.
inline void captureNativeScreen (juce::Component& component, const juce::File& destination)
{
#if JUCE_MAC && STUDIO_SPATIAL_TESTS
    auto* window = component.getTopLevelComponent();
    window->toFront (true); window->repaint();
    juce::MessageManager::getInstance()->runDispatchLoopUntil (80);
    juce::ChildProcess capture;
    const bool ok = capture.start (juce::StringArray { "/usr/sbin/screencapture", "-x", destination.getFullPathName() })
        && capture.waitForProcessToFinish (5000) && capture.getExitCode() == 0 && destination.existsAsFile();
    std::printf ("Native WindowServer screenshot %s: %s\n", destination.getFileName().toRawUTF8(), ok ? "captured" : "unavailable");
#else
    juce::ignoreUnused (component, destination);
#endif
}
}
