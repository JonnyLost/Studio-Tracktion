// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <juce_core/juce_core.h>
#include <vector>

namespace studio
{
enum class ArticulationMessage { keyswitch, controller };

struct ArticulationMapping
{
    juce::String id, name;
    ArticulationMessage message = ArticulationMessage::keyswitch;
    int channel = 1, number = 0, value = 100, shortcut = 0;
    bool operator== (const ArticulationMapping&) const = default;
};

inline bool validArticulation (const ArticulationMapping& mapping)
{
    return mapping.id.isNotEmpty() && mapping.name.isNotEmpty()
        && mapping.channel >= 1 && mapping.channel <= 16
        && mapping.number >= 0 && mapping.number <= 127
        && mapping.value >= (mapping.message == ArticulationMessage::keyswitch ? 1 : 0) && mapping.value <= 127
        && mapping.shortcut >= 0 && mapping.shortcut <= 9;
}

inline std::vector<ArticulationMapping> readArticulations (const juce::XmlElement& profile)
{
    std::vector<ArticulationMapping> mappings;
    for (auto* entry : profile.getChildWithTagNameIterator ("Mapping"))
    {
        const auto kind = entry->getStringAttribute ("type");
        if (kind != "cc" && kind != "note") continue;
        ArticulationMapping mapping { entry->getStringAttribute ("id"), entry->getStringAttribute ("name"),
            kind == "cc" ? ArticulationMessage::controller : ArticulationMessage::keyswitch,
            entry->getIntAttribute ("channel", -1), entry->getIntAttribute ("number", -1),
            entry->getIntAttribute ("value", -1), entry->getIntAttribute ("shortcut") };
        if (validArticulation (mapping)) mappings.push_back (std::move (mapping));
    }
    return mappings;
}

inline void writeArticulations (juce::XmlElement& profile, const std::vector<ArticulationMapping>& mappings)
{
    profile.deleteAllChildElements();
    for (const auto& mapping : mappings)
    {
        auto* entry = profile.createNewChildElement ("Mapping");
        entry->setAttribute ("id", mapping.id);
        entry->setAttribute ("name", mapping.name);
        entry->setAttribute ("type", mapping.message == ArticulationMessage::controller ? "cc" : "note");
        entry->setAttribute ("channel", mapping.channel);
        entry->setAttribute ("number", mapping.number);
        entry->setAttribute ("value", mapping.value);
        entry->setAttribute ("shortcut", mapping.shortcut);
    }
}
}
