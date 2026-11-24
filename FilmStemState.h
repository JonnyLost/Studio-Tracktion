// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "FilmStemDelivery.h"
#include <juce_data_structures/juce_data_structures.h>

namespace studio::film
{
// Stored separately from STUDIO_FILM_SCORING so picture markers and cue
// undo/redo are not overwritten by edits to instrument stem assignments.
inline std::vector<StemAssignment> readStemAssignments (const juce::ValueTree& editState)
{
    std::vector<StemAssignment> assignments;
    const auto root = editState.getChildWithName ("STUDIO_FILM_STEMS");
    if (! root.isValid() || (int) root.getProperty ("schema", 0) != 1)
        return assignments;
    for (int i = 0; i < root.getNumChildren() && i < 256; ++i)
    {
        const auto child = root.getChild (i);
        if (! child.hasType ("STEM_TRACK")) continue;
        const auto id = child.getProperty ("id").toString().toStdString();
        const int familyCode = (int) child.getProperty ("family", 0);
        if (validStemTrackId (id) && validStemFamily (familyCode))
            setStemFamily (assignments, id, static_cast<StemFamily> (familyCode));
    }
    return assignments;
}

inline void saveStemAssignments (juce::ValueTree editState,
                                 const std::vector<StemAssignment>& assignments,
                                 juce::UndoManager* undo)
{
    juce::ValueTree node ("STUDIO_FILM_STEMS");
    node.setProperty ("schema", 1, nullptr);
    for (const auto& entry : assignments)
    {
        if (! validStemTrackId (entry.trackId)
            || ! validStemFamily (static_cast<int> (entry.family))
            || entry.family == StemFamily::unassigned)
            continue;
        juce::ValueTree item ("STEM_TRACK");
        item.setProperty ("id", juce::String (entry.trackId), nullptr);
        item.setProperty ("family", (int) entry.family, nullptr);
        node.addChild (item, -1, nullptr);
    }
    const auto old = editState.getChildWithName ("STUDIO_FILM_STEMS");
    if (old.isValid()) editState.removeChild (old, undo);
    editState.addChild (node, -1, undo);
}
} // namespace studio::film
