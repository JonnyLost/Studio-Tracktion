// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <iomanip>
#include <locale>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

namespace studio::ostinato
{
using Tick=std::int64_t;
constexpr Tick ticksPerQuarter=9600;
constexpr int maxRows=64,maxSteps=256,maxRepeats=256;
constexpr size_t maxNotes=65536;
struct Step
{
    bool enabled=false,accent=false;
    int velocity=90,gate=750; // gate in thousandths of a step
    bool operator==(const Step&) const=default;
};
struct Row
{
    std::uint32_t id=1;
    std::string name="Row 1",trackId; // opaque, stable project track identity
    int channel=1,pitch=60;
    bool mute=false,solo=false;
    std::vector<Step> steps=std::vector<Step>(16);
    bool operator==(const Row&) const=default;
};
struct Pattern
{
    std::string name="New ostinato";
    int stepCount=16,stepsPerQuarter=4;
    std::vector<Row> rows{Row{}};
    bool operator==(const Pattern&) const=default;
    Tick stepTicks() const {return stepsPerQuarter>0?ticksPerQuarter/stepsPerQuarter:0;}
    Tick duration() const {return stepTicks()*stepCount;}
    static bool textValid(const std::string& s,size_t max,bool empty=false)
    {
        return (empty||!s.empty())&&s.size()<=max&&std::none_of(s.begin(),s.end(),[](unsigned char c){return c<32||c==127;});
    }
    bool valid() const
    {
        if(!textValid(name,160)||stepCount<1||stepCount>maxSteps||stepsPerQuarter<1||stepsPerQuarter>32
            ||ticksPerQuarter%stepsPerQuarter||rows.empty()||rows.size()>maxRows)return false;
        std::set<std::uint32_t> ids;
        for(const auto& r:rows)
        {
            if(!r.id||!ids.insert(r.id).second||!textValid(r.name,80)||!textValid(r.trackId,256,true)
               ||r.channel<1||r.channel>16||r.pitch<0||r.pitch>127||r.steps.size()!=size_t(stepCount))return false;
            for(const auto& s:r.steps)if(s.velocity<1||s.velocity>127||s.gate<1||s.gate>1000)return false;
        }
        return true;
    }
    bool addRow()
    {
        if(!valid()||rows.size()>=maxRows)return false;
        std::uint32_t id=1;
        while(std::any_of(rows.begin(),rows.end(),[&](const auto& r){return r.id==id;}))++id;
        Row r;r.id=id;r.name="Row "+std::to_string(id);r.steps.resize(size_t(stepCount));
        rows.push_back(std::move(r));return true;
    }
    bool removeRow(std::uint32_t id)
    {
        if(rows.size()<=1)return false;
        auto it=std::find_if(rows.begin(),rows.end(),[&](const auto& r){return r.id==id;});
        if(it==rows.end())return false;
        rows.erase(it);return true;
    }
    bool setGrid(int count,int division)
    {
        if(!valid()||count<1||count>maxSteps||division<1||division>32||ticksPerQuarter%division)return false;
        // Explicitly refuse shortening over active notes rather than silently erase them.
        for(const auto& r:rows)for(size_t i=size_t(count);i<r.steps.size();++i)if(r.steps[i].enabled)return false;
        for(auto& r:rows)r.steps.resize(size_t(count));
        stepCount=count;stepsPerQuarter=division;return true;
    }
};

// Versioned portable pattern payload. Parse transactionally on the message/worker
// thread. Project embedding and named user library are later milestones.
inline std::string encode(const Pattern& p)
{
    if(!p.valid())return {};
    std::ostringstream out;out.imbue(std::locale::classic());
    out<<"STUDIO_OSTINATO 1 "<<std::quoted(p.name)<<' '<<p.stepCount<<' '<<p.stepsPerQuarter<<' '<<p.rows.size()<<'\n';
    for(const auto& r:p.rows)
    {
        out<<r.id<<' '<<std::quoted(r.name)<<' '<<std::quoted(r.trackId)<<' '<<r.channel<<' '<<r.pitch<<' '<<int(r.mute)<<' '<<int(r.solo)<<'\n';
        for(const auto& s:r.steps)out<<int(s.enabled)<<' '<<int(s.accent)<<' '<<s.velocity<<' '<<s.gate<<'\n';
    }
    return out.str();
}
inline bool decode(const std::string& text,Pattern& output)
{
    if(text.empty()||text.size()>1024*1024)return false;
    std::istringstream in(text);in.imbue(std::locale::classic());
    std::string magic;int version=0,count=0;
    Pattern p;
    if(!(in>>magic>>version>>std::quoted(p.name)>>p.stepCount>>p.stepsPerQuarter>>count)
        ||magic!="STUDIO_OSTINATO"||version!=1||count<1||count>maxRows||p.stepCount<1||p.stepCount>maxSteps)return false;
    p.rows.clear();
    auto flag=[&](bool& result){int i=-1;if(!(in>>i)||i<0||i>1)return false;result=i==1;return true;};
    for(int i=0;i<count;++i)
    {
        Row r;
        std::int64_t id=0;
        if(!(in>>id>>std::quoted(r.name)>>std::quoted(r.trackId)>>r.channel>>r.pitch)
            ||id<1||id>UINT32_MAX||!flag(r.mute)||!flag(r.solo))return false;
        r.id=static_cast<std::uint32_t>(id);r.steps.resize(size_t(p.stepCount));
        for(auto& s:r.steps)if(!flag(s.enabled)||!flag(s.accent)||!(in>>s.velocity>>s.gate))return false;
        p.rows.push_back(std::move(r));
    }
    in>>std::ws;if(!in.eof()||!p.valid())return false;
    output=std::move(p);return true;
}
struct Note
{
    Tick start=0,end=0;
    int route=0,channel=1,pitch=60,velocity=90;
    bool operator==(const Note&) const=default;
};
struct Event
{
    Tick tick=0;
    int route=0,channel=1,pitch=60,velocity=0;
    bool on=false;
    bool operator==(const Event&) const=default;
};
struct Render
{
    Tick duration=0;
    std::vector<std::string> routes;
    std::vector<Note> notes;
    std::vector<Event> events;
    int unassignedRows=0;
};
// Compile off the audio thread. No routing fallback: absent destinations fail
// closed. Unassigned rows are silent and counted. Resolve collisions on the
// same physical route/channel/pitch; simultaneous hits merge, later hits
// retrigger after the earlier note is released.
inline bool compile(const Pattern& p,const std::vector<std::string>& existingTracks,int repeats,Render& output,std::string& error)
{
    auto fail=[&](const char* why){error=why;return false;};
    if(!p.valid()||repeats<1||repeats>maxRepeats)return fail("Invalid pattern or repeat count.");
    Render next;next.duration=p.duration()*repeats;
    const bool solo=std::any_of(p.rows.begin(),p.rows.end(),[](const auto& r){return r.solo;});
    for(const auto& row:p.rows)
    {
        if(row.mute||(solo&&!row.solo))continue;
        if(row.trackId.empty()){++next.unassignedRows;continue;}
        if(std::find(existingTracks.begin(),existingTracks.end(),row.trackId)==existingTracks.end())return fail("Assigned track no longer exists.");
        auto it=std::find(next.routes.begin(),next.routes.end(),row.trackId);
        int route=int(it-next.routes.begin());
        if(it==next.routes.end())next.routes.push_back(row.trackId);
        for(int cycle=0;cycle<repeats;++cycle)
            for(int step=0;step<p.stepCount;++step)
            {
                const auto& s=row.steps[size_t(step)];
                if(!s.enabled)continue;
                if(next.notes.size()>=maxNotes)return fail("Pattern exceeds bounded render size.");
                const Tick start=cycle*p.duration()+step*p.stepTicks();
                const Tick length=std::max<Tick>(1,p.stepTicks()*s.gate/1000);
                next.notes.push_back({start,std::min(start+length,next.duration),route,row.channel,row.pitch,std::min(127,s.velocity+(s.accent?20:0))});
            }
    }
    auto address=[](const Note& n){return std::tuple(n.route,n.channel,n.pitch);};
    std::sort(next.notes.begin(),next.notes.end(),[&](const auto& a,const auto& b){
        return std::tuple(address(a),a.start,a.end,a.velocity)<std::tuple(address(b),b.start,b.end,b.velocity);
    });
    std::vector<Note> normalized;normalized.reserve(next.notes.size());
    for(const auto& n:next.notes)
    {
        if(!normalized.empty()&&address(normalized.back())==address(n))
        {
            auto& prior=normalized.back();
            if(prior.start==n.start){prior.end=std::max(prior.end,n.end);prior.velocity=std::max(prior.velocity,n.velocity);continue;}
            prior.end=std::min(prior.end,n.start);
        }
        normalized.push_back(n);
    }
    next.notes=std::move(normalized);
    std::sort(next.notes.begin(),next.notes.end(),[&](const auto& a,const auto& b){return std::tuple(a.start,address(a))<std::tuple(b.start,address(b));});
    next.events.reserve(next.notes.size()*2);
    for(const auto& n:next.notes)
    {
        next.events.push_back({n.start,n.route,n.channel,n.pitch,n.velocity,true});
        next.events.push_back({n.end,n.route,n.channel,n.pitch,0,false});
    }
    std::sort(next.events.begin(),next.events.end(),[](const auto& a,const auto& b){
        return std::tie(a.tick,a.on,a.route,a.channel,a.pitch)<std::tie(b.tick,b.on,b.route,b.channel,b.pitch);
    });
    output=std::move(next);error.clear();return true;
}

// Allocation-free consumer of an immutable, precompiled render. Caller owns
// the render lifetime and provides a nonallocating MIDI sink. Beat ticks,
// not seconds or wall-clock timers, determine ordering. A transport adapter
// will translate tick positions to sample offsets in a later milestone.
class Cursor
{
public:
    explicit Cursor(const Render& source):render(source){}
    Cursor(const Cursor&)=delete;Cursor& operator=(const Cursor&)=delete;
    template<class Sink> void stop(Tick at,Sink&& sink)
    {
        for(size_t i=0;i<activeCount;++i)
        {
            auto e=active[i];e.tick=at;e.on=false;e.velocity=0;sink(e);
        }
        activeCount=0;started=false;index=0;expected=0;
    }
    template<class Sink> bool process(Tick start,Tick end,Sink&& sink)
    {
        if(start<0||end<start){stop(std::max<Tick>(0,start),sink);return false;}
        if(!started||start!=expected)
        {
            stop(start,sink);
            index=size_t(std::lower_bound(render.events.begin(),render.events.end(),start,[](const Event& e,Tick t){return e.tick<t;})-render.events.begin());
            started=true;
        }
        while(index<render.events.size()&&render.events[index].tick<end)
        {
            auto e=render.events[index++];
            auto same=[&](const Event& a){return a.route==e.route&&a.channel==e.channel&&a.pitch==e.pitch;};
            auto it=std::find_if(active.begin(),active.begin()+activeCount,same);
            if(e.on)
            {
                if(it!=active.begin()+activeCount||activeCount>=active.size()){stop(e.tick,sink);return false;}
                active[activeCount++]=e;sink(e);
            }
            else if(it!=active.begin()+activeCount)
            {
                *it=active[--activeCount];sink(e);
            }
        }
        expected=end;return true;
    }
    size_t held()const noexcept{return activeCount;}
private:
    const Render& render;
    std::array<Event,maxRows> active{};
    size_t activeCount=0,index=0;
    Tick expected=0;bool started=false;
};
}
