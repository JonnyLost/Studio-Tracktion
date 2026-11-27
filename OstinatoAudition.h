// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "OstinatoCore.h"
#include <cmath>
#include <functional>
#include <atomic>

namespace studio::ostinato
{
// Stopped-transport audition only. Precompute absolute deadlines using the
// project's tempo map; the native playback graph is not driven by this timer.
struct TimedRender
{
    Render midi;
    std::vector<double> deadlines;
    double duration=0;
};
template<class ToSeconds>
bool prepareAudition(Render midi,ToSeconds&& seconds,TimedRender& output)
{
    TimedRender next;next.midi=std::move(midi);
    const double zero=seconds(Tick(0));
    next.duration=seconds(next.midi.duration)-zero;
    if(!std::isfinite(zero)||!std::isfinite(next.duration)||next.duration<=0||next.duration>600)return false;
    next.deadlines.reserve(next.midi.events.size());
    for(const auto& e:next.midi.events)
    {
        const double t=seconds(e.tick)-zero;
        if(!std::isfinite(t)||t<0||t>next.duration||(!next.deadlines.empty()&&t<next.deadlines.back()))return false;
        next.deadlines.push_back(t);
    }
    output=std::move(next);return true;
}
struct ConductingValues
{
    int intensity=127,density=127;
    bool operator==(const ConductingValues&) const=default;
};
inline int conductedVelocity(int velocity,int intensity)
{
    // 25–100% of the authored velocity. Zero is soft, not note-off.
    return std::clamp((velocity*(127+3*std::clamp(intensity,0,127))+254)/508,1,127);
}
// MIDI callback writes only an atomic packed mailbox; UI consumes latest values.
struct ConductingMapping
{
    int intensityCC=1,densityCC=11;
    bool valid()const noexcept
    {
        // Exclude channel-mode messages and pedals. This is a dedicated input,
        // but these messages have special MIDI meaning and are poor choices.
        auto allowed=[](int n){return n>=0&&n<120&&!(n>=64&&n<=69);};
        return allowed(intensityCC)&&allowed(densityCC)&&intensityCC!=densityCC;
    }
    int role(int cc)const noexcept{return cc==intensityCC?1:cc==densityCC?11:-1;}
};
class ConductingMailbox
{
public:
    void reset(){packed.store(0,std::memory_order_relaxed);}
    void receive(int cc,int value) noexcept
    {
        if((cc!=1&&cc!=11)||value<0||value>127)return;
        const unsigned shift=cc==1?0:8;
        const unsigned mask=255u<<shift, bits=unsigned(value+1)<<shift;
        auto old=packed.load(std::memory_order_relaxed);
        while(!packed.compare_exchange_weak(old,(old&~mask)|bits,std::memory_order_relaxed)){}
    }
    ConductingValues consume(ConductingValues current) noexcept
    {
        auto v=packed.exchange(0,std::memory_order_relaxed);
        if(v&255u)current.intensity=int(v&255u)-1;
        if((v>>8)&255u)current.density=int((v>>8)&255u)-1;
        return current;
    }
private:std::atomic<unsigned> packed{0};
};
// Dedicated keyboard chord input: note states only, no audio/MIDI forwarding.
class HarmonyMailbox
{
public:
    void reset() noexcept
    {
        for(auto& k:keys)k.store(false,std::memory_order_relaxed);
        revision.store(0);press.store(0);
    }
    void note(int pitch,bool on) noexcept
    {
        if(pitch<0||pitch>127)return;
        keys[size_t(pitch)].store(on,std::memory_order_relaxed);
        if(on)press.fetch_add(1,std::memory_order_relaxed);
        revision.fetch_add(1,std::memory_order_release);
    }
    void releaseAll() noexcept
    {
        for(auto& k:keys)k.store(false,std::memory_order_relaxed);
        revision.fetch_add(1,std::memory_order_release);
    }
    struct Snapshot{std::array<bool,128> keys{};unsigned revision=0,press=0;};
    Snapshot snapshot()const noexcept
    {
        Snapshot s;
        s.revision=revision.load(std::memory_order_acquire);
        for(size_t i=0;i<keys.size();++i)s.keys[i]=keys[i].load(std::memory_order_relaxed);
        s.press=press.load(std::memory_order_relaxed);
        return s;
    }
private:
    std::array<std::atomic<bool>,128> keys{};
    std::atomic<unsigned> revision{0},press{0};
};
// Settle chord attacks over 35 ms. Release-only changes do not replace
// a latched chord, so lifting fingers doesn't cause accidental inversions.
class HarmonyLatch
{
public:
    Harmony consume(const HarmonyMailbox::Snapshot& s,double now,Harmony current)
    {
        if(s.press!=lastPress){lastPress=s.press;pending=true;}
        if(s.revision!=lastRevision){lastRevision=s.revision;changedAt=now;}
        if(pending&&now-changedAt>=.035)
        {
            current=harmonyFromKeys(s.keys,current);pending=false;
        }
        return current;
    }
private:
    unsigned lastPress=0,lastRevision=0;
    double changedAt=0;bool pending=false;
};
class AuditionCursor
{
public:
    explicit AuditionCursor(const TimedRender& r):render(r){}
    AuditionCursor(TimedRender&&)=delete;
    AuditionCursor(const TimedRender&&)=delete;
    template<class Sink> void stop(Sink&& send)
    {
        for(size_t i=0;i<count;++i){auto e=held[i].event;e.on=false;e.velocity=0;send(e);}
        count=0;finished=true;
    }
    // No catch-up burst after a blocked UI. Fail closed and release owned notes.
    template<class Sink> bool advance(double now,Sink&& send)
    {
        return advance(now,ConductingValues{},std::forward<Sink>(send));
    }
    template<class Sink> bool advance(double now,ConductingValues controls,Sink&& send)
    {
        return advance(now,controls,render.midi.harmony,std::forward<Sink>(send));
    }
    template<class Sink> bool advance(double now,ConductingValues controls,const Harmony& harmony,Sink&& send)
    {
        if(finished)return false;
        if(!harmony.valid()||!std::isfinite(now)||now<previous){stop(send);return false;}
        previous=now;
        if(index<render.deadlines.size()&&now-render.deadlines[index]>.100){stop(send);return false;}
        size_t dispatched=0;
        while(index<render.deadlines.size()&&render.deadlines[index]<=now)
        {
            if(++dispatched>512){stop(send);return false;}
            auto e=render.midi.events[index++];
            auto it=std::find_if(held.begin(),held.begin()+count,[&](const auto& a){return a.event.route==e.route&&a.event.channel==e.channel&&a.referencePitch==e.pitch;});
            if(e.on)
            {
                if(e.densityThreshold>std::clamp(controls.density,0,127))continue;
                e.velocity=conductedVelocity(e.velocity,controls.intensity);
                if(it!=held.begin()+count||count>=held.size()){stop(send);return false;}
                const int reference=e.pitch;
                if(int(e.recipe.mode)>=2)e.pitch=resolvePitch(e.recipe,harmony);
                // Never steal a held note when harmony changes or two recipes
                // converge. The matching reference off will be ignored.
                if(e.pitch<0||e.pitch>127||std::any_of(held.begin(),held.begin()+count,[&](const auto& a){
                    return a.event.route==e.route&&a.event.channel==e.channel&&a.event.pitch==e.pitch;
                })){++skipped;continue;}
                held[count++]={e,reference};send(e);
            }
            else if(it!=held.begin()+count){e.pitch=it->event.pitch;*it=held[--count];send(e);}
        }
        if(now>=render.duration){stop(send);return false;}
        return true;
    }
    size_t skippedNotes()const{return skipped;}
    bool completed()const{return index==render.deadlines.size()&&previous>=render.duration;}
private:
    const TimedRender& render;
    size_t index=0,count=0;
    struct Held{Event event;int referencePitch=0;};
    std::array<Held,maxRows> held{};
    size_t skipped=0;
    double previous=0;
    bool finished=false;
};
}
