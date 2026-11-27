// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "InstrumentProfile.h"
#include <algorithm>

namespace studio::designer
{
struct TranslationDecision
{
    juce::Result result=juce::Result::fail("Unresolved semantic mapping.");
    Element destination;
    int value=0;
};
inline TranslationDecision translateBinding(const Profile& source,const Element& original,
    const Profile& destination,int channel,int value)
{
    auto fail=[](const String& message){return TranslationDecision{juce::Result::fail(message),{},0};};
    if(source.instrument!=destination.instrument)return fail("Different physical instrument IDs.");
    if(original.semantic.startsWith("aria.label."))return fail("Local label is not a portable semantic ID.");
    if(!original.binding.valid()||original.binding.type=="unassigned"||original.binding.resolvedChannel(source.channel)!=channel)
        return fail("Source assignment does not match the clip channel.");
    int sourceCount=0,destCount=0;
    const Element* target=nullptr;
    for(const auto& e:source.elements)if(e.semantic==original.semantic&&e.kind==original.kind)++sourceCount;
    for(const auto& e:destination.elements)if(e.semantic==original.semantic&&e.kind==original.kind){target=&e;++destCount;}
    if(sourceCount!=1)return fail("Ambiguous source technique (variants may differ).");
    if(destCount!=1)return fail(destCount==0?"Destination has no matching semantic ID.":"Ambiguous destination technique.");
    if(!target->binding.valid()||target->binding.type=="unassigned")return fail("Destination assignment is not configured.");
    if(target->binding.resolvedChannel(destination.channel)!=channel)return fail("Destination assignment requires a different MIDI channel.");
    const auto& from=original.binding;const auto& to=target->binding;
    if(original.kind=="articulation")
    {
        if((from.type!="note"&&from.type!="cc")||(to.type!="note"&&to.type!="cc")||value!=from.value)
            return fail("Only fixed note/CC articulation selections can be translated.");
        return {juce::Result::ok(),*target,to.value};
    }
    // Same semantic controller, same numeric domain; never assume pitch range,
    // inverted scales, pedal thresholds, or velocity-driven behavior.
    const auto seven=[](const String& type){return type=="cc"||type=="pressure";};
    if(seven(from.type)&&seven(to.type)&&value>=0&&value<=127)
    {
        if((from.type=="cc"&&from.number==64)!=(to.type=="cc"&&to.number==64))
            return fail("Sustain and continuous controller behavior differ.");
        return {juce::Result::ok(),*target,value};
    }
    if(from.type=="bend"&&to.type=="bend"&&value>=0&&value<=16383)
        return {juce::Result::ok(),*target,value};
    return fail("Incompatible performance-control message types.");
}
}
