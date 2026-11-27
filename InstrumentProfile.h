// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <juce_core/juce_core.h>
#include <vector>
#include <set>

namespace studio::designer
{
using juce::String;
struct Element
{
    String id, semantic, label, kind, presentation;
    bool transport = true, full = true;
    // M46 deliberately creates no enabled MIDI or AU mappings.
    bool operator==(const Element&) const = default;
};
struct Profile
{
    String id = juce::Uuid().toString(), name = "Untitled instrument", instrument,
           library, patch, pluginId, pluginVersion, artwork, variant;
    int channel = 1, slot = 1;
    bool channelInfo = true;
    std::vector<Element> elements;
    bool operator==(const Profile&) const = default;
    Profile duplicate() const { auto p = *this; p.id = juce::Uuid().toString(); p.name += " Copy"; return p; }

    juce::Result validate() const
    {
        const auto bounded = [](const String& s, int max) { return s.length() <= max; };
        if (id.length() != 32 || !id.containsOnly("0123456789abcdefABCDEF")
            || name.trim().isEmpty() || !bounded(name, 160) || instrument.isEmpty()
            || !bounded(instrument, 180) || !bounded(library, 250) || !bounded(patch, 250)
            || !bounded(pluginId, 512) || !bounded(pluginVersion, 80) || !bounded(variant, 160)
            || !bounded(artwork, 180) || channel < 1 || channel > 16 || slot < 1 || slot > 16
            || elements.size() > 128)
            return juce::Result::fail("Invalid profile identity or too many controls.");
        std::set<String> ids;
        for (const auto& e : elements)
        {
            const bool art = e.kind == "articulation", control = e.kind == "control";
            if (e.id.length() != 32 || !e.id.containsOnly("0123456789abcdefABCDEF") || !ids.insert(e.id).second
                || e.semantic.isEmpty() || !bounded(e.semantic, 180) || e.label.trim().isEmpty()
                || !bounded(e.label, 80) || (!art && !control)
                || (art && e.presentation != "button")
                || (control && e.presentation != "knob" && e.presentation != "fader" && e.presentation != "button")
                || (!e.transport && !e.full))
                return juce::Result::fail("Invalid control, duplicate ID, or no visible surface.");
        }
        return juce::Result::ok();
    }
    std::unique_ptr<juce::XmlElement> xml() const
    {
        auto x = std::make_unique<juce::XmlElement>("StudioInstrumentProfile");
        x->setAttribute("schemaVersion", 1);
        x->setAttribute("id", id); x->setAttribute("name", name); x->setAttribute("instrument", instrument);
        x->setAttribute("library", library); x->setAttribute("patch", patch);
        x->setAttribute("pluginId", pluginId); x->setAttribute("pluginVersion", pluginVersion);
        x->setAttribute("artwork", artwork); x->setAttribute("variant", variant);
        x->setAttribute("channel", channel); x->setAttribute("slot", slot); x->setAttribute("channelInfo", channelInfo);
        for (const auto& e : elements)
        {
            auto* n = x->createNewChildElement("Element");
            n->setAttribute("id",e.id); n->setAttribute("semantic",e.semantic);
            n->setAttribute("label",e.label); n->setAttribute("kind",e.kind); n->setAttribute("presentation",e.presentation);
            n->setAttribute("transport",e.transport); n->setAttribute("full",e.full);
            n->setAttribute("binding","unassigned");
        }
        return x;
    }
    static juce::Result read(const juce::XmlElement& x, Profile& out)
    {
        if (!x.hasTagName("StudioInstrumentProfile") || x.getStringAttribute("schemaVersion") != "1")
            return juce::Result::fail("Unsupported instrument profile version.");
        Profile p;
        p.id=x.getStringAttribute("id"); p.name=x.getStringAttribute("name"); p.instrument=x.getStringAttribute("instrument");
        p.library=x.getStringAttribute("library"); p.patch=x.getStringAttribute("patch");
        p.pluginId=x.getStringAttribute("pluginId"); p.pluginVersion=x.getStringAttribute("pluginVersion");
        p.artwork=x.getStringAttribute("artwork"); p.variant=x.getStringAttribute("variant");
        p.channel=x.getIntAttribute("channel",0); p.slot=x.getIntAttribute("slot",0); p.channelInfo=x.getBoolAttribute("channelInfo",true);
        for (auto* n = x.getFirstChildElement(); n != nullptr; n = n->getNextElement())
        {
            if (!n->hasTagName("Element") || n->getStringAttribute("binding") != "unassigned" || p.elements.size() >= 128)
                return juce::Result::fail("Unsupported or oversized element list.");
            p.elements.push_back({n->getStringAttribute("id"),n->getStringAttribute("semantic"),n->getStringAttribute("label"),
                n->getStringAttribute("kind"),n->getStringAttribute("presentation"),n->getBoolAttribute("transport"),n->getBoolAttribute("full")});
        }
        if (auto r=p.validate(); r.failed()) return r;
        out=std::move(p); return juce::Result::ok();
    }
};
class ProfileStore
{
public:
    explicit ProfileStore(juce::File folder) : directory(std::move(folder)) {}
    static juce::File localDirectory()
    {
        return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile("Studio").getChildFile("Instrument Profiles");
    }
    juce::Result save(const Profile& p) const
    {
        if (auto r=p.validate(); r.failed()) return r;
        if (auto r=directory.createDirectory(); r.failed()) return r;
        auto destination=directory.getChildFile(p.id+".studioinstrument");
        if (destination.isSymbolicLink()) return juce::Result::fail("Refusing symbolic link.");
        juce::TemporaryFile temp(destination);
        if (!temp.getFile().replaceWithText(p.xml()->toString()))
            return juce::Result::fail("Could not write profile.");
        return temp.overwriteTargetFileWithTemporary() ? juce::Result::ok() : juce::Result::fail("Could not replace profile.");
    }
    static juce::Result load(const juce::File& f, Profile& out)
    {
        if (f.isSymbolicLink() || !f.existsAsFile() || f.getSize()>512*1024)
            return juce::Result::fail("Profile unavailable or too large.");
        auto text=f.loadFileAsString();
        if (text.containsIgnoreCase("<!DOCTYPE") || text.containsIgnoreCase("<!ENTITY"))
            return juce::Result::fail("External XML declarations are not supported.");
        auto x=juce::parseXML(text);
        if (x==nullptr) return juce::Result::fail("Unreadable profile.");
        return Profile::read(*x,out);
    }
    std::vector<Profile> list() const
    {
        std::vector<Profile> result;
        for (const auto& f : directory.findChildFiles(juce::File::findFiles,false,"*.studioinstrument"))
        {
            Profile p;
            if (load(f,p).wasOk() && f.getFileNameWithoutExtension()==p.id) result.push_back(std::move(p));
        }
        return result;
    }
private:
    juce::File directory;
};
}
