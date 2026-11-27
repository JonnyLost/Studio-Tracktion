// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "OstinatoAudition.h"

namespace studio::ostinato
{
constexpr Tick printLead=ticksPerQuarter/16; // same 1/16-beat lead as articulation palette
struct PrintMapping
{
    int route=0,articulation=0,channel=1,number=0,value=0;
    bool keyswitch=false;
};
struct PrintNote
{
    Tick start=0,end=0;int pitch=60,velocity=90;
    bool operator==(const PrintNote&)const=default;
};
struct PrintCC
{
    Tick tick=0;int number=0,value=0;
    bool operator==(const PrintCC&)const=default;
};
struct PrintPart
{
    int route=0,channel=1;
    std::vector<PrintNote> notes;
    std::vector<PrintCC> controllers;
};
struct PrintPlan
{
    Tick duration=0,lead=0;
    std::vector<PrintPart> parts;
    size_t musicalNotes=0,switchNotes=0,controllerEvents=0;
};
// Input is a validated compiled Render and the exact destination mappings.
// Notes retain beat ticks; negative relative ticks describe an optional
// pre-roll BEFORE the musical origin, never a shift of the musical attacks.
inline bool planPrint(const Render& r,const std::vector<PrintMapping>& mappings,
                      ConductingValues controls,PrintPlan& output,std::string& error)
{
    auto fail=[&](const char* why){error=why;return false;};
    if(r.duration<=0||r.notes.size()>maxNotes||r.routes.size()>maxRows)return fail("Invalid compiled performance.");
    if(r.unassignedRows)return fail("Assign all active rows before printing.");
    if(controls.intensity<0||controls.intensity>127||controls.density<0||controls.density>127)return fail("Invalid conducting values.");
    PrintPlan next;next.duration=r.duration;
    std::map<std::tuple<int,int,int>,PrintMapping> resolved;
    std::set<std::tuple<int,int,int>> reserved;
    for(const auto& m:mappings)
    {
        if(m.route<0||size_t(m.route)>=r.routes.size()||m.articulation<1||size_t(m.articulation)>r.articulations.size()||
           m.channel<1||m.channel>16||m.number<0||m.number>127||m.value<0||m.value>127||(m.keyswitch&&m.value==0))
            return fail("Invalid articulation mapping.");
        if(!resolved.emplace(std::tuple(m.route,m.channel,m.articulation),m).second)return fail("Ambiguous articulation mapping.");
        if(m.keyswitch)reserved.emplace(m.route,m.channel,m.number);
    }
    std::map<std::pair<int,int>,std::vector<Note>> groups;
    for(const auto& n:r.notes)
    {
        if(n.route<0||size_t(n.route)>=r.routes.size()||n.channel<1||n.channel>16||n.pitch<0||n.pitch>127||
           n.velocity<1||n.velocity>127||n.start<0||n.end<=n.start||n.end>r.duration||n.articulation<0||
           size_t(n.articulation)>r.articulations.size()||n.densityThreshold<0||n.densityThreshold>127)
            return fail("Invalid compiled note.");
        if(n.densityThreshold>controls.density)continue;
        if(reserved.count({n.route,n.channel,n.pitch}))return fail("A musical pitch conflicts with a keyswitch.");
        if(n.articulation&&!resolved.count({n.route,n.channel,n.articulation}))return fail("Missing destination articulation mapping.");
        groups[{n.route,n.channel}].push_back(n);
    }
    for(auto& [address,notes]:groups)
    {
        PrintPart part;part.route=address.first;part.channel=address.second;
        std::sort(notes.begin(),notes.end(),[](const auto& a,const auto& b){return std::tie(a.start,a.end,a.pitch)<std::tie(b.start,b.end,b.pitch);});
        // A shared MIDI channel can have only one active articulation at a
        // time. Join overlapping/nearby notes using the same explicit mapping.
        Tick soundingEnd=-1;
        int soundingArt=0;
        struct Run{Tick start,end;int art;};
        std::vector<Run> runs;
        for(const auto& n:notes)
        {
            if(soundingEnd>n.start&&soundingArt!=n.articulation)return fail("Conflicting simultaneous channel articulations.");
            if(soundingEnd<=n.start){soundingEnd=n.end;soundingArt=n.articulation;}
            else soundingEnd=std::max(soundingEnd,n.end);
            part.notes.push_back({n.start,n.end,n.pitch,conductedVelocity(n.velocity,controls.intensity)});
            ++next.musicalNotes;
            if(!runs.empty()&&runs.back().art==n.articulation&&
               n.start<=runs.back().end+(n.articulation?printLead:0))
                runs.back().end=std::max(runs.back().end,n.end);
            else runs.push_back({n.start,n.end,n.articulation});
        }
        Tick previousEnd=-printLead;
        for(const auto& run:runs)
        {
            if(run.art)
            {
                const auto& m=resolved.at({part.route,part.channel,run.art});
                const Tick at=run.start-printLead;
                if(at<previousEnd)return fail("Articulation change needs a 1/16-beat gap before its next attack.");
                next.lead=std::max(next.lead,-at);
                if(m.keyswitch)
                {
                    // Hold the switch through the full sounding group, as
                    // stopped audition does; no overlap of different switches.
                    part.notes.push_back({at,run.end,m.number,m.value});++next.switchNotes;
                }
                else{part.controllers.push_back({at,m.number,m.value});++next.controllerEvents;}
            }
            previousEnd=run.end;
        }
        std::sort(part.notes.begin(),part.notes.end(),[](const auto& a,const auto& b){return std::tie(a.start,a.pitch,a.end)<std::tie(b.start,b.pitch,b.end);});
        next.parts.push_back(std::move(part));
    }
    if(next.musicalNotes==0)return fail("No assigned notes at this density to print.");
    output=std::move(next);error.clear();return true;
}
}
