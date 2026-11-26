// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../StudioSoundBank.h"

namespace studio::soundbank::fixtures
{
inline void add(Bytes& b,const Bytes& a) { b.insert(b.end(),a.begin(),a.end()); }
inline void word(Bytes& b,int n) { b.push_back(static_cast<unsigned char>(n));b.push_back(static_cast<unsigned char>(n>>8)); }
inline void dword(Bytes& b,U32 n) { word(b,int(n&65535));word(b,int(n>>16)); }
inline void name(Bytes& b,const std::string& s,std::size_t width)
{ for(std::size_t i=0;i<width;++i)b.push_back(i<s.size()?static_cast<unsigned char>(s[i]):0); }
inline Bytes chunk(const std::string& tag,const Bytes& payload)
{ Bytes b;name(b,tag,4);dword(b,static_cast<U32>(payload.size()));add(b,payload);if(payload.size()&1)b.push_back(0);return b; }
inline Bytes list(const std::string& type,std::initializer_list<Bytes> chunks,const std::string& tag="LIST")
{ Bytes b;name(b,type,4);for(const auto& c:chunks)add(b,c);return chunk(tag,b); }
inline Bytes gen(std::initializer_list<std::pair<int,int>> pairs)
{ Bytes b;for(auto [op,v]:pairs){word(b,op);word(b,v);}return b; }
inline Bytes bags(std::initializer_list<int> offsets)
{ Bytes b;for(auto v:offsets){word(b,v);word(b,0);}return b; }
inline Bytes sf(bool compressed=false,bool extended=false,bool corrupt=false)
{
    Bytes ph,ins,sh,pcm,low;
    const auto preset=[&](std::string n,int program,int bank,int bag)
    { name(ph,n,20);word(ph,program);word(ph,bank);word(ph,bag);for(int j=0;j<3;++j)dword(ph,0); };
    preset("Warm strings",40,0,0);preset("Drum test",0,128,2);preset("EOP",0,0,3);
    name(ins,"Test instrument",20);word(ins,0);name(ins,"EOI",20);word(ins,2);
    const auto sample=[&](std::string n,U32 a,U32 b,U32 l,U32 r,int type)
    { name(sh,n,20);dword(sh,a);dword(sh,b);dword(sh,l);dword(sh,r);dword(sh,48000);sh.push_back(60);sh.push_back(5);word(sh,0);word(sh,type); };
    sample("Test sample",0,corrupt?999999:compressed?64:65536,1024,16384,compressed?17:1);
    sample("EOS",0,0,0,0,1);
    if(compressed) { pcm.resize(64);name(low,"",0); }
    else for(int i=0;i<65582;++i) { word(pcm,(i%256-128)*128);low.push_back(64); }
    auto data=extended?list("sdta",{chunk("smpl",pcm),chunk("sm24",low)}):list("sdta",{chunk("smpl",pcm)});
    // Preset global attenuation; preset local range intersects instrument range.
    auto pd=list("pdta",{chunk("phdr",ph),chunk("pbag",bags({0,1,3,4})),
        chunk("pmod",Bytes(10)),chunk("pgen",gen({{48,20},{43,0x5040},{41,0},{41,0},{0,0}})),
        chunk("inst",ins),chunk("ibag",bags({0,1,7})),chunk("imod",Bytes(10)),
        chunk("igen",gen({{34,-1200},{43,0x6030},{44,0x7f20},{17,-100},{48,30},{54,3},{53,0},{0,0}})),chunk("shdr",sh)});
    return list("sfbk",{data,pd},"RIFF");
}
inline Bytes dls(bool corrupt=false)
{
    Bytes fmt,pcm,ws,rh,wl,ih,pt;
    word(fmt,1);word(fmt,1);dword(fmt,48000);dword(fmt,96000);word(fmt,2);word(fmt,16);
    for(int i=0;i<65536;++i) word(pcm,(i%256-128)*128);
    dword(ws,20);word(ws,62);word(ws,25);dword(ws,0);dword(ws,0);dword(ws,1);
    dword(ws,16);dword(ws,0);dword(ws,1024);dword(ws,8192);
    word(rh,48);word(rh,72);word(rh,10);word(rh,110);word(rh,0);word(rh,0);
    word(wl,0);word(wl,0);dword(wl,1);dword(wl,corrupt?1:0);
    dword(ih,1);dword(ih,0x80000102);dword(ih,10);
    dword(pt,8);dword(pt,1);dword(pt,0);
    Bytes title;name(title,"DLS fixture",12);
    return list("DLS ",{chunk("ptbl",pt),
        list("lins",{list("ins ",{chunk("insh",ih),list("lrgn",{list("rgn ",{chunk("rgnh",rh),chunk("wlnk",wl),chunk("wsmp",ws)})}),
            list("INFO",{chunk("INAM",title)})})}),
        list("wvpl",{list("wave",{chunk("fmt ",fmt),chunk("data",pcm)})})},"RIFF");
}
inline void write(const std::filesystem::path& path,const Bytes& data)
{
    std::ofstream f(path,std::ios::binary);
    if(!f.write(reinterpret_cast<const char*>(data.data()),static_cast<std::streamsize>(data.size())))
        throw std::runtime_error("Cannot write test bank");
}
}
