// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cstdint>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace studio::exs
{
// Binary EXS24 field layout cross-checked against public reverse-engineering
// references (see docs/exs24-importer.md). No third-party parser dependency.
using Bytes = std::vector<unsigned char>;
struct Source
{
    std::string name, directory, filename;
    int frames=0, rate=0, channels=0;
};
struct Group
{
    std::string name;
    int volume=0, pan=0, soft=1, hard=127, low=0, high=127;
    int condition=0, key=0, controller=0, ccLow=0, ccHigh=127, channel=0, sequence=-1;
    bool mute=false, release=false, unsupported=false;
};
struct Region
{
    std::string name;
    int source=-1, group=-1, root=60, low=0, high=127, soft=1, hard=127;
    int start=0, end=0, loopStart=0, loopEnd=0, crossfadeMs=0, fine=0, coarse=0, gain=0, pan=0;
    bool pitch=true, oneShot=false, loop=false, releaseTail=false, equalPower=false, unsupported=false;
};
struct Document
{
    std::string name;
    std::map<int,Source> samples;
    std::map<int,Group> groups;
    std::vector<Region> regions;
    std::set<std::string> warnings;
};
class Parser
{
public:
    static Document parse(const Bytes& data,int limit=128)
    {
        if(data.size()<84 || data.size()>32u*1024u*1024u || limit<1) fail("EXS file size or region limit invalid");
        Document out;
        bool instrument=false;
        int blocks=0;
        std::set<int> ids;
        for(std::size_t p=0;p<data.size();)
        {
            if(++blocks>65536 || data.size()-p<84) fail("Truncated or excessive EXS blocks");
            const auto magic=std::string(data.begin()+p+16,data.begin()+p+20);
            const bool big=magic=="SOBT"||magic=="SOBJ";
            if(!big && magic!="TBOS" && magic!="JBOS") fail("Unsupported EXS block magic");
            // The marker is a version word with the object type in the high
            // byte on little endian files, or the final byte on legacy files.
            const auto a=data[p],b=data[p+1],c=data[p+2],d=data[p+3];
            if(!((!big && a==1 && b==1 && c==0) ||
                 (big && ((a==0&&b==1&&c==0)||(a==1&&b==1&&c==0)))))
                fail("Unrecognized EXS binary marker");
            const int type=d&15;
            if(d&0x30) fail("Unknown EXS block flags");
            auto length=u32(data,p+4,big);
            if(d&0x80) length &= 0x7fffu;
            if(length>data.size()-p-84) fail("EXS block exceeds file length");
            const int id=signed32(u32(data,p+8,big));
            const std::string name=string(data,p+20,64);
            const auto begin=p+84;
            const Bytes body(data.begin()+begin,data.begin()+begin+length);
            const auto field=[&](std::size_t pos){return u32(body,pos,big);};
            const auto require=[&](std::size_t size){if(body.size()<size)fail("Truncated EXS object");};
            if(type==0)
            {
                if(instrument||p!=0) fail("Invalid EXS instrument header order");
                instrument=true;out.name=name;
            }
            else if(!instrument) fail("Missing EXS instrument header");
            else if(type==1)
            {
                require(96);
                if(id<0||!ids.insert(id).second)fail("Duplicate/invalid EXS zone ID");
                if(static_cast<int>(out.regions.size())>=limit)fail("EXS exceeds Studio zone limit; no partial import");
                Region r;
                r.name=name;r.root=body[1];r.fine=signed8(body[2]);r.pan=signed8(body[3]);r.gain=signed8(body[4]);
                r.low=body[6];r.high=body[7];
                const auto flags=body[0],loopFlags=body[33];
                r.pitch=(flags&2)==0;r.oneShot=(flags&1)!=0;r.loop=(loopFlags&1)!=0;
                r.releaseTail=(loopFlags&4)!=0;r.equalPower=(loopFlags&2)!=0;
                if(flags&8){r.soft=std::max(1,int(body[9]));r.hard=body[10];}
                r.start=frame(field(12));r.end=frame(field(16));
                if(r.loop)
                {
                    r.loopStart=frame(field(20));r.loopEnd=frame(field(24));r.crossfadeMs=frame(field(28));
                    if(r.loopEnd-r.loopStart<2||r.loopStart<r.start||(r.end&&r.loopEnd>r.end))
                        fail("Invalid EXS forward loop bounds");
                }
                r.coarse=signed8(body[80]);r.group=signed32(field(88));r.source=signed32(field(92));
                if(r.root>127||r.low>r.high||r.high>127||r.soft>r.hard||r.hard>127||
                   (r.end&&r.start>=r.end)||r.group< -1||r.source< -1)
                    fail("Invalid EXS zone mapping");
                if((flags&4)||(r.loop&&body[34]!=0)||(r.oneShot&&r.loop))
                {r.unsupported=true;out.warnings.insert("Reverse, non-forward or one-shot loop region omitted");}
                if(flags&64)out.warnings.insert("Zone output assignment not converted");
                if(body[77]!=0)out.warnings.insert("Flex playback parameters not converted");
                if(r.loop&&(body[32]!=0||body[79]!=0))out.warnings.insert("Loop or tail tuning not converted");
                if(r.equalPower&&r.crossfadeMs)out.warnings.insert("Equal-power loop crossfade approximated by native linear fade");
                if(body.size()>96)out.warnings.insert("Extended zone parameters not fully converted");
                out.regions.push_back(std::move(r));
            }
            else if(type==2)
            {
                require(76);
                if(id<0||out.groups.count(id))fail("Duplicate/invalid EXS group ID");
                Group g;g.name=name;g.volume=signed8(body[0]);g.pan=signed8(body[1]);
                g.mute=(body[3]&16)!=0;g.release=body[73]!=0;
                g.soft=std::max(1,int(body[5]));g.hard=body[6];
                if(g.soft>g.hard||g.hard>127)fail("Invalid EXS group velocity range");
                if(body[2]!=0)out.warnings.insert("Group polyphony not converted");
                if(body[4]!=0)out.warnings.insert("Exclusive/choke groups not converted");
                if(body[7]!=0||(body[3]&128))
                {g.unsupported=true;out.warnings.insert("Random/fixed sample selection group omitted");}
                if(g.release&&(body[3]&64))out.warnings.insert("Release-trigger decay approximated by native release playback");
                if(body[32]!=128||body[35]!=128)out.warnings.insert("Group velocity/key crossfades not converted");
                g.key=body[75];
                if(body.size()>76)
                {
                    require(92);
                    g.sequence=signed32(field(80));g.condition=body[84];
                    g.controller=body[85];g.ccLow=body[86];g.ccHigh=body[87];
                    g.low=body[88];g.high=body[89];g.channel=body[90];
                    // Zero/zero is an unset range in older group records.
                    if(g.low==0&&g.high==0)g.high=127;
                    if(g.low>g.high||g.high>127)fail("Invalid EXS group key range");
                }
                if(g.condition==1)
                {if(g.key>127)fail("Invalid EXS switch key");out.warnings.insert("Note-enabled groups use Studio's latched keyswitch behavior");}
                else if(g.condition==3)
                {if(g.controller>=120||g.ccLow>g.ccHigh||g.ccHigh>127)fail("Invalid EXS controller range");}
                else if(g.condition==5)
                {if(g.channel<1||g.channel>16)fail("Invalid EXS MIDI channel");}
                else if(g.condition!=0&&g.condition!=2)
                {g.unsupported=true;out.warnings.insert("Unsupported bend, tempo or articulation-ID group omitted");}
                if(g.condition==2 && g.sequence < -1)fail("Invalid EXS round-robin index");
                out.groups.emplace(id,std::move(g));
            }
            else if(type==3)
            {
                require(336);
                if(id<0||out.samples.count(id))fail("Duplicate/invalid EXS sample ID");
                Source s;s.name=name;s.frames=frame(field(4));s.rate=frame(field(8));s.channels=frame(field(16));
                s.directory=string(body,80,256);
                if(body.size()>=592)s.filename=string(body,336,256);
                if(s.filename.empty())s.filename=name;
                if(s.filename.empty())fail("EXS sample has no filename");
                out.samples.emplace(id,std::move(s));
            }
            else if(type==4)out.warnings.insert("Instrument-wide EXS synthesis parameters not converted");
            else out.warnings.insert("Unrecognized or extended EXS block type "+std::to_string(type)+" not converted");
            p=begin+length;
        }
        if(!instrument||out.regions.empty()||out.samples.empty())fail("No supported EXS binary zones/sample references");
        // Sample references in standard files are zero-based indices. Accept
        // explicit object IDs only; ambiguous/missing links cannot be guessed.
        for(const auto& r:out.regions)
        {
            if(r.source>=0&&!out.samples.count(r.source))fail("EXS zone references missing sample object");
            if(r.group>=0&&!out.groups.count(r.group))fail("EXS zone references missing group object");
        }
        return out;
    }
private:
    [[noreturn]] static void fail(const char* message){throw std::runtime_error(message);}
    static std::uint32_t u32(const Bytes& b,std::size_t p,bool big)
    {
        if(p>b.size()||b.size()-p<4)fail("Truncated EXS field");
        std::uint32_t value=0;
        for(int j=0;j<4;++j)value=(value<<8)|b[p+(big?j:3-j)];
        return value;
    }
    static int frame(std::uint32_t v)
    {if(v>=std::uint32_t(std::numeric_limits<int>::max()))fail("EXS frame value exceeds Studio limit");return int(v);}
    static int signed8(unsigned char v){return v<128?int(v):int(v)-256;}
    static int signed32(std::uint32_t v)
    {return v<0x80000000u?int(v):static_cast<int>(std::int64_t(v)-0x100000000ll);}
    static std::string string(const Bytes& b,std::size_t pos,std::size_t size)
    {
        if(pos>b.size()||size>b.size()-pos)fail("Truncated EXS string");
        std::string s(b.begin()+pos,b.begin()+pos+size);
        if(auto n=s.find('\0');n!=std::string::npos)s.resize(n);
        return s;
    }
};
}
