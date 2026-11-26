// SPDX-License-Identifier: GPL-3.0-or-later
#include "LegacyBankFixtures.h"
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
    assert(r.audio.frames==196608&&r.audio.encoding==Encoding::pcm16);
    assert(s16(a.read(r.audio.offset,2),0)==-16384);
    assert(a.resolve(1).regions[0].low==48);
    Bank b(sf24);const auto ext=b.resolve(0).regions[0];
    assert(ext.audio.encoding==Encoding::sf24&&b.read(ext.audio.lowBytes,1)[0]==64);
    Bank c(sf3);const auto comp=c.resolve(0).regions[0];
    assert(comp.audio.encoding==Encoding::vorbis&&comp.audio.bytes==64&&comp.audio.frames==0&&comp.loopEnd==16384);
    Bank d(dls);assert(d.isDls&&d.programs.size()==1&&d.programs[0].drums&&d.programs[0].bank==130&&d.programs[0].number==10);
    auto dm=d.resolve(0);const auto& dr=dm.regions[0];
    assert(dr.low==48&&dr.high==72&&dr.soft==10&&dr.hard==110&&dr.root==62&&dr.cents==25);
    assert(dr.loop&&dr.loopStart==1024&&dr.loopEnd==9216&&dr.audio.frames==196608);
    fixtures::write(bad,fixtures::sf(false,false,true));
    bool failed=false;try { Bank invalid(bad);invalid.resolve(0); }catch(const std::exception&){failed=true;}assert(failed);
    fixtures::write(bad,fixtures::dls(true));
    failed=false;try { Bank invalid(bad);invalid.resolve(0); }catch(const std::exception&){failed=true;}assert(failed);
    fixtures::write(bad,Bytes{'R','I','F','F',255,255,255,255,'s','f','b','k'});
    failed=false;try { Bank invalid(bad); }catch(const std::exception&){failed=true;}assert(failed);
    failed=false;try { a.resolve(999); }catch(const std::exception&){failed=true;}assert(failed);
    for(bool modern:{false,true})
    {
        const auto folder=dir/(modern?"s3000":"s1000");
        std::filesystem::create_directory(folder);
        const auto sample=folder/(modern?"STRINGS.S3":"STRINGS.S1");
        const auto program=folder/(modern?"ORCH.P3":"ORCH.P1");
        fixtures::write(sample,legacyfixtures::sample(modern));
        fixtures::write(program,legacyfixtures::program(modern));
        Bank ak(program);assert(ak.isAkai&&ak.programs.size()==1&&ak.programs[0].name=="ORCHESTRA");
        const auto r=ak.resolve(0).regions.at(0);
        assert(r.root==60&&r.low==48&&r.high==72&&r.soft==20&&r.hard==110);
        assert(r.transpose==1&&std::abs(r.cents)<1e-5f&&r.pan==20);
        assert(r.audio.offset==(modern?192u:150u)&&r.audio.frames==196608&&r.audio.rate==48000);
        assert(r.loop&&r.sustainLoop&&r.loopStart==70000&&r.loopEnd==100001);
        assert(s16(ak.readAudio(r.audio,r.audio.offset,2),0)==-16384);
        Bank single(sample);assert(single.resolve(0).regions[0].root==60);
        fixtures::write(folder/(modern?"Duplicate.S3":"Duplicate.S1"),legacyfixtures::sample(modern));
        failed=false;try{ak.resolve(0);}catch(const std::exception&){failed=true;}assert(failed);
        std::filesystem::remove(folder/(modern?"Duplicate.S3":"Duplicate.S1"));
        auto invalid=legacyfixtures::sample(modern);invalid.pop_back();fixtures::write(sample,invalid);
        failed=false;try{ak.resolve(0);}catch(const std::exception&){failed=true;}assert(failed);
    }
    for(bool extended:{false,true})
    {
        const auto gig=dir/"fixture.gig";
        fixtures::write(gig,legacyfixtures::gig(extended));
        { Bank g(gig);assert(g.isGig&&g.programs[0].name=="GIG fixture");
          const auto r=g.resolve(0).regions.at(0);
          assert(r.root==62&&r.cents==25&&r.loopEnd==9216&&r.audio.frames==196608); }
        fixtures::write(gig,legacyfixtures::gig(extended,true));
        failed=false;try{Bank g(gig);g.resolve(0);}catch(const std::exception&){failed=true;}assert(failed);
    }
    std::puts("Legacy banks: S1000/S3000 PCM, keygroups, loops, ambiguous samples, GIG dimension cues and unsupported dimensions tested.");
    std::puts("Sound banks: catalogs, SF2 inheritance/ranges, SF24, SF3 metadata, DLS wave links, loops and invalid offsets passed.");
}
