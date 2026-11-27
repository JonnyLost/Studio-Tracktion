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
class AuditionCursor
{
public:
    explicit AuditionCursor(const TimedRender& r):render(r){}
    AuditionCursor(TimedRender&&)=delete;
    AuditionCursor(const TimedRender&&)=delete;
    template<class Sink> void stop(Sink&& send)
    {
        for(size_t i=0;i<count;++i){auto e=held[i];e.on=false;e.velocity=0;send(e);}
        count=0;finished=true;
    }
    // No catch-up burst after a blocked UI. Fail closed and release owned notes.
    template<class Sink> bool advance(double now,Sink&& send)
    {
        return advance(now,ConductingValues{},std::forward<Sink>(send));
    }
    template<class Sink> bool advance(double now,ConductingValues controls,Sink&& send)
    {
        if(finished)return false;
        if(!std::isfinite(now)||now<previous){stop(send);return false;}
        previous=now;
        if(index<render.deadlines.size()&&now-render.deadlines[index]>.100){stop(send);return false;}
        size_t dispatched=0;
        while(index<render.deadlines.size()&&render.deadlines[index]<=now)
        {
            if(++dispatched>512){stop(send);return false;}
            auto e=render.midi.events[index++];
            auto it=std::find_if(held.begin(),held.begin()+count,[&](const auto& a){return a.route==e.route&&a.channel==e.channel&&a.pitch==e.pitch;});
            if(e.on)
            {
                if(e.densityThreshold>std::clamp(controls.density,0,127))continue;
                e.velocity=conductedVelocity(e.velocity,controls.intensity);
                if(it!=held.begin()+count||count>=held.size()){stop(send);return false;}
                held[count++]=e;send(e);
            }
            else if(it!=held.begin()+count){*it=held[--count];send(e);}
        }
        if(now>=render.duration){stop(send);return false;}
        return true;
    }
    bool completed()const{return index==render.deadlines.size()&&previous>=render.duration;}
private:
    const TimedRender& render;
    size_t index=0,count=0;
    std::array<Event,maxHeldNotes> held{};
    double previous=0;
    bool finished=false;
};
}
