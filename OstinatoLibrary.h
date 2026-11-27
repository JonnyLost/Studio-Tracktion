// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "OstinatoCore.h"
#include <initializer_list>

namespace studio::ostinato
{
// Track IDs and articulation mapping IDs are project-local. Never bind a
// portable preset by name or by an accidentally matching project ID.
inline constexpr size_t maxPresetBytes=4*1024*1024;
inline Pattern portablePattern(Pattern p)
{
    for(auto& r:p.rows)
    {
        r.trackId.clear();
        for(auto& s:r.steps)s.articulation.clear();
    }
    return p;
}
inline std::string encodePreset(const Pattern& p)
{
    if(!p.valid())return {};
    auto result=encode(portablePattern(p));
    return result.size()<=maxPresetBytes?result:std::string{};
}
// Reject malformed UTF-8 before it reaches native text controls.
inline bool presetUtf8(const std::string& text)
{
    for(size_t i=0;i<text.size();)
    {
        const auto first=static_cast<unsigned char>(text[i++]);
        if(first<0x80){if(first==0)return false;continue;}
        int count=0;unsigned value=0,min=0;
        if(first>=0xc2&&first<=0xdf){count=1;value=first&31u;min=0x80;}
        else if(first>=0xe0&&first<=0xef){count=2;value=first&15u;min=0x800;}
        else if(first>=0xf0&&first<=0xf4){count=3;value=first&7u;min=0x10000;}
        else return false;
        if(size_t(count)>text.size()-i)return false;
        while(count--)
        {
            const auto c=static_cast<unsigned char>(text[i++]);
            if((c&0xc0)!=0x80)return false;
            value=(value<<6)|(c&63u);
        }
        if(value<min||value>0x10ffff||(value>=0xd800&&value<=0xdfff))return false;
    }
    return true;
}
inline bool decodePreset(const std::string& bytes,Pattern& output)
{
    Pattern p;
    if(bytes.size()>maxPresetBytes||!presetUtf8(bytes)||!decode(bytes,p))return false;
    output=portablePattern(std::move(p));
    return true;
}
struct FactoryPattern
{
    std::string id,category,description;
    Pattern pattern;
};

// Original rhythm recipes, not transcriptions of scores or commercial presets.
// Each row is an instrument role; assign it to a track explicitly after load.
inline std::vector<FactoryPattern> factoryPatterns()
{
    std::vector<FactoryPattern> result;
    auto make=[&](const char* id,const char* category,const char* name,const char* description,int count=16,int division=4)->Pattern&
    {
        Pattern p;p.name=name;p.setGrid(count,division);
        p.rows.clear();result.push_back({id,category,description,std::move(p)});
        return result.back().pattern;
    };
    auto row=[](Pattern& p,const char* role,int pitch,PitchMode mode,std::initializer_list<int> cells,int velocity=85,int gate=500)->Row&
    {
        Row r;r.id=std::uint32_t(p.rows.size()+1);r.name=role;r.pitch=pitch;r.pitchMode=mode;
        r.steps.resize(size_t(p.stepCount));
        for(int index:cells)
        {
            auto& s=r.steps[size_t(index)];s.enabled=true;s.velocity=velocity;s.gate=gate;
            s.accent=index%p.stepsPerQuarter==0;
        }
        p.rows.push_back(std::move(r));return p.rows.back();
    };
    {
        auto& p=make("strings.motor","Strings","String motor","Driving sixteenths with a harmonic lower-string foundation.");
        auto& r=row(p,"Violas / violins",60,PitchMode::chordDegree,{0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15},77,420);
        for(size_t i=0;i<r.steps.size();++i)r.steps[i].interval=int(i%4==3?2:i%2);
        row(p,"Cellos / basses",36,PitchMode::followRoot,{0,4,8,12},95,620);
    }
    {
        auto& p=make("strings.pizz","Strings","Pizzicato clock","Light offbeat answers and a sparse bass.");
        row(p,"Pizzicato violins",72,PitchMode::chordDegree,{2,6,10,14},69,300);
        auto& r=row(p,"Pizzicato violas",60,PitchMode::chordDegree,{1,5,9,13},64,350);
        for(auto& s:r.steps)s.interval=1;
        row(p,"Pizzicato cellos",48,PitchMode::followRoot,{0,8},83,450);
    }
    {
        auto& p=make("strings.triplet","Strings","Triplet pursuit","Rolling compound subdivisions with anchored bass.",12,3);
        auto& r=row(p,"Short strings",60,PitchMode::arpUp,{0,1,2,3,4,5,6,7,8,9,10,11},85,480);
        for(size_t i=0;i<r.steps.size();++i)r.steps[i].densityThreshold=i%3==2?48:0;
        row(p,"Low strings",36,PitchMode::followRoot,{0,3,6,9},94,700);
    }
    {
        auto& p=make("strings.tension","Strings","Tension weave","Sparse low pulse growing into alternating upper-string figures.");
        p.variations.sequence="AABBACAD";p.variations.densityStart=0;p.variations.densityEnd=127;
        row(p,"Cellos",48,PitchMode::followRoot,{0,3,6,8,11,14},82,520);
        auto& r=row(p,"Violas",60,PitchMode::chordDegree,{2,5,7,10,13,15},71,480);
        for(size_t i=0;i<r.steps.size();++i){r.steps[i].interval=1;r.steps[i].densityThreshold=int(20+5*i);}
        auto& h=row(p,"Violins",72,PitchMode::arpBounce,{1,4,9,12},74,450);
        for(auto& s:h.steps)s.variationMask=14;
    }
    {
        auto& p=make("brass.call","Brass","Heroic calls","Staggered horn and trumpet calls with low-brass punctuation.");
        auto& r=row(p,"Horns",48,PitchMode::chordDegree,{0,6,12},104,900);
        r.steps[0].sustainCells=2;r.steps[6].interval=1;r.steps[12].interval=2;
        auto& t=row(p,"Trumpets",72,PitchMode::chordDegree,{4,10,14},92,550);
        for(auto& s:t.steps)s.interval=2;
        row(p,"Low brass",36,PitchMode::followRoot,{0,8},110,800);
    }
    {
        auto& p=make("brass.stabs","Brass","Syncopated stabs","Short ensemble attacks grouped 3+3+2 over two beats.");
        row(p,"Horns",48,PitchMode::followRoot,{0,3,6,8,11,14},99,400);
        auto& r=row(p,"Trombones",48,PitchMode::chordDegree,{0,6,8,14},95,350);
        for(auto& s:r.steps)s.interval=2;
    }
    {
        auto& p=make("winds.birds","Woodwinds","Woodwind flutter","Alternating ascending and descending light figures.");
        auto& r=row(p,"Flutes",72,PitchMode::scaleDegree,{0,1,3,4,8,9,11,12},66,420);
        for(size_t i=0;i<r.steps.size();++i){r.steps[i].interval=int(i%5);r.steps[i].chance=90;}
        auto& a=row(p,"Clarinets",60,PitchMode::scaleDegree,{2,5,6,7,10,13,14,15},62,500);
        for(size_t i=0;i<a.steps.size();++i)a.steps[i].interval=4-int(i%5);
    }
    {
        auto& p=make("winds.pastoral","Woodwinds","Pastoral six-eight","Gentle compound-meter accompaniment.",12,4);
        row(p,"Bassoons",48,PitchMode::followRoot,{0,6},68,850);
        auto& r=row(p,"Oboes",60,PitchMode::chordDegree,{0,3,6,9},73,750);
        r.steps[3].interval=1;r.steps[9].interval=2;
        row(p,"Flutes",72,PitchMode::arpBounce,{2,5,8,11},62,550);
    }
    {
        auto& p=make("perc.march","Percussion","Cinematic march","Low drums, snare and optional metal accents.");
        row(p,"Low drum",36,PitchMode::fixed,{0,4,8,12},106,400);
        auto& r=row(p,"Snare",38,PitchMode::fixed,{2,6,10,14,15},92,300);
        r.steps[15].strikes=3;r.steps[15].variationMask=16;
        p.variations.fillEvery=4;
        auto& a=row(p,"Metal accent",49,PitchMode::fixed,{0,8},79,650);
        for(auto& s:a.steps)s.densityThreshold=60;
    }
    {
        auto& p=make("perc.taiko","Percussion","Heavy drum dialogue","Alternating low and high drum attacks with periodic rolls.");
        row(p,"Low drum",36,PitchMode::fixed,{0,3,6,8,11,14},112,400);
        auto& r=row(p,"High drum",45,PitchMode::fixed,{2,5,7,10,13,15},90,350);
        r.steps[15].variationMask=16;r.steps[15].strikes=4;p.variations.fillEvery=2;
    }
    {
        auto& p=make("perc.cross","Percussion","Three against four","Independent three-quarter and four-quarter cycles.");
        row(p,"Low drum",36,PitchMode::fixed,{0,4,8,12},100,400);
        auto& r=row(p,"Wood block",76,PitchMode::fixed,{0,1,2,3,4,5},72,350);
        r.timing=TimingMode::independentCycle;r.numerator=3;r.denominator=4;
        r.steps.resize(6);
    }
    {
        auto& p=make("keys.harp","Harp & keys","Harp shimmer","Ascending chord ripples with a lower-register answer.");
        row(p,"Harp upper",72,PitchMode::arpUp,{0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15},62,950);
        auto& r=row(p,"Harp lower",48,PitchMode::chordDegree,{0,6,8,14},75,900);
        r.steps[6].interval=2;r.steps[14].interval=1;
    }
    {
        auto& p=make("keys.piano","Harp & keys","Prepared pulse","Sparse syncopated piano with octave accents.");
        row(p,"Piano low",36,PitchMode::followRoot,{0,3,8,11},86,400);
        auto& r=row(p,"Piano upper",60,PitchMode::chordDegree,{2,5,7,10,13,15},70,550);
        r.steps[7].interval=3;r.steps[15].interval=4;
        p.variations.timing=25;p.variations.velocity=5;
    }
    {
        auto& p=make("odd.five","Odd meters","Five-four pursuit","Five beats grouped 3+2, with eighth-note motion.",20,4);
        row(p,"Low strings",36,PitchMode::followRoot,{0,4,8,12,16},94,600);
        auto& r=row(p,"Upper strings",60,PitchMode::arpBounce,{0,2,4,6,8,10,12,14,16,18},80,450);
        r.steps[12].accent=true;
    }
    {
        auto& p=make("odd.seven","Odd meters","Seven-eight engine","Asymmetric 2+2+3 eighth-note grouping.",14,4);
        row(p,"Low strings",48,PitchMode::followRoot,{0,4,8},95,800);
        row(p,"Upper strings",60,PitchMode::arpUp,{0,2,4,6,8,10,12},80,500);
        row(p,"Frame drum",45,PitchMode::fixed,{0,3,4,7,8,10,12},88,350);
    }
    {
        auto& p=make("ensemble.rise","Ensemble","Orchestral escalation","Eight-cycle density build with variation and drum fills.");
        p.variations.sequence="AABBCCDD";p.variations.fillEvery=4;p.variations.densityStart=0;p.variations.densityEnd=127;
        row(p,"Low strings",36,PitchMode::followRoot,{0,4,8,12},88,700);
        auto& v=row(p,"Short strings",60,PitchMode::arpBounce,{0,2,3,4,6,7,8,10,11,12,14,15},75,500);
        for(size_t i=0;i<v.steps.size();++i)v.steps[i].densityThreshold=i%4==0?0:35;
        auto& h=row(p,"Horns",48,PitchMode::chordDegree,{0,8},105,950);
        for(auto& s:h.steps){s.densityThreshold=75;s.variationMask=12;}
        auto& d=row(p,"Low drum",36,PitchMode::fixed,{0,6,8,14,15},105,400);
        for(auto& s:d.steps)s.densityThreshold=48;
        d.steps[15].strikes=3;d.steps[15].variationMask=16;
    }
    return result;
}
}
