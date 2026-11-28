// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "InstrumentPerformanceSequence.h"
#include "InstrumentSemanticTranslation.h"
#include <map>

namespace studio::designer
{
struct TranslationEvent
{
    tracktion::MidiClip* clip=nullptr;
    NamedPoint original;
    Element target;
    int value=0;
};
struct TranslationPlan
{
    std::vector<TranslationEvent> events;
    juce::StringArray errors;
    std::map<String,int> mappings;
    int rawControllers=0, clips=0, errorCount=0;
    String fingerprint;
    void reject(const String& reason){++errorCount;if(errors.size()<100)errors.add(reason);}
    bool ready()const{return errorCount==0&&!events.empty();}
    String report()const
    {
        String s="Named events: "+String((int)events.size())+"   MIDI clips: "+String(clips)+"\n";
        s+="Ordinary MIDI controllers retained: "+String(rawControllers)+"\n\n";
        for(const auto& [name,count]:mappings)s+=name+"  ("+String(count)+")\n";
        if(errorCount){s+="\nBLOCKED — "+String(errorCount)+" issue(s). No MIDI will be changed.\n";for(const auto& e:errors)s+=e+"\n";}
        if(events.empty()&&!errorCount)s+="No named M50 events to translate.\n";
        return s;
    }
};
inline String describeBinding(const Element& e)
{
    return e.label+" ("+e.semantic+") ["+e.binding.type+(e.binding.type=="note"||e.binding.type=="cc"?" "+String(e.binding.number):String())+
        (e.kind=="articulation"?" = "+String(e.binding.value):String())+"]";
}
inline TranslationPlan planTranslation(const juce::Array<tracktion::MidiClip*>& clips,const Profile& source,const Profile& destination)
{
    namespace te=tracktion;
    TranslationPlan plan;
    if(source.validate().failed()||destination.validate().failed()||source.instrument!=destination.instrument)
    {plan.reject("Profiles must be valid and describe the same physical instrument.");return plan;}
    std::set<std::pair<int,int>> switchPitches;
    for(const auto* p:{&source,&destination})
        for(const auto& e:p->elements)if(e.binding.type=="note")switchPitches.emplace(e.binding.resolvedChannel(p->channel),e.binding.number);
    for(auto* c:clips)
    {
        if(!c)continue;
        ++plan.clips;
        plan.fingerprint+=c->itemID.toString()+":"+String(c->state.toXmlString().hashCode64())+";";
        // Alternate takes can conceal additional named events; don't modify this track.
        if(!supportedPerformanceClip(*c)){plan.reject(c->getName()+": unsupported clip (mute, loop, takes, MPE, groove or quantisation).");continue;}
        const int channel=c->getMidiChannel().getChannelNumber();
        for(auto* n:c->getSequence().getNotes())
            if(!isNamedEvent(n->state)&&switchPitches.count({channel,n->getNoteNumber()}))
                plan.reject(c->getName()+": ordinary note at a source/destination keyswitch pitch "+String(n->getNoteNumber())+". Resolve manually.");
        for(auto* e:c->getSequence().getControllerEvents())if(!isNamedEvent(e->state))++plan.rawControllers;
        for(const auto& n:namedPoints(*c))
        {
            const auto prefix=c->getName()+" @ "+String(n.beat,3)+": ";
            if(plan.events.size()>=16384){plan.reject("Translation exceeds 16,384 named events.");return plan;}
            auto* original=matchingElement(source,n,channel);
            if(!original||n.state["studioPerformanceSemantic"].toString()!=original->semantic
                ||n.state["studioPerformanceKind"].toString()!=original->kind||!fits(*c,n.beat))
            {plan.reject(prefix+"Stored event does not match the source profile or visible clip.");continue;}
            auto decision=translateBinding(source,*original,destination,channel,n.value);
            if(decision.result.failed()){plan.reject(prefix+original->label+": "+decision.result.getErrorMessage());continue;}
            if(!auditionMessage(decision.destination.binding,destination.channel))
            {plan.reject(prefix+"Invalid destination message.");continue;}
            if(decision.destination.binding.type=="note"&&
                (c->getContentBeatAtTime(c->getPosition().getEnd()).inBeats()-n.beat)<1.0/960.0)
            {plan.reject(prefix+"Not enough room for destination keyswitch.");continue;}
            ++plan.mappings[describeBinding(*original)+" -> "+describeBinding(decision.destination)];
            plan.events.push_back({c,n,decision.destination,decision.value});
        }
    }
    if(plan.errorCount||plan.events.empty())return plan;
    // Overlapping same-channel clips have no unique ordering at the track.
    for(int i=0;i<clips.size();++i)for(int j=i+1;j<clips.size();++j)
    {
        auto* a=clips[i];auto* b=clips[j];
        if(a&&b&&a->getMidiChannel().getChannelNumber()==b->getMidiChannel().getChannelNumber()
            &&a->getPosition().getStart()<b->getPosition().getEnd()&&b->getPosition().getStart()<a->getPosition().getEnd())
            plan.reject("Overlapping MIDI clips on the same channel: "+a->getName()+" / "+b->getName());
    }
    // Check destination events against untagged controllers and each other.
    // Keys are clip ID + physical message address; note intervals are half-open.
    struct Interval{double start,end;};
    std::map<std::pair<String,String>,std::vector<Interval>> destinations;
    std::map<String,std::set<String>> channelControlMeanings;
    std::set<String> articulationAddresses;
    for(const auto& e:plan.events)
    {
        auto& c=*e.clip;const auto& b=e.target.binding;
        const auto at=te::BeatPosition::fromBeats(e.original.beat);
        int address=b.type=="note"?1000+b.number:b.type=="cc"?b.number:
            b.type=="bend"?te::MidiControllerEvent::pitchWheelType:te::MidiControllerEvent::channelPressureType;
        double end=e.original.beat;
        if(b.type=="note")end=std::min(end+0.0625,c.getContentBeatAtTime(c.getPosition().getEnd()).inBeats());
        else
            if(auto* existing=c.getSequence().getControllerEventAt(at,address);existing&&existing->getBeatPosition()==at&&!isNamedEvent(existing->state))
                plan.reject(c.getName()+": destination controller collides with ordinary MIDI.");
        const auto addressKey=b.type+":"+String(b.number);
        destinations[{c.itemID.toString(),addressKey}].push_back({e.original.beat,end});
        if(b.type!="note")
        {
            const auto key=String(c.getMidiChannel().getChannelNumber())+":"+addressKey;
            if(e.target.kind=="control")channelControlMeanings[key].insert(e.target.semantic);
            else articulationAddresses.insert(key);
        }
    }
    for(const auto& [key,meanings]:channelControlMeanings)
        if(meanings.size()>1||articulationAddresses.count(key))plan.reject("Independent performance controls share destination address "+key+".");
    for(auto& [key,intervals]:destinations)
    {
        std::sort(intervals.begin(),intervals.end(),[](auto a,auto b){return a.start<b.start;});
        for(size_t i=1;i<intervals.size();++i)
            if(intervals[i].start==intervals[i-1].start||intervals[i].start<intervals[i-1].end)
                plan.reject("Two translated events occupy the same MIDI address/time.");
    }
    return plan;
}
inline juce::Result applyTranslation(const TranslationPlan& plan,const Profile& destination,juce::UndoManager& undo)
{
    if(!plan.ready())return juce::Result::fail("Translation is blocked.");
    undo.beginNewTransaction("Translate instrument performance");
    auto rollback=[&]{undo.undoCurrentTransactionOnly();undo.beginNewTransaction();return juce::Result::fail("Translation failed; MIDI edits rolled back.");};
    for(const auto& e:plan.events)if(!removeNamed(*e.clip,e.original.state,&undo))return rollback();
    for(const auto& e:plan.events)
        if(writePerformance(*e.clip,destination,e.target,e.original.beat,e.value,&undo).failed())return rollback();
    // Caller attaches destination snapshot in this same transaction.
    return juce::Result::ok();
}
}
