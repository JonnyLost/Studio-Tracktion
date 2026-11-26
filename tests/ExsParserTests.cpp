// SPDX-License-Identifier: GPL-3.0-or-later
#include "ExsFixtures.h"
#include <cassert>
#include <cstdio>
using namespace studio::exs;
int main()
{
    for(bool big:{false,true}) for(bool extended:{false,true}) for(bool modern:{false,true})
    {
        const auto d=Parser::parse(fixtures::make(big,extended,false,modern));
        assert(d.name=="EXS Fixture"&&d.regions.size()==2&&d.samples.size()==1&&d.groups.size()==1);
        const auto& r=d.regions[0];const auto& g=d.groups.at(3);const auto& s=d.samples.at(0);
        assert(r.root==60&&r.low==48&&r.high==72&&r.soft==20&&r.hard==120);
        assert(r.start==65536&&r.end==196608&&r.loopStart==70000&&r.loopEnd==100000&&r.crossfadeMs==5);
        assert(r.loop&&r.releaseTail&&r.equalPower&&r.fine==-25&&r.coarse==1&&r.gain==-3&&r.pan==10);
        assert(r.group==3&&r.source==0&&d.regions[1].unsupported);
        assert(g.condition==3&&g.controller==1&&g.ccLow==64&&g.ccHigh==127);
        assert(g.volume==-2&&g.pan==-5&&g.low==50&&g.high==70&&g.soft==32&&g.hard==100);
        assert(s.filename=="Long C4.wav"&&s.frames==196608&&s.rate==48000&&s.channels==2);
        assert(d.warnings.count("Instrument-wide EXS synthesis parameters not converted"));
    }
    auto reject=[](Bytes data,int limit=128)
    {
        bool rejected=false;try{Parser::parse(data,limit);}catch(const std::exception&){rejected=true;}assert(rejected);
    };
    reject(fixtures::make(false,false,true));
    reject(fixtures::make(),1);
    auto data=fixtures::make();data.pop_back();reject(data);
    data=fixtures::make();data[16]='X';reject(data);
    data=fixtures::make();fixtures::number(data,4,0xffffffffu,false);reject(data);
    std::puts("EXS: little/big endian, extended blocks, legacy/long names, groups, loops, bounds and malformed links passed.");
}
