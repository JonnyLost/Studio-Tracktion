// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <juce_core/juce_core.h>
#include <map>
#include <set>

namespace studio::designer
{
// Immutable discovery data. This class never sends MIDI or changes a plug-in.
// Load on the UI/worker thread, not the audio callback.
class Catalog final
{
public:
    juce::Result load(const juce::String& json)
    {
        Catalog next;
        if (auto r = juce::JSON::parse(json, next.root); r.failed()) return r;
        if (!next.root.isObject() || int(next.root["schemaVersion"]) != 1)
            return juce::Result::fail("Unsupported instrument catalog schema");
        if (!index(next.root["instruments"], next.instruments)
            || !index(next.root["techniques"], next.techniques)
            || !index(next.root["controls"], next.controls)
            || !index(next.root["families"], next.families))
            return juce::Result::fail("Missing or duplicate catalog IDs");
        auto* groups = next.root["techniqueGroups"].getDynamicObject();
        if (groups == nullptr) return juce::Result::fail("Missing technique groups");
        for (const auto& property : groups->getProperties())
        {
            auto* members = property.value.getArray();
            if (members == nullptr) return juce::Result::fail("Invalid technique group");
            for (const auto& member : *members)
                if (next.techniques.count(member.toString()) != 1)
                    return juce::Result::fail("Unknown technique ID");
        }
        for (const auto& entry : next.instruments)
        {
            const auto& item = entry.second;
            if (next.families.count(item["family"].toString()) != 1)
                return juce::Result::fail("Unknown instrument family");
            auto* candidates = item["candidateTechniqueGroups"].getArray();
            if (candidates == nullptr) return juce::Result::fail("Missing candidate groups");
            for (const auto& group : *candidates)
                if (!groups->hasProperty(juce::Identifier(group.toString())))
                    return juce::Result::fail("Unknown candidate group");
            if (auto* overrides = item["techniqueOverrides"].getArray())
                for (const auto& rule : *overrides)
                {
                    if (rule["mode"].toString() != "suggest")
                        return juce::Result::fail("Unknown override mode");
                    auto* ids = rule["ids"].getArray();
                    if (ids == nullptr) return juce::Result::fail("Missing override IDs");
                    for (const auto& id : *ids)
                        if (next.techniques.count(id.toString()) != 1)
                            return juce::Result::fail("Unknown override ID");
                }
        }
        if (auto* hints = next.root["documentedPatchHints"].getArray())
            for (const auto& hint : *hints)
            {
                if (bool(hint["enabled"]) || next.instruments.count(hint["instrumentId"].toString()) != 1)
                    return juce::Result::fail("Invalid or enabled discovery hint");
                auto* bindings = hint["bindings"].getArray();
                if (bindings == nullptr) return juce::Result::fail("Missing hint bindings");
                for (const auto& binding : *bindings)
                    if (next.techniques.count(binding["techniqueId"].toString()) != 1)
                        return juce::Result::fail("Unknown hint technique");
            }
        *this = std::move(next); // transactional: failed loads retain the current catalog
        return juce::Result::ok();
    }

    size_t instrumentCount() const noexcept { return instruments.size(); }
    size_t techniqueCount() const noexcept { return techniques.size(); }
    size_t controlCount() const noexcept { return controls.size(); }
    juce::var instrument(const juce::String& id) const { return lookup(instruments, id); }
    juce::var technique(const juce::String& id) const { return lookup(techniques, id); }
    juce::var control(const juce::String& id) const { return lookup(controls, id); }

    juce::StringArray searchInstruments(const juce::String& query) const
    {
        juce::StringArray result;
        const auto q = query.trim();
        for (const auto& [id, item] : instruments)
        {
            bool matches = q.isEmpty() || id.containsIgnoreCase(q) || item["name"].toString().containsIgnoreCase(q);
            if (auto* aliases = item["aliases"].getArray())
                for (const auto& alias : *aliases)
                    matches |= alias.toString().containsIgnoreCase(q);
            if (matches) result.add(id);
        }
        return result;
    }

    juce::StringArray allTechniques() const { return keys(techniques); }
    juce::StringArray allControls() const { return keys(controls); }

    juce::var documentedHints() const { return root["documentedPatchHints"].clone(); }
    juce::String exactTechnique(const juce::String& label) const
    {
        juce::String result;
        const auto query=label.trim();
        if(query.isEmpty())return {};
        for(const auto& [id,item]:techniques)
        {
            bool match=item["name"].toString().trim().equalsIgnoreCase(query);
            const auto aliases=item["aliases"];
            if(auto* a=aliases.getArray())for(const auto& alias:*a)match|=alias.toString().trim().equalsIgnoreCase(query);
            if(match){if(result.isNotEmpty())return {};result=id;}
        }
        return result;
    }

    // Explicit instrument suggestions first. Family-level terms are only a
    // broad discovery list, never evidence of a playable articulation.
    juce::StringArray candidateTechniques(const juce::String& id) const
    {
        juce::StringArray result;
        auto item = instrument(id);
        if (!item.isObject()) return result;
        if (auto* overrides = item["techniqueOverrides"].getArray())
            for (const auto& rule : *overrides)
                if (auto* ids = rule["ids"].getArray())
                    for (const auto& value : *ids) result.addIfNotAlreadyThere(value.toString());
        if (auto* groups = item["candidateTechniqueGroups"].getArray())
            for (const auto& group : *groups)
                if (auto* values = root["techniqueGroups"][juce::Identifier(group.toString())].getArray())
                    for (const auto& value : *values) result.addIfNotAlreadyThere(value.toString());
        return result;
    }
private:
    using Index = std::map<juce::String, juce::var>;
    juce::var root;
    Index instruments, techniques, controls, families;
    static juce::StringArray keys(const Index& index)
    {
        juce::StringArray result;
        for (const auto& entry : index) result.add(entry.first);
        return result;
    }
    static bool index(const juce::var& array, Index& out)
    {
        auto* rows = array.getArray();
        if (rows == nullptr || rows->isEmpty()) return false;
        for (const auto& row : *rows)
        {
            auto id = row["id"].toString();
            if (!row.isObject() || id.isEmpty() || !out.emplace(id, row).second) return false;
        }
        return true;
    }
    static juce::var lookup(const Index& index, const juce::String& id)
    {
        auto it = index.find(id);
        return it == index.end() ? juce::var{} : it->second.clone();
    }
};
}
