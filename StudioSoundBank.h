// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <filesystem>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace studio::soundbank
{
using U64 = std::uint64_t;
using U32 = std::uint32_t;
using Bytes = std::vector<unsigned char>;
inline U32 u16 (const Bytes& b, std::size_t p)
{ if (p > b.size() || b.size() - p < 2) throw std::runtime_error ("Truncated bank field"); return U32(b[p]) | U32(b[p+1]) << 8; }
inline U32 u32 (const Bytes& b, std::size_t p)
{ return u16(b,p) | u16(b,p+2) << 16; }
inline int s16 (const Bytes& b, std::size_t p)
{ const auto n=u16(b,p); return n<32768 ? int(n) : int(n)-65536; }
inline std::int64_t s32 (const Bytes& b, std::size_t p)
{ const auto n=u32(b,p); return n<0x80000000u ? std::int64_t(n) : std::int64_t(n)-0x100000000ll; }
inline std::string str (const Bytes& b, std::size_t p, std::size_t count)
{
    if (p > b.size() || count > b.size()-p) throw std::runtime_error ("Truncated bank string");
    std::string s(b.begin()+p,b.begin()+p+count);
    if (auto n=s.find('\0'); n!=std::string::npos) s.resize(n);
    while (!s.empty() && (s.back()==' ' || s.back()=='\r' || s.back()=='\n')) s.pop_back();
    return s;
}
inline std::string fourcc (const Bytes& b,std::size_t p)
{
    if(p>b.size()||b.size()-p<4) throw std::runtime_error("Truncated RIFF FourCC");
    return std::string(b.begin()+p,b.begin()+p+4);
}
struct Chunk { std::string id, type; U64 header=0, data=0, size=0; };
enum class Encoding { pcm8, pcm16, pcm24, sf24, vorbis };
struct Audio
{
    Encoding encoding=Encoding::pcm16;
    U64 offset=0, bytes=0, lowBytes=0;
    int channels=1, rate=44100, frames=0;
};
struct Region
{
    Audio audio;
    int root=60, low=0, high=127, soft=1, hard=127;
    int start=0, end=0, loopStart=0, loopEnd=0, transpose=0, keyTrack=100;
    float cents=0, gainDb=0, pan=0, attack=.001f, decay=.001f, sustain=1, release=.001f;
    bool loop=false, sustainLoop=false;
};
struct Program { std::string name; U32 bank=0, number=0; bool drums=false; };
struct Mapping { std::vector<Region> regions; std::set<std::string> warnings; };

// Random-access RIFF reader. Catalog/region traversal reads bounded metadata
// only; the original PCM or Ogg payload is never loaded as one bank-sized blob.
class Bank
{
public:
    explicit Bank (const std::filesystem::path& path) : input(path,std::ios::binary)
    {
        if (!input) fail("Cannot open sound bank");
        input.seekg(0,std::ios::end);
        const auto end=input.tellg();
        if(end<12) fail("Invalid RIFF bank");
        length=static_cast<U64>(end);
        const auto h=read(0,12);
        if(fourcc(h,0)!="RIFF") fail("Not a little-endian RIFF sound bank");
        const U64 declared=U64(u32(h,4))+8;
        if(declared>length || declared<12) fail("Truncated RIFF bank");
        const auto type=fourcc(h,8);
        const auto chunks=children(12,declared);
        if(type=="sfbk") loadSf(chunks);
        else if(type=="DLS ") loadDls(chunks);
        else fail("Unsupported RIFF bank type");
        if(programs.empty()) fail("No instruments in bank");
    }
    std::vector<Program> programs;
    bool isDls=false;
    Bytes read(U64 offset,U64 count)
    {
        if(offset>length || count>length-offset || count>32u*1024u*1024u) fail("Invalid or oversized bank metadata read");
        Bytes b(static_cast<std::size_t>(count));
        if(count)
        {
            input.clear(); input.seekg(static_cast<std::streamoff>(offset));
            if(!input.read(reinterpret_cast<char*>(b.data()),static_cast<std::streamsize>(count))) fail("Cannot read sound bank");
        }
        return b;
    }
    Mapping resolve(std::size_t index,int maxRegions=128)
    {
        if(index>=programs.size() || maxRegions<1) fail("Invalid bank instrument selection");
        Mapping result;
        if(isDls) resolveDls(index,result,maxRegions);
        else resolveSf(index,result,maxRegions);
        if(result.regions.empty()) fail("No convertible regions in this bank instrument");
        return result;
    }
private:
    std::ifstream input;
    U64 length=0;
    Chunk smpl,sm24;
    Bytes phdr,pbag,pgen,pmod,inst,ibag,igen,imod,shdr;
    std::vector<Chunk> instruments;
    std::map<U32,Chunk> dlsWaves;
    std::vector<U32> cues;
    [[noreturn]] static void fail(const char* msg) { throw std::runtime_error(msg); }
    std::vector<Chunk> children(U64 begin,U64 end)
    {
        std::vector<Chunk> out;
        if(end>length || begin>end) fail("Invalid RIFF list");
        while(begin<end)
        {
            if(end-begin<8 || out.size()>=65536) fail("Truncated or excessive RIFF chunks");
            const auto h=read(begin,8);
            Chunk c{fourcc(h,0),{},begin,begin+8,u32(h,4)};
            const auto pad=c.size&1;
            if(c.size+pad>end-c.data) fail("RIFF chunk exceeds parent");
            const auto next=c.data+c.size+pad;
            if(c.id=="LIST") { if(c.size<4) fail("Invalid RIFF LIST"); c.type=fourcc(read(c.data,4),0); c.data+=4;c.size-=4; }
            out.push_back(c); begin=next;
        }
        return out;
    }
    std::vector<Chunk> children(const Chunk& c) { return children(c.data,c.data+c.size); }
    static Chunk get(const std::vector<Chunk>& chunks,const std::string& id,const std::string& type={})
    {
        for(const auto& c:chunks) if(c.id==id && (type.empty()||c.type==type)) return c;
        return {};
    }
    Bytes table(const std::vector<Chunk>& chunks,const char* id,int stride,int minimum=1)
    {
        const auto c=get(chunks,id);
        if(c.id.empty() || c.size%stride || c.size/U64(stride)<U64(minimum) || c.size/U64(stride)>65536)
            fail("Missing or malformed SoundFont table");
        return read(c.data,c.size);
    }
    static void bounds(int lo,int hi,int min,int max)
    { if(lo<min || hi>max || lo>hi) fail("Invalid bank key or velocity range"); }
    static void append(Mapping& m,Region r,int limit)
    {
        if(static_cast<int>(m.regions.size())>=limit) fail("Instrument exceeds Studio region limit; no partial conversion");
        m.regions.push_back(std::move(r));
    }
    void loadSf(const std::vector<Chunk>& chunks)
    {
        const auto sd=get(chunks,"LIST","sdta"), pd=get(chunks,"LIST","pdta");
        if(sd.id.empty()||pd.id.empty()) fail("Missing SoundFont sample or preset data");
        const auto data=children(sd), meta=children(pd);
        smpl=get(data,"smpl");sm24=get(data,"sm24");
        if(smpl.id.empty()||smpl.size==0) fail("Missing SoundFont audio data");
        phdr=table(meta,"phdr",38,2);pbag=table(meta,"pbag",4);pgen=table(meta,"pgen",4);
        pmod=table(meta,"pmod",10);inst=table(meta,"inst",22,2);ibag=table(meta,"ibag",4);
        igen=table(meta,"igen",4);imod=table(meta,"imod",10);shdr=table(meta,"shdr",46,2);
        if(str(phdr,phdr.size()-38,20)!="EOP" || str(inst,inst.size()-22,20)!="EOI" || str(shdr,shdr.size()-46,20)!="EOS")
            fail("Missing SoundFont terminal records");
        for(std::size_t p=0;p+38<phdr.size();p+=38)
        {
            const auto bank=u16(phdr,p+22), num=u16(phdr,p+20);
            programs.push_back({str(phdr,p,20),bank,num,bank==128});
        }
    }
    using Gens=std::map<int,int>;
    static void overlay(Gens& dst,const Gens& src)
    { for(auto [k,v]:src) dst[k]=v; }
    static int val(const Gens& g,int key,int fallback=0)
    { const auto it=g.find(key);return it==g.end()?fallback:it->second; }
    struct Bag { Gens g; bool mods=false; };
    static std::vector<Bag> zones(const Bytes& bags,const Bytes& gens,const Bytes& mods,U32 first,U32 last,int terminal)
    {
        if(first>last||last>=bags.size()/4) fail("Invalid SoundFont bag indices");
        std::vector<Bag> out;
        for(U32 z=first;z<last;++z)
        {
            const auto begin=u16(bags,z*4),end=u16(bags,(z+1)*4);
            const auto mb=u16(bags,z*4+2),me=u16(bags,(z+1)*4+2);
            if(begin>end||end>gens.size()/4||mb>me||me>mods.size()/10) fail("Invalid SoundFont generator/modulator indices");
            Bag b; b.mods=mb!=me;
            for(U32 i=begin;i<end;++i)
            {
                const int op=static_cast<int>(u16(gens,i*4));
                if(op==terminal && i+1!=end) fail("SoundFont link must terminate local zone");
                const int v=(op==41||op==53||op==43||op==44)?int(u16(gens,i*4+2)):s16(gens,i*4+2);
                if(!b.g.emplace(op,v).second) fail("Duplicate SoundFont generator");
            }
            out.push_back(std::move(b));
        }
        return out;
    }
    static std::vector<Bag> withGlobal(std::vector<Bag> in,int terminal)
    {
        Bag global;
        std::vector<Bag> local;
        for(std::size_t i=0;i<in.size();++i)
        {
            auto& b=in[i];
            if(!b.g.count(terminal))
            { if(i!=0) fail("Non-first global SoundFont zone");global=std::move(b);continue; }
            Bag merged=global;overlay(merged.g,b.g);merged.mods|=b.mods;local.push_back(std::move(merged));
        }
        return local;
    }
    static int boundedInt(std::int64_t v)
    { if(v<0||v>=std::numeric_limits<int>::max()) fail("Sample frame bounds exceed Studio limit");return int(v); }
    static float seconds(int tc)
    { return tc<=-32768?0.f:float(std::clamp(std::exp2(tc/1200.0),0.0,100.0)); }
    static void sfWarnings(const Gens& g,Mapping& m)
    {
        const std::set<int> supported{0,1,2,3,4,12,17,34,36,37,38,41,43,44,45,48,50,51,52,53,54,56,58};
        for(auto [op,value]:g) if(!supported.count(op))
            m.warnings.insert("Unsupported SoundFont generator "+std::to_string(op));
    }
    void resolveSf(std::size_t index,Mapping& m,int limit)
    {
        const auto p=index*38;
        const auto presets=withGlobal(zones(pbag,pgen,pmod,u16(phdr,p+24),u16(phdr,p+62),41),41);
        std::size_t visits=0;
        for(const auto& pb:presets)
        {
            const auto ii=std::size_t(val(pb.g,41));
            if(ii>=inst.size()/22-1) fail("Invalid SoundFont instrument link");
            const auto local=withGlobal(zones(ibag,igen,imod,u16(inst,ii*22+20),u16(inst,ii*22+42),53),53);
            for(const auto& ib:local)
            {
                if(++visits>65536) fail("Excessive SoundFont region expansion");
                Region r;
                const auto range=[&](int op,int& lo,int& hi)
                {
                    const int a=val(pb.g,op,0x7f00), b=val(ib.g,op,0x7f00);
                    bounds(a&255,(a>>8)&255,0,127);bounds(b&255,(b>>8)&255,0,127);
                    lo=std::max(a&255,b&255);hi=std::min((a>>8)&255,(b>>8)&255);
                };
                range(43,r.low,r.high);range(44,r.soft,r.hard);r.soft=std::max(1,r.soft);
                if(r.low>r.high||r.soft>r.hard) continue;
                sfWarnings(pb.g,m);sfWarnings(ib.g,m);
                if(pb.mods||ib.mods) m.warnings.insert("Custom SoundFont modulators not converted");
                const auto sample=std::size_t(val(ib.g,53));
                if(sample>=shdr.size()/46-1) fail("Invalid SoundFont sample link");
                const auto s=sample*46;
                const U64 a=u32(shdr,s+20),b=u32(shdr,s+24);
                const auto rate=u32(shdr,s+36),type=u16(shdr,s+44);
                if(type&0x8000u) fail("ROM SoundFont samples are unavailable");
                if(rate<1000||rate>768000||b<=a) fail("Invalid SoundFont sample range/rate");
                const int kind=type&0xf;
                if(kind!=1&&kind!=2&&kind!=4) fail("Unsupported linked SoundFont sample type");
                const auto sum=[&](int op,int fallback=0){return val(ib.g,op,fallback)+val(pb.g,op);};
                const auto off=[&](int fine,int coarse){return std::int64_t(val(ib.g,fine))+32768ll*val(ib.g,coarse);};
                if(type&0x10u)
                {
                    if(b>smpl.size) fail("Compressed SoundFont sample exceeds smpl");
                    if(off(0,4)||off(1,12)) fail("SF3 sample address offsets not supported");
                    r.audio={Encoding::vorbis,smpl.data+a,b-a,0,1,int(rate),0};
                    if((val(ib.g,54)&3)==1||(val(ib.g,54)&3)==3)
                    {
                        r.loopStart=boundedInt(std::int64_t(u32(shdr,s+28))+off(2,45));
                        r.loopEnd=boundedInt(std::int64_t(u32(shdr,s+32))+off(3,50));
                    }
                }
                else
                {
                    const auto first=std::int64_t(a)+off(0,4),last=std::int64_t(b)+off(1,12);
                    if(first<0||last<=first||U64(last)>smpl.size/2) fail("SoundFont sample exceeds PCM pool");
                    r.audio={Encoding::pcm16,smpl.data+U64(first)*2,U64(last-first)*2,0,1,int(rate),boundedInt(last-first)};
                    if(!sm24.id.empty())
                    {
                        if(U64(last)>sm24.size) fail("SoundFont sm24 data truncated");
                        r.audio.encoding=Encoding::sf24;r.audio.lowBytes=sm24.data+U64(first);
                    }
                    // Only validate loops when used; disabled headers often contain placeholder offsets.
                    if((val(ib.g,54)&3)==1||(val(ib.g,54)&3)==3)
                    {
                        r.loopStart=boundedInt(std::int64_t(u32(shdr,s+28))+off(2,45)-first);
                        r.loopEnd=boundedInt(std::int64_t(u32(shdr,s+32))+off(3,50)-first);
                    }
                }
                const int root=val(ib.g,58,shdr[s+40]);
                r.root=root==255?60:root;
                if(r.root<0||r.root>127) fail("Invalid SoundFont root key");
                const int correction=shdr[s+41]<128?shdr[s+41]:int(shdr[s+41])-256;
                const int cents=sum(52)+correction;
                const int semitones=sum(51)+cents/100;
                if(semitones<-48||semitones>48) fail("SoundFont transpose exceeds native range");
                r.transpose=semitones;r.cents=float(cents%100);
                r.keyTrack=std::clamp(val(ib.g,56,100)+val(pb.g,56),0,200);
                const auto pan=sum(17);
                r.pan=float(std::clamp(pan/5.0,-100.0,100.0));
                if(kind==2||kind==4)
                {
                    m.warnings.insert("Linked left/right samples imported as separate mono regions; automatic stereo pairing not emulated");
                    if(!ib.g.count(17)&&!pb.g.count(17)) r.pan=kind==2?100.f:-100.f;
                }
                r.gainDb=float(std::clamp(-sum(48)/10.0,-60.0,12.0));
                if(sum(48)>600) m.warnings.insert("Very large attenuation limited to native -60 dB");
                r.attack=seconds(sum(34,-12000));r.decay=seconds(sum(36,-12000));
                r.release=seconds(sum(38,-12000));r.sustain=float(std::clamp(std::pow(10.0,-sum(37)/200.0),0.0,1.0));
                const int mode=val(ib.g,54)&3;r.loop=mode==1||mode==3;r.sustainLoop=mode==3;
                if(mode==2) m.warnings.insert("Reserved SoundFont sample mode treated as no loop");
                if(r.loop && (r.loopEnd-r.loopStart<2 || (r.audio.frames>0 && r.loopEnd>r.audio.frames)))
                    fail("Invalid SoundFont loop");
                if(val(ib.g,57)!=0) m.warnings.insert("Exclusive classes (drum choking) not converted");
                if(pb.g.count(0)||pb.g.count(1)||pb.g.count(2)||pb.g.count(3)||pb.g.count(4)||pb.g.count(12)||pb.g.count(45)||pb.g.count(50)||pb.g.count(54)||pb.g.count(58)) m.warnings.insert("Invalid preset-level instrument-only generators ignored");
                append(m,r,limit);
            }
        }
        m.warnings.insert("Native envelope and velocity response approximate SoundFont synthesis; filters, effects, default/custom modulators and exclusive classes are not emulated");
    }
    std::string infoName(const std::vector<Chunk>& chunks,const std::string& fallback)
    {
        const auto info=get(chunks,"LIST","INFO");
        if(info.id.empty())return fallback;
        const auto name=get(children(info),"INAM");
        if(name.id.empty()||name.size>4096)return fallback;
        auto s=str(read(name.data,name.size),0,std::size_t(name.size));
        return s.empty()?fallback:s;
    }
    void loadDls(const std::vector<Chunk>& chunks)
    {
        isDls=true;
        const auto lins=get(chunks,"LIST","lins"),pool=get(chunks,"LIST","wvpl"),ptbl=get(chunks,"ptbl");
        if(lins.id.empty()||pool.id.empty()||ptbl.id.empty()||ptbl.size<8) fail("Missing DLS instrument or wave pool");
        const auto header=read(ptbl.data,8);
        const auto size=u32(header,0),count=u32(header,4);
        if(size<8||size>ptbl.size||count>65536||U64(count)*4>ptbl.size-size) fail("Invalid DLS pool table");
        const auto entries=read(ptbl.data+size,U64(count)*4);
        for(U32 i=0;i<count;++i)cues.push_back(u32(entries,i*4));
        for(const auto& c:children(pool)) if(c.id=="LIST"&&c.type=="wave") dlsWaves.emplace(U32(c.header-pool.data),c);
        for(const auto& c:children(lins)) if(c.id=="LIST"&&c.type=="ins ")
        {
            const auto cs=children(c);const auto h=get(cs,"insh");
            if(h.size<12) fail("Truncated DLS instrument header");
            const auto b=read(h.data,12);const auto bank=u32(b,4);
            programs.push_back({infoName(cs,"Instrument "+std::to_string(programs.size()+1)),((bank>>8)&127)*128+(bank&127),u32(b,8)&127,(bank&0x80000000u)!=0});
            instruments.push_back(c);
        }
    }
    void resolveDls(std::size_t index,Mapping& m,int limit)
    {
        const auto cs=children(instruments[index]);const auto lr=get(cs,"LIST","lrgn");
        if(lr.id.empty()) fail("Missing DLS regions");
        if(!get(cs,"LIST","lart").id.empty()||!get(cs,"LIST","lar2").id.empty()) m.warnings.insert("DLS articulation connections not converted");
        std::size_t count=0;
        for(const auto& rc:children(lr)) if(rc.id=="LIST"&&(rc.type=="rgn "||rc.type=="rgn2"))
        {
            ++count;
            const auto rs=children(rc);const auto rh=get(rs,"rgnh"),wl=get(rs,"wlnk");
            if(rh.size<12||wl.size<12)fail("Truncated DLS region");
            const auto h=read(rh.data,12),l=read(wl.data,12);
            Region r;
            r.low=int(u16(h,0));r.high=int(u16(h,2));r.soft=std::max(1,int(u16(h,4)));r.hard=int(u16(h,6));
            bounds(r.low,r.high,0,127);bounds(r.soft,r.hard,1,127);
            if(u16(h,10)) m.warnings.insert("DLS exclusive key group not converted");
            if(u16(l,0)&1) m.warnings.insert("DLS multichannel phase groups not emulated");
            if(!get(rs,"LIST","lart").id.empty()||!get(rs,"LIST","lar2").id.empty()) m.warnings.insert("DLS region articulation connections not converted");
            if(!get(rs,"cdl ").id.empty())fail("Conditional DLS region not supported");
            const auto cue=u32(l,8);
            if(cue>=cues.size()||!dlsWaves.count(cues[cue]))fail("Invalid DLS wave pool reference");
            const auto wave=children(dlsWaves.at(cues[cue]));const auto fmt=get(wave,"fmt "),data=get(wave,"data");
            if(fmt.size<16||data.id.empty())fail("Missing DLS wave format/data");
            const auto f=read(fmt.data,16);
            const auto ch=u16(f,2),rate=u32(f,4),bits=u16(f,14),align=u16(f,12);
            if(u16(f,0)!=1||ch<1||ch>2||rate<1000||rate>768000||(bits!=8&&bits!=16&&bits!=24)||align!=ch*(bits/8)||data.size%align)
                fail("Unsupported DLS wave encoding (PCM 8/16/24-bit mono/stereo required)");
            r.audio={bits==8?Encoding::pcm8:bits==16?Encoding::pcm16:Encoding::pcm24,data.data,data.size,0,int(ch),int(rate),boundedInt(data.size/align)};
            if(r.audio.frames<2)fail("Empty DLS wave");
            auto ws=get(rs,"wsmp");if(ws.id.empty())ws=get(wave,"wsmp");
            if(!ws.id.empty())
            {
                if(ws.size<20)fail("Truncated DLS wsmp");
                const auto w=read(ws.data,20);
                const auto cb=u32(w,0),loops=u32(w,16);
                if(cb<20||cb>ws.size||loops>1)fail("Unsupported DLS loop descriptor");
                r.root=int(u16(w,4));if(r.root>127)fail("Invalid DLS unity note");
                const auto fine=s16(w,6);r.transpose=fine/100;r.cents=float(fine%100);
                if(r.transpose<-48||r.transpose>48)fail("DLS tuning exceeds native range");
                r.gainDb=float(std::clamp(s32(w,8)/655360.0,-60.0,12.0));
                if(loops)
                {
                    if(ws.size-cb<16)fail("Truncated DLS sample loop");
                    const auto loop=read(ws.data+cb,16);
                    if(u32(loop,0)<16||u32(loop,0)>ws.size-cb||u32(loop,4)!=0)fail("Unsupported DLS loop type");
                    r.loopStart=boundedInt(u32(loop,8));r.loopEnd=boundedInt(U64(u32(loop,8))+u32(loop,12));
                    if(r.loopEnd-r.loopStart<2||r.loopEnd>r.audio.frames)fail("Invalid DLS loop bounds");
                    r.loop=true;
                }
            }
            append(m,r,limit);
        }
        const auto ih=get(cs,"insh");
        if(count!=u32(read(ih.data,12),0))fail("DLS region count mismatch");
        m.warnings.insert("DLS conversion preserves PCM waves, ranges, tuning and forward loops; connection graphs, envelopes, modulation and key groups are not emulated");
    }
};
}
