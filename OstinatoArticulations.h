// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "OstinatoCore.h"
namespace studio::ostinato
{
struct ArticulationBinding
{
    ArticulationRequest request;
    bool controller=false;
    int number=0,value=100;
    bool operator==(const ArticulationBinding&) const=default;
};
inline bool validateArticulations(const Render& r,const std::vector<ArticulationBinding>& bindings,std::string& error)
{
    auto fail=[&](const char* reason){error=reason;return false;};
    if(bindings.size()!=r.articulations.size())return fail("Missing articulation mapping.");
    std::set<std::pair<int,int>> channels;
    std::set<std::tuple<int,int,int>> reserved;
    for(size_t i=0;i<bindings.size();++i)
    {
        const auto& b=bindings[i];
        if(b.request!=r.articulations[i]||b.number<0||b.number>127||b.value<(b.controller?0:1)||b.value>127
            ||(b.controller&&(b.number>=120||(b.number>=64&&b.number<=69))))
            return fail("Invalid or unsafe articulation mapping.");
        channels.emplace(b.request.route,b.request.channel);
        if(!b.controller)reserved.emplace(b.request.route,b.request.channel,b.number);
    }
    // Channel-wide keyswitch/CC state is shared by every voice. Until a
    // library-specific polyphonic strategy exists, require serialized notes
    // on any channel that uses named articulation commands.
    std::map<std::pair<int,int>,Tick> ends;
    for(const auto& n:r.notes)
    {
        const auto key=std::pair(n.route,n.channel);
        if(reserved.count({n.route,n.channel,n.pitch}))return fail("Musical note collides with an articulation keyswitch.");
        if(n.articulation>=0)
        {
            if(size_t(n.articulation)>=bindings.size())return fail("Invalid articulation index.");
            const auto& request=bindings[size_t(n.articulation)].request;
            if(request.route!=n.route||request.channel!=n.channel)return fail("Articulation route/channel mismatch.");
        }
        if(channels.count(key))
        {
            if(ends[key]>n.start)return fail("Overlapping notes share an articulation-controlled MIDI channel. Use separate channels or shorter gates.");
            ends[key]=n.end;
        }
    }
    error.clear();return true;
}
}
