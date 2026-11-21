// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "SeatingState.h"

#if JUCE_MAC
namespace studio::seating
{
class Workspace final : public juce::Component, private juce::ValueTree::Listener
{
public:
    Workspace();
    ~Workspace() override;
    void setEdit (tracktion::Edit*);
    void appearanceChanged();
    void visibilityChanged() override;
    void resized() override;
   #if STUDIO_SPATIAL_TESTS
    bool exercise (int step, const juce::File& screenshot);
   #endif

private:
    void ensureView();
    void refresh();
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void store (const juce::String&, const juce::String&);
    juce::NSViewComponent native;
    void* handle = nullptr;
    tracktion::Edit* edit = nullptr;
    juce::ValueTree project;
    juce::String loaded;
};
}
#endif
