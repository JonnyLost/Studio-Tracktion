// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <JuceHeader.h>
#include "UniversalInstrumentState.h"
#include <cmath>
#include <algorithm>

namespace studio::designer
{
// Normal Tracktion MIDI sequence events remain the playback/export authority.
// Only explicit M50 events are tagged; ordinary low notes are never guessed.
inline bool isNamedEvent(const juce::ValueTree& state)
{
    return state.getProperty("studioPerformanceElement").toString().isNotEmpty();
}
inline void tag(juce::ValueTree state,const Profile& p,const Element& e,juce::UndoManager* undo)
{
    state.setProperty("studioPerformanceElement",e.id,undo);
    state.setProperty("studioPerformanceProfile",p.id,undo);
    state.setProperty("studioPerformanceSemantic",e.semantic,undo);
    state.setProperty("studioPerformanceLabel",e.label,undo);
    state.setProperty("studioPerformanceKind",e.kind,undo);
}
inline void copyTag(const juce::ValueTree& from,juce::ValueTree to,juce::UndoManager* undo)
{
    if(!isNamedEvent(from))return;
    for(const auto* key:{"studioPerformanceElement","studioPerformanceProfile","studioPerformanceSemantic","studioPerformanceLabel","studioPerformanceKind"})
        to.setProperty(key,from.getProperty(key),undo);
}
inline bool supportedPerformanceClip(tracktion::MidiClip& clip)
{
    return clip.hasValidSequence()&&!clip.isLooping()&&!clip.isMuted()&&!clip.hasAnyTakes()
        &&!clip.getMPEMode()&&!clip.getQuantisation().isEnabled()&&clip.getGrooveTemplate().isEmpty();
}
inline bool fits(tracktion::MidiClip& clip,double beat)
{
    if(!std::isfinite(beat)||beat<0)return false;
    auto time=clip.getTimeOfContentBeat(tracktion::BeatPosition::fromBeats(beat));
    return time>=clip.getPosition().getStart()&&time<clip.getPosition().getEnd();
}
inline juce::Result writePerformance(tracktion::MidiClip& clip,const Profile& p,const Element& e,double beat,int value,juce::UndoManager* undo)
{
    namespace te=tracktion;
    if(!supportedPerformanceClip(clip)||!fits(clip,beat))
        return juce::Result::fail("Use a linear, unmuted MIDI clip without takes, MPE, groove or clip quantisation.");
    auto b=e.binding;b.value=value;
    if(b.resolvedChannel(p.channel)!=clip.getMidiChannel().getChannelNumber()||!auditionMessage(b,p.channel))
        return juce::Result::fail("The assignment must be valid and match the MIDI clip channel.");
    const auto at=te::BeatPosition::fromBeats(beat);
    auto& seq=clip.getSequence();
    if(b.type=="note")
    {
        const auto end=clip.getContentBeatAtTime(clip.getPosition().getEnd());
        const auto length=std::min(te::BeatDuration::fromBeats(0.0625),end-at);
        if(length.inBeats()<1.0/960.0)return juce::Result::fail("Not enough space for a keyswitch.");
        for(auto* note:seq.getNotes())
        {
            if(note->getNoteNumber()!=b.number||note->getStartBeat()>=at+length||note->getEndBeat()<=at)continue;
            // Prevent a pulse from stealing a real note or overlapping an existing switch.
            if(note->getStartBeat()==at&&isNamedEvent(note->state)
                &&note->state["studioPerformanceElement"].toString()==e.id
                &&note->state["studioPerformanceProfile"].toString()==p.id)return juce::Result::ok();
            return juce::Result::fail("A note already overlaps this keyswitch pitch.");
        }
        if(auto* note=seq.addNote(b.number,at,length,value,0,undo)){tag(note->state,p,e,undo);return juce::Result::ok();}
    }
    else
    {
        const int type=b.type=="cc"?b.number:b.type=="bend"?te::MidiControllerEvent::pitchWheelType:te::MidiControllerEvent::channelPressureType;
        const int stored=b.type=="bend"?value:value<<7;
        if(auto* existing=seq.getControllerEventAt(at,type);existing&&existing->getBeatPosition()==at)
        {
            if(!isNamedEvent(existing->state)||existing->state["studioPerformanceElement"].toString()!=e.id
                ||existing->state["studioPerformanceProfile"].toString()!=p.id)
                return juce::Result::fail("A different MIDI event occupies this controller/time.");
            existing->setControllerValue(stored,undo);return juce::Result::ok();
        }
        if(auto* event=seq.addControllerEvent(at,type,stored,undo)){tag(event->state,p,e,undo);return juce::Result::ok();}
    }
    return juce::Result::fail("Could not insert MIDI event.");
}
struct NamedPoint
{
    double beat=0;
    juce::ValueTree state;
    int number=-1,value=0;
    juce::String type;
};
inline std::vector<NamedPoint> namedPoints(tracktion::MidiClip& clip)
{
    std::vector<NamedPoint> result;
    if(!clip.hasValidSequence())return result;
    for(auto* n:clip.getSequence().getNotes())
        if(isNamedEvent(n->state))result.push_back({n->getStartBeat().inBeats(),n->state,n->getNoteNumber(),n->getVelocity(),"note"});
    for(auto* c:clip.getSequence().getControllerEvents())
        if(isNamedEvent(c->state))
        {
            const auto type=c->getType();
            result.push_back({c->getBeatPosition().inBeats(),c->state,type<120?type:0,
                type==tracktion::MidiControllerEvent::pitchWheelType?c->getControllerValue():c->getControllerValue()>>7,
                type==tracktion::MidiControllerEvent::pitchWheelType?"bend":type==tracktion::MidiControllerEvent::channelPressureType?"pressure":type>=0&&type<120?"cc":"unsupported"});
        }
    std::stable_sort(result.begin(),result.end(),[](const auto& a,const auto& b){return a.beat<b.beat;});
    return result;
}
inline bool removeNamed(tracktion::MidiClip& clip,const juce::ValueTree& state,juce::UndoManager* undo)
{
    if(!isNamedEvent(state))return false;
    if(auto* note=clip.getSequence().getNoteFor(state)){clip.getSequence().removeNote(*note,undo);return true;}
    for(auto* event:clip.getSequence().getControllerEvents())
        if(event->state==state){clip.getSequence().removeControllerEvent(*event,undo);return true;}
    return false;
}
// Match current profile exactly before chase. Never reinterpret an old event
// as a different keyswitch after profile replacement.
inline const Element* matchingElement(const Profile& p,const NamedPoint& n,int channel)
{
    if(n.state["studioPerformanceProfile"].toString()!=p.id)return nullptr;
    for(const auto& e:p.elements)
        if(e.id==n.state["studioPerformanceElement"].toString()
            &&e.binding.type==n.type&&e.binding.resolvedChannel(p.channel)==channel
            &&(n.type=="note"||n.type=="cc"?e.binding.number==n.number:true)
            &&(n.type=="note"?e.binding.value==n.value:true))return &e;
    return nullptr;
}
}
