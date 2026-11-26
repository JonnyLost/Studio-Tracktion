// SPDX-License-Identifier: GPL-3.0-or-later
#include "SoundBankFixtures.h"
#include <cassert>
#include <chrono>
#include <cstdio>
using namespace studio::soundbank;
int main()
{
    const auto dir=std::filesystem::temp_directory_path()/("studio-bank-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(dir);
    struct Cleanup{std::filesystem::path p;~Cleanup(){std::error_code ec;std::filesystem::remove_all(p,ec);}} cleanup{dir};
    const auto sf=dir/"test.sf2", sf24=dir/"24bit.sf2", sf3=dir/"test.sf3", dls=dir/"test.dls", bad=dir/"bad.sf2";
    fixtures::write(sf,fixtures::sf());fixtures::write(sf24,fixtures::sf(false,true));fixtures::write(sf3,fixtures::sf(true));fixtures::write(dls,fixtures::dls());
    Bank a(sf);
    assert(a.programs.size()==2&&a.programs[0].name=="Warm strings"&&a.programs[0].number==40);
    assert(a.programs[1].bank==128&&a.programs[1].drums);
    auto m=a.resolve(0);
    assert(m.regions.size()==1);
    const auto& r=m.regions[0];
    assert(r.low==64&&r.high==80&&r.soft==32&&r.hard==127);
    assert(r.root==60&&r.cents==5&&r.gainDb==-5&&r.pan==-20);
    assert(std::abs(r.attack-0.5f)<1e-6f&&r.loop&&r.sustainLoop&&r.loopStart==1024&&r.loopEnd==16384);
    assert(r.audio.frames==65536&&r.audio.encoding==Encoding::pcm16);
    assert(s16(a.read(r.audio.offset,2),0)==-16384);
    assert(a.resolve(1).regions[0].low==48);
    Bank b(sf24);const auto ext=b.resolve(0).regions[0];
    assert(ext.audio.encoding==Encoding::sf24&&b.read(ext.audio.lowBytes,1)[0]==64);
    Bank c(sf3);const auto comp=c.resolve(0).regions[0];
    assert(comp.audio.encoding==Encoding::vorbis&&comp.audio.bytes==64&&comp.audio.frames==0&&comp.loopEnd==16384);
    Bank d(dls);assert(d.isDls&&d.programs.size()==1&&d.programs[0].drums&&d.programs[0].bank==130&&d.programs[0].number==10);
    auto dm=d.resolve(0);const auto& dr=dm.regions[0];
    assert(dr.low==48&&dr.high==72&&dr.soft==10&&dr.hard==110&&dr.root==62&&dr.cents==25);
    assert(dr.loop&&dr.loopStart==1024&&dr.loopEnd==9216&&dr.audio.frames==65536);
    fixtures::write(bad,fixtures::sf(false,false,true));
    bool failed=false;try { Bank invalid(bad);invalid.resolve(0); }catch(const std::exception&){failed=true;}assert(failed);
    fixtures::write(bad,fixtures::dls(true));
    failed=false;try { Bank invalid(bad);invalid.resolve(0); }catch(const std::exception&){failed=true;}assert(failed);
    fixtures::write(bad,Bytes{'R','I','F','F',255,255,255,255,'s','f','b','k'});
    failed=false;try { Bank invalid(bad); }catch(const std::exception&){failed=true;}assert(failed);
    failed=false;try { a.resolve(999); }catch(const std::exception&){failed=true;}assert(failed);
    std::puts("Sound banks: catalogs, SF2 inheritance/ranges, SF24, SF3 metadata, DLS wave links, loops and invalid offsets passed.");
}
