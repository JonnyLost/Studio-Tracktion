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
    std::unique_ptr<juce::DocumentWindow> reviewWindow;
    // Some integration entry points construct the actual StudioView without an
    // application shell. Put that live view in a real native review window.
    if (window->getPeer() == nullptr)
    {
        const int viewWidth = window->getWidth(), viewHeight = window->getHeight();
        reviewWindow = std::make_unique<juce::DocumentWindow> ("Studio", juce::Colour (0xff1c2530), 0);
        reviewWindow->setUsingNativeTitleBar (true);
        reviewWindow->setContentNonOwned (window, true);
        reviewWindow->centreWithSize (viewWidth, viewHeight + 28);
        window = reviewWindow.get();
    }
    window->setVisible (true);
    window->toFront (true); window->repaint();
    juce::MessageManager::getInstance()->runDispatchLoopUntil (80);
    juce::ChildProcess capture;
    const bool ok = capture.start (juce::StringArray { "/usr/sbin/screencapture", "-x", destination.getFullPathName() })
        && capture.waitForProcessToFinish (5000) && capture.getExitCode() == 0 && destination.existsAsFile();
    if (reviewWindow) reviewWindow->clearContentComponent();
    std::printf ("Native WindowServer screenshot %s: %s\n", destination.getFileName().toRawUTF8(), ok ? "captured" : "unavailable");
#else
    juce::ignoreUnused (component, destination);
#endif
}
}
