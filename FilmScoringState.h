// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "FilmScoringCore.h"
#include <juce_data_structures/juce_data_structures.h>

namespace studio::film
{
// Store under the Tracktion Edit's root state so ordinary project saves,
// Save As, and ValueTree recovery snapshots carry identical picture metadata.
inline juce::ValueTree encode (const Project& project)
{
    juce::ValueTree node ("STUDIO_FILM_SCORING");
    node.setProperty ("schema", 1, nullptr);
    node.setProperty ("rate", (int) project.rate, nullptr);
    node.setProperty ("startFrame", juce::String (project.pictureStartFrame), nullptr);
    node.setProperty ("picturePath", juce::String (project.picturePath), nullptr);
    for (const auto& spot : project.spots)
    {
        juce::ValueTree item ("SPOT");
        item.setProperty ("id", juce::String (spot.id), nullptr);
        item.setProperty ("cue", juce::String (spot.cue), nullptr);
        item.setProperty ("name", juce::String (spot.name), nullptr);
        item.setProperty ("notes", juce::String (spot.notes), nullptr);
        item.setProperty ("frame", juce::String (spot.pictureFrame), nullptr);
        node.addChild (item, -1, nullptr);
    }
    return node;
}

inline Project decode (const juce::ValueTree& root)
{
    Project project;
    const auto node = root.hasType ("STUDIO_FILM_SCORING") ? root : root.getChildWithName ("STUDIO_FILM_SCORING");
    if (! node.isValid() || (int) node.getProperty ("schema", 0) != 1) return project;
    const auto rate = (int) node.getProperty ("rate", (int) Rate::fps24);
    if (rate >= (int) Rate::fps23976 && rate <= (int) Rate::fps30) project.rate = (Rate) rate;
    project.pictureStartFrame = node.getProperty ("startFrame", "0").toString().getLargeIntValue();
    project.picturePath = node.getProperty ("picturePath").toString().toStdString();
    for (const auto& child : node)
    {
        if (! child.hasType ("SPOT")) continue;
        Spot spot { child.getProperty ("id").toString().toStdString(),
                    child.getProperty ("cue").toString().toStdString(),
                    child.getProperty ("name").toString().toStdString(),
                    child.getProperty ("notes").toString().toStdString(),
                    child.getProperty ("frame", "0").toString().getLargeIntValue() };
        project.addSpot (std::move (spot));
    }
    return project;
}

inline void store (juce::ValueTree editRoot, const Project& project, juce::UndoManager* undo)
{
    if (undo != nullptr) undo->beginNewTransaction ("Edit Film Scoring");
    const auto existing = editRoot.getChildWithName ("STUDIO_FILM_SCORING");
    const auto index = existing.isValid() ? editRoot.indexOf (existing) : editRoot.getNumChildren();
    if (existing.isValid()) editRoot.removeChild (existing, undo);
    editRoot.addChild (encode (project), index, undo);
}
} // namespace studio::film
