// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstddef>
#include <utility>
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
    int interval=0; // semitones from the row's configured root
    int densityThreshold=0; // 0 always plays, 1–127 optional at corresponding density
    int sustainCells=1,strikes=1,offset=0,chance=100; // offset in thousandths of the first cell
    std::string articulation{}; // exact saved mapping ID; no inferred vendor mapping
    bool operator==(const Step&) const=default;
};
enum class PitchMode { fixed=0, intervals=1, followRoot=2, scaleDegree=3, chordDegree=4, arpUp=5, arpDown=6, arpBounce=7 };
enum class Scale { major=0, naturalMinor, harmonicMinor, dorian, mixolydian, pentatonicMajor, chromatic };
struct Harmony
{
    int root=0; // pitch class, C=0
    Scale scale=Scale::major;
    unsigned chord=0x091; // relative pitch classes C E G
    bool valid()const{return root>=0&&root<12&&int(scale)>=0&&int(scale)<=6&&chord>0&&chord<4096&&(chord&1);}
    bool operator==(const Harmony&)const=default;
};
struct PitchRecipe
{
    PitchMode mode=PitchMode::fixed;
    int anchor=60,degree=0;
    bool operator==(const PitchRecipe&)const=default;
};
inline int positiveMod(int x,int n){return (x%n+n)%n;}
inline int degreeOffset(unsigned mask,int degree)
{
    std::array<int,12> tones{};int count=0;
    for(int i=0;i<12;++i)if(mask&(1u<<i))tones[size_t(count++)]=i;
    if(!count)return 0;
    const int index=positiveMod(degree,count);
    return tones[size_t(index)]+12*((degree-index)/count);
}
inline unsigned scaleMask(Scale s)
{
    switch(s)
    {
        case Scale::major:return 0xab5;
        case Scale::naturalMinor:return 0x5ad;
        case Scale::harmonicMinor:return 0x9ad;
        case Scale::dorian:return 0x6ad;
        case Scale::mixolydian:return 0x6b5;
        case Scale::pentatonicMajor:return 0x295;
        case Scale::chromatic:return 0xfff;
    }
    return 0xab5;
}
inline int resolvePitch(PitchRecipe r,const Harmony& h)
{
    if(r.mode==PitchMode::fixed)return r.anchor;
    if(r.mode==PitchMode::intervals)return r.anchor+r.degree;
    // Nearest root to the authored register; tritone ties choose downward.
    const int root=r.anchor+positiveMod(h.root-r.anchor+6,12)-6;
    if(r.mode==PitchMode::followRoot)return root+r.degree;
    if(r.mode==PitchMode::scaleDegree)return root+degreeOffset(scaleMask(h.scale),r.degree);
    if(r.mode==PitchMode::chordDegree)return root+degreeOffset(h.chord,r.degree);
    int count=0;for(int i=0;i<12;++i)if(h.chord&(1u<<i))++count;
    if(count<1)return -1;
    if(r.mode==PitchMode::arpUp)return root+degreeOffset(h.chord,positiveMod(r.degree,count));
    if(r.mode==PitchMode::arpDown)return root+degreeOffset(h.chord,count-1-positiveMod(r.degree,count));
    const int period=std::max(1,2*(count-1)),position=positiveMod(r.degree,period);
    return root+degreeOffset(h.chord,std::min(position,period-position));
}
inline Harmony harmonyFromKeys(const std::array<bool,128>& keys,Harmony previous)
{
    int bass=-1;unsigned mask=0;
    for(int i=0;i<128;++i)if(keys[size_t(i)]){if(bass<0)bass=i;mask|=1u<<positiveMod(i-bass,12);}
    if(bass>=0){previous.root=bass%12;previous.chord=mask;}
    return previous;
}
enum class TimingMode { followGrid=0, sharedSpan=1, independentCycle=2 };
struct Row
{
    std::uint32_t id=1;
    std::string name="Row 1",trackId; // opaque, stable project track identity
    int channel=1,pitch=60;
    PitchMode pitchMode=PitchMode::fixed;
    bool mute=false,solo=false;
    TimingMode timing=TimingMode::followGrid;
    // For independent cycles: one cycle = bars * numerator / denominator
    // whole notes. Shared-span rows ignore these canonical default fields.
    int numerator=4,denominator=4,bars=1;
    Tick cycleTicks() const {return meterValid(numerator,denominator,bars)?Tick(bars)*numerator*(ticksPerQuarter*4)/denominator:0;}
    static bool meterValid(int n,int d,int b)
    {
        return n>=1&&n<=32&&b>=1&&b<=16&&(d==1||d==2||d==4||d==8||d==16||d==32);
    }
    std::vector<Step> steps=std::vector<Step>(16);
    bool operator==(const Row&) const=default;
};
struct Pattern
{
    std::string name="New ostinato";
    int stepCount=16,stepsPerQuarter=4;
    std::vector<Row> rows{Row{}};
    Harmony harmony;
    bool operator==(const Pattern&) const=default;
    Tick stepTicks() const {return stepsPerQuarter>0?ticksPerQuarter/stepsPerQuarter:0;}
    Tick duration() const {return stepTicks()*stepCount;}
    static bool textValid(const std::string& s,size_t max,bool empty=false)
    {
        return (empty||!s.empty())&&s.size()<=max&&std::none_of(s.begin(),s.end(),[](unsigned char c){return c<32||c==127;});
    }
    bool valid() const
    {
        if(!harmony.valid()||!textValid(name,160)||stepCount<1||stepCount>maxSteps||stepsPerQuarter<1||stepsPerQuarter>32
            ||ticksPerQuarter%stepsPerQuarter||rows.empty()||rows.size()>maxRows)return false;
        std::set<std::uint32_t> ids;
        for(const auto& r:rows)
        {
            if(!r.id||!ids.insert(r.id).second||!textValid(r.name,80)||!textValid(r.trackId,256,true)
               ||r.channel<1||r.channel>16||r.pitch<0||r.pitch>127||r.steps.empty()||r.steps.size()>maxSteps
               ||!Row::meterValid(r.numerator,r.denominator,r.bars))return false;
            if(r.timing==TimingMode::followGrid&&r.steps.size()!=size_t(stepCount))return false;
            if(r.timing!=TimingMode::followGrid&&r.timing!=TimingMode::sharedSpan&&r.timing!=TimingMode::independentCycle)return false;
            if(r.timing!=TimingMode::independentCycle&&(r.numerator!=4||r.denominator!=4||r.bars!=1))return false;
            if(int(r.pitchMode)<0||int(r.pitchMode)>7)return false;
            for(size_t i=0;i<r.steps.size();++i)
            {
                const auto& s=r.steps[i];
                if(s.sustainCells<1||s.sustainCells>16||s.strikes<1||s.strikes>8||s.offset<-450||s.offset>450
                    ||s.chance<0||s.chance>100||!textValid(s.articulation,128,true))return false;
                if(s.enabled&&s.sustainCells>1)
                {
                    if(s.strikes!=1||i+size_t(s.sustainCells)>r.steps.size())return false;
                    for(size_t j=i+1;j<i+size_t(s.sustainCells);++j)if(r.steps[j].enabled)return false;
                }
                if(s.velocity<1||s.velocity>127||s.gate<1||s.gate>1000||s.interval<-127||s.interval>127||s.densityThreshold<0||s.densityThreshold>127
                   ||(r.pitchMode==PitchMode::intervals&&s.enabled&&(r.pitch+s.interval<0||r.pitch+s.interval>127)))return false;
            }
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
        for(const auto& r:rows)if(r.timing==TimingMode::followGrid)
            for(size_t i=size_t(count);i<r.steps.size();++i)if(r.steps[i].enabled)return false;
        for(auto& r:rows)if(r.timing==TimingMode::followGrid)r.steps.resize(size_t(count));
        stepCount=count;stepsPerQuarter=division;return true;
    }
    // Row resizing preserves the existing cell order and rejects data loss.
    bool setRowTiming(std::uint32_t id,TimingMode mode,int count,int n=4,int d=4,int b=1)
    {
        if(!valid()||count<1||count>maxSteps||!Row::meterValid(n,d,b))return false;
        if(mode!=TimingMode::followGrid&&mode!=TimingMode::sharedSpan&&mode!=TimingMode::independentCycle)return false;
        if(mode==TimingMode::followGrid&&count!=stepCount)return false;
        if(mode!=TimingMode::independentCycle&&(n!=4||d!=4||b!=1))return false;
        auto it=std::find_if(rows.begin(),rows.end(),[&](const auto& r){return r.id==id;});
        if(it==rows.end())return false;
        for(size_t i=size_t(count);i<it->steps.size();++i)if(it->steps[i].enabled)return false;
        it->steps.resize(size_t(count));it->timing=mode;it->numerator=n;it->denominator=d;it->bars=b;return true;
    }
    // Explicit master meter convenience. Master grid retains exact O1 divisions.
    bool setMasterMeter(int n,int d,int bars,int subdivision)
    {
        if(!Row::meterValid(n,d,bars)||subdivision<1||subdivision>32)return false;
        const int cells=bars*n*4*subdivision;
        return cells%d==0&&setGrid(cells/d,subdivision);
    }
    Tick rowCycle(const Row& r) const
    {
        return r.timing==TimingMode::independentCycle?r.cycleTicks():duration();
    }
    // Shared geometry for the future matrix UI and the compiler; cell may
    // equal the count to obtain the exact right edge of the cycle.
    Tick rowBoundary(const Row& r,size_t cell) const
    {
        if(r.steps.empty()||r.steps.size()>maxSteps||cell>r.steps.size())return 0;
        return rowCycle(r)*static_cast<Tick>(cell)/static_cast<Tick>(r.steps.size());
    }
};

// Versioned portable pattern payload. Parse transactionally on the message/worker
// thread. Project embedding and named user library are later milestones.
inline std::string encode(const Pattern& p)
{
    if(!p.valid())return {};
    std::ostringstream out;out.imbue(std::locale::classic());
    out<<"STUDIO_OSTINATO 6 "<<std::quoted(p.name)<<' '<<p.stepCount<<' '<<p.stepsPerQuarter<<' '<<p.rows.size()<<' '<<p.harmony.root<<' '<<int(p.harmony.scale)<<' '<<p.harmony.chord<<'\n';
    for(const auto& r:p.rows)
    {
        out<<r.id<<' '<<std::quoted(r.name)<<' '<<std::quoted(r.trackId)<<' '<<r.channel<<' '<<r.pitch<<' '<<int(r.mute)<<' '<<int(r.solo)<<' '<<int(r.timing)<<' '<<r.numerator<<' '<<r.denominator<<' '<<r.bars<<' '<<r.steps.size()<<' '<<int(r.pitchMode)<<'\n';
        for(const auto& s:r.steps)out<<int(s.enabled)<<' '<<int(s.accent)<<' '<<s.velocity<<' '<<s.gate<<' '<<s.interval<<' '<<s.densityThreshold<<' '<<s.sustainCells<<' '<<s.strikes<<' '<<s.offset<<' '<<s.chance<<' '<<std::quoted(s.articulation)<<'\n';
    }
    return out.str();
}
inline bool decode(const std::string& text,Pattern& output)
{
    if(text.empty()||text.size()>4*1024*1024)return false;
    std::istringstream in(text);in.imbue(std::locale::classic());
    std::string magic;int version=0,count=0;
    Pattern p;
    if(!(in>>magic>>version>>std::quoted(p.name)>>p.stepCount>>p.stepsPerQuarter>>count)
        ||magic!="STUDIO_OSTINATO"||(version<1||version>6)||count<1||count>maxRows||p.stepCount<1||p.stepCount>maxSteps)return false;
    if(version>=6){int scale=-1;if(!(in>>p.harmony.root>>scale>>p.harmony.chord)||scale<0||scale>6)return false;p.harmony.scale=static_cast<Scale>(scale);if(!p.harmony.valid())return false;}
    p.rows.clear();
    auto flag=[&](bool& result){int i=-1;if(!(in>>i)||i<0||i>1)return false;result=i==1;return true;};
    for(int i=0;i<count;++i)
    {
        Row r;
        std::int64_t id=0;
        if(!(in>>id>>std::quoted(r.name)>>std::quoted(r.trackId)>>r.channel>>r.pitch)
            ||id<1||id>UINT32_MAX||!flag(r.mute)||!flag(r.solo))return false;
        r.id=static_cast<std::uint32_t>(id);
        int cells=p.stepCount;
        if(version>=2)
        {
            int mode=-1;
            if(!(in>>mode>>r.numerator>>r.denominator>>r.bars>>cells)||mode<0||mode>2||cells<1||cells>maxSteps)return false;
            r.timing=static_cast<TimingMode>(mode);
            if(version>=3){int pitchMode=-1;if(!(in>>pitchMode)||pitchMode<0||pitchMode>(version>=6?7:1))return false;r.pitchMode=static_cast<PitchMode>(pitchMode);}
        }
        r.steps.resize(size_t(cells));
        for(auto& s:r.steps)if(!flag(s.enabled)||!flag(s.accent)||!(in>>s.velocity>>s.gate)||(version>=3&&!(in>>s.interval))||(version>=4&&!(in>>s.densityThreshold))||(version>=5&&!(in>>s.sustainCells>>s.strikes>>s.offset>>s.chance>>std::quoted(s.articulation))))return false;
        p.rows.push_back(std::move(r));
    }
    in>>std::ws;if(!in.eof()||!p.valid())return false;
    output=std::move(p);return true;
}
struct Note
{
    Tick start=0,end=0;
    int route=0,channel=1,pitch=60,velocity=90;
    int densityThreshold=0;
    int articulation=0;
    PitchRecipe recipe{};
    bool operator==(const Note&) const=default;
};
struct Event
{
    Tick tick=0;
    int route=0,channel=1,pitch=60,velocity=0;
    bool on=false;
    int densityThreshold=0;
    int articulation=0;
    PitchRecipe recipe{};
    bool operator==(const Event&) const=default;
};
struct Render
{
    Tick duration=0;
    std::vector<std::string> routes;
    std::vector<Note> notes;
    std::vector<Event> events;
    std::vector<std::string> articulations; // event index 1-based, zero = leave articulation unchanged
    int unassignedRows=0;
};
// Compile off the audio thread. No routing fallback: absent destinations fail
// closed. Unassigned rows are silent and counted. Resolve collisions on the
// same physical route/channel/pitch; simultaneous hits merge, later hits
// retrigger after the earlier note is released.
// Stable integer hash: repeatable probability across machines and render partitions.
inline unsigned chanceRoll(std::uint32_t row,Tick cycle,size_t step)
{
    std::uint64_t x=(std::uint64_t(row)<<32)^std::uint64_t(cycle)^((std::uint64_t(step)+1)*0x9e3779b97f4a7c15ULL);
    x^=x>>30;x*=0xbf58476d1ce4e5b9ULL;x^=x>>27;x*=0x94d049bb133111ebULL;x^=x>>31;
    return unsigned(x%100);
}
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
        const Tick span=p.rowCycle(row);
        // Absolute rational boundaries: round down only once per boundary,
        // never add a rounded subdivision repeatedly. Every cycle ends exactly.
        // Precollect active indices so long silent/mostly silent cycles are cheap.
        std::vector<size_t> activeSteps;
        for(size_t i=0;i<row.steps.size();++i)if(row.steps[i].enabled)activeSteps.push_back(i);
        if(activeSteps.empty())continue;
        for(Tick origin=0;origin<next.duration;origin+=span)
            for(size_t index:activeSteps)
            {
                const auto& s=row.steps[index];
                const Tick offset=p.rowBoundary(row,index);
                const Tick nextOffset=p.rowBoundary(row,index+1);
                if(s.chance==0||(s.chance<100&&chanceRoll(row.id,origin/span,index)>=unsigned(s.chance)))continue;
                const Tick cellWidth=nextOffset-offset;
                const Tick shift=cellWidth*s.offset/1000;
                const Tick start=origin+std::clamp<Tick>(offset+shift,0,span-1);
                if(start>=next.duration)continue;
                int art=0;
                if(!s.articulation.empty())
                {
                    auto found=std::find(next.articulations.begin(),next.articulations.end(),s.articulation);
                    art=int(found-next.articulations.begin())+1;
                    if(found==next.articulations.end())next.articulations.push_back(s.articulation);
                }
                const Tick width=p.rowBoundary(row,index+size_t(s.sustainCells))-offset;
                if(width<s.strikes)return fail("Too many strikes for this timing resolution.");
                for(int hit=0;hit<s.strikes;++hit)
                {
                    const Tick hitStart=start+width*hit/s.strikes;
                    if(hitStart>=next.duration||hitStart>=origin+span)break;
                    const Tick hitWidth=width*(hit+1)/s.strikes-width*hit/s.strikes;
                    // A tie sustains through complete continuation cells; gate
                    // applies only to the final cell. Ratchets use per-hit gate.
                    const Tick tail=s.sustainCells>1?p.rowBoundary(row,index+size_t(s.sustainCells))-p.rowBoundary(row,index+size_t(s.sustainCells)-1):hitWidth;
                    const Tick length=(s.sustainCells>1?width-tail:0)+std::max<Tick>(1,tail*s.gate/1000);
                    if(next.notes.size()>=maxNotes)return fail("Pattern exceeds bounded render size.");
                    PitchRecipe recipe{row.pitchMode,row.pitch,s.interval};
                    if(row.pitchMode==PitchMode::arpUp||row.pitchMode==PitchMode::arpDown||row.pitchMode==PitchMode::arpBounce)
                        recipe.degree=int(index)+s.interval;
                    const int pitch=resolvePitch(recipe,p.harmony);
                    if(pitch<0||pitch>127)return fail("Harmonic pitch is out of MIDI range.");
                    if(int(row.pitchMode)<2)recipe={}; // legacy events retain default recipe
                    next.notes.push_back({hitStart,std::min({hitStart+length,next.duration,origin+span}),route,row.channel,pitch,std::min(127,s.velocity+(s.accent?20:0)),s.densityThreshold,art,recipe});
                }
            }
    }
    auto address=[](const Note& n){return std::tuple(n.route,n.channel,n.pitch);};
    std::sort(next.notes.begin(),next.notes.end(),[&](const auto& a,const auto& b){
        return std::tuple(address(a),a.start,a.end,a.velocity)<std::tuple(address(b),b.start,b.end,b.velocity);
    });
    // Different density tiers cannot share overlapping pitches: otherwise
    // filtering an optional hit could shorten or steal a required note.
    // Track the maximum end per threshold for each physical MIDI address.
    std::array<Tick,128> tierEnds{};
    Tick addressEnd=-1;int addressArt=0;
    const Note* last=nullptr;
    for(const auto& n:next.notes)
    {
        if(!last||address(*last)!=address(n)){tierEnds.fill(-1);addressEnd=-1;}
        if(addressEnd>n.start&&addressArt!=n.articulation)return fail("Overlapping same-pitch notes request different articulations.");
        if(addressEnd<=n.start){addressArt=n.articulation;addressEnd=n.end;}else addressEnd=std::max(addressEnd,n.end);
        for(int tier=0;tier<128;++tier)
            if(tier!=n.densityThreshold&&tierEnds[size_t(tier)]>n.start)
                return fail("Overlapping same-pitch notes use different density thresholds. Separate pitch, channel, timing or threshold.");
        auto& end=tierEnds[size_t(n.densityThreshold)];end=std::max(end,n.end);last=&n;
    }
    std::vector<Note> normalized;normalized.reserve(next.notes.size());
    for(const auto& n:next.notes)
    {
        if(!normalized.empty()&&address(normalized.back())==address(n))
        {
            auto& prior=normalized.back();
            if(prior.start==n.start){if(prior.recipe!=n.recipe)return fail("Simultaneous reference pitches use different harmonic recipes.");if(prior.articulation!=n.articulation)return fail("Simultaneous same-pitch notes request different articulations.");prior.end=std::max(prior.end,n.end);prior.velocity=std::max(prior.velocity,n.velocity);continue;}
            if(prior.end>n.start&&(int(prior.recipe.mode)>=2||int(n.recipe.mode)>=2)&&prior.recipe!=n.recipe)return fail("Overlapping reference pitches use different harmonic recipes.");
            prior.end=std::min(prior.end,n.start);
        }
        normalized.push_back(n);
    }
    next.notes=std::move(normalized);
    std::sort(next.notes.begin(),next.notes.end(),[&](const auto& a,const auto& b){return std::tuple(a.start,address(a))<std::tuple(b.start,address(b));});
    // Articulation state is shared by a MIDI channel. An explicit change
    // may not cut across a different sounding technique on that channel.
    std::map<std::pair<int,int>,std::pair<Tick,int>> sounding;
    for(const auto& n:next.notes)
    {
        auto& lastArt=sounding[{n.route,n.channel}];
        if(lastArt.first>n.start&&lastArt.second!=n.articulation)
            return fail("Overlapping notes request different articulations on one instrument channel.");
        if(lastArt.first<=n.start)lastArt={n.end,n.articulation};
        else lastArt.first=std::max(lastArt.first,n.end);
    }
    next.events.reserve(next.notes.size()*2);
    for(const auto& n:next.notes)
    {
        next.events.push_back({n.start,n.route,n.channel,n.pitch,n.velocity,true,n.densityThreshold,n.articulation,n.recipe});
        next.events.push_back({n.end,n.route,n.channel,n.pitch,0,false,n.densityThreshold,n.articulation,n.recipe});
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
    Cursor(Render&&)=delete;Cursor(const Render&&)=delete;
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
