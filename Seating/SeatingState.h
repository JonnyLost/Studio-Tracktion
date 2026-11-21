// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <JuceHeader.h>

namespace studio::seating
{
inline const juce::Identifier layoutProperty { "studioPhysicalSeating" };
inline constexpr size_t maximumLayoutBytes = 256 * 1024;

inline juce::String read (const juce::ValueTree& project)
{
    return project.getProperty (layoutProperty).toString();
}

inline bool write (tracktion::Edit& edit, const juce::String& data, const juce::String& action)
{
    auto& project = edit.state;
    if (! project.isValid() || data.isEmpty() || data.getNumBytesAsUTF8() > maximumLayoutBytes || read (project) == data)
        return false;
    const auto layout = juce::JSON::parse (data);
    if (! layout.isObject() || layout.getProperty ("version", 0) != juce::var (1)
        || ! layout.getProperty ("seats", {}).isArray())
        return false;

    auto& undo = edit.getUndoManager();
    undo.beginNewTransaction (action);
    project.setProperty (layoutProperty, data, &undo);
    undo.beginNewTransaction();
    edit.markAsChanged();
    return true;
}
}
