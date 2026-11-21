// SPDX-License-Identifier: GPL-3.0-or-later
#include "SeatingWorkspace.h"
#include "AppearanceMetrics.h"

#if JUCE_MAC
extern "C"
{
    void* studioSeatingCreate (void*, void (*)(void*, const char*, const char*));
    void* studioSeatingNativeView (void*);
    void studioSeatingRelease (void*);
    void studioSeatingRestore (void*, const char*, int);
    void studioSeatingAppearance (void*, double, double);
   #if STUDIO_SPATIAL_TESTS
    int studioSeatingExercise (void*, int, const char*);
   #endif
}

namespace studio::seating
{
Workspace::Workspace() { addAndMakeVisible (native); }

Workspace::~Workspace()
{
    project.removeListener (this);
    native.setView (nullptr);
    if (handle != nullptr) studioSeatingRelease (handle);
}

void Workspace::setEdit (tracktion::Edit* next)
{
    project.removeListener (this);
    edit = next;
    project = edit != nullptr ? edit->state : juce::ValueTree();
    project.addListener (this);
    refresh();
}

void Workspace::ensureView()
{
    if (handle != nullptr) return;
    handle = studioSeatingCreate (this, [] (void* context, const char* data, const char* action)
    {
        static_cast<Workspace*> (context)->store (juce::String::fromUTF8 (data), juce::String::fromUTF8 (action));
    });
    native.setView (studioSeatingNativeView (handle));
    refresh();
    appearanceChanged();
}

void Workspace::visibilityChanged()
{
    if (isVisible()) ensureView();
}

void Workspace::resized() { native.setBounds (getLocalBounds()); }

void Workspace::appearanceChanged()
{
    if (handle != nullptr)
        studioSeatingAppearance (handle, appearance.interfacePercent() / 100.0, appearance.textPercent() / 100.0);
}

void Workspace::refresh()
{
    loaded = read (project);
    if (handle != nullptr) studioSeatingRestore (handle, loaded.toRawUTF8(), edit != nullptr ? 1 : 0);
}

void Workspace::valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& key)
{
    if (tree == project && key == layoutProperty && read (project) != loaded) refresh();
}

void Workspace::store (const juce::String& data, const juce::String& action)
{
    if (edit == nullptr) return;
    loaded = data;
    if (! write (*edit, data, action)) refresh();
}

#if STUDIO_SPATIAL_TESTS
bool Workspace::exercise (int step, const juce::File& screenshot)
{
    ensureView();
    const auto path = screenshot.getFullPathName();
    return studioSeatingExercise (handle, step, screenshot == juce::File() ? nullptr : path.toRawUTF8()) != 0;
}
#endif
}
#endif
