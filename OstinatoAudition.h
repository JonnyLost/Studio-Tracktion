// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "OstinatoCore.h"
#include <cmath>
#include <functional>

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
        if(finished)return false;
        if(!std::isfinite(now)||now<previous){stop(send);return false;}
        previous=now;
        if(index<render.deadlines.size()&&now-render.deadlines[index]>.100){stop(send);return false;}
        size_t dispatched=0;
        while(index<render.deadlines.size()&&render.deadlines[index]<=now)
        {
            if(++dispatched>512){stop(send);return false;}
            const auto e=render.midi.events[index++];
            auto it=std::find_if(held.begin(),held.begin()+count,[&](const auto& a){return a.route==e.route&&a.channel==e.channel&&a.pitch==e.pitch;});
            if(e.on)
            {
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
    std::array<Event,maxRows> held{};
    double previous=0;
    bool finished=false;
};
}
