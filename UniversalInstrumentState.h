// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "InstrumentMidiBinding.h"
#include <map>

namespace studio::designer
{
// Project-local attachment: copy the profile, don't depend on the global
// library file. The exact instrument instance is part of its identity.
struct Attachment
{
    Profile profile;
    String instance;
    String serialise() const
    {
        if(instance.isEmpty()||profile.validate().failed())return {};
        auto xml=std::make_unique<juce::XmlElement>("StudioInstrumentAttachment");
        xml->setAttribute("version",1);xml->setAttribute("instance",instance);
        xml->addChildElement(profile.xml().release());
        return xml->toString();
    }
    static std::optional<Attachment> parse(const String& source)
    {
        if(source.isEmpty()||source.length()>512*1024||source.containsIgnoreCase("<!DOCTYPE")||source.containsIgnoreCase("<!ENTITY"))return {};
        auto xml=juce::parseXML(source);
        if(!xml||!xml->hasTagName("StudioInstrumentAttachment")||xml->getStringAttribute("version")!="1"
            ||xml->getNumChildElements()!=1)return {};
        Attachment a;a.instance=xml->getStringAttribute("instance");
        if(a.instance.isEmpty()||Profile::read(*xml->getFirstChildElement(),a.profile).failed())return {};
        return a;
    }
    bool matches(const String& id,const String& plugin,const String& version) const
    {
        return instance==id&&profile.pluginId.isNotEmpty()&&profile.pluginId==plugin
            &&profile.pluginVersion==version&&profile.patch.trim().isNotEmpty();
    }
};
struct LiveValues
{
    // Observed values are not inferred from defaults. Never emit on restore.
    std::map<String,int> values;
    std::map<int,String> active;
    void applied(const Element& e,int profileChannel,int value)
    {
        if(e.binding.type=="unassigned")return;
        values[e.id]=value;
        if(e.kind=="articulation")active[e.binding.resolvedChannel(profileChannel)]=e.id;
    }
    bool selected(const Element& e,int profileChannel) const
    {
        auto it=active.find(e.binding.resolvedChannel(profileChannel));
        return it!=active.end()&&it->second==e.id;
    }
    std::optional<int> value(const String& id) const
    {
        auto it=values.find(id);return it==values.end()?std::nullopt:std::optional<int>(it->second);
    }
};
}
