// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../StudioExs.h"
#include <fstream>
#include <filesystem>
namespace studio::exs::fixtures
{
inline void number(Bytes& data,std::size_t at,std::uint32_t value,bool big)
{
    for(int i=0;i<4;++i)data.at(at+(big?3-i:i))=static_cast<unsigned char>(value>>(8*i));
}
inline void name(Bytes& data,std::size_t at,std::size_t length,const std::string& value)
{
    for(std::size_t i=0;i<std::min(length,value.size());++i)data.at(at+i)=static_cast<unsigned char>(value[i]);
}
inline void block(Bytes& output,int type,int id,const std::string& label,const Bytes& body,bool big,bool extended)
{
    Bytes h(84);
    h[0]=big?0:1;h[1]=1;h[2]=0;h[3]=static_cast<unsigned char>(type|(extended?(big?128:64):0));
    number(h,4,static_cast<std::uint32_t>(body.size())|(extended&&big?0x8000:0),big);
    number(h,8,static_cast<std::uint32_t>(id),big);
    name(h,16,4,big?"SOBT":"TBOS");name(h,20,64,label);
    output.insert(output.end(),h.begin(),h.end());output.insert(output.end(),body.begin(),body.end());
}
inline Bytes zone(int sample,int group,bool big,bool loop=true,bool reverse=false)
{
    Bytes z(148);
    z[0]=static_cast<unsigned char>(8|(reverse?4:0));z[1]=60;z[2]=static_cast<unsigned char>(-25);z[3]=10;z[4]=static_cast<unsigned char>(-3);
    z[6]=48;z[7]=72;z[9]=20;z[10]=120;
    number(z,12,65536,big);number(z,16,196608,big);
    if(loop)
    {
        number(z,20,70000,big);number(z,24,100000,big);number(z,28,5,big);
        z[33]=7;
    }
    z[80]=1;
    number(z,88,static_cast<std::uint32_t>(group),big);number(z,92,static_cast<std::uint32_t>(sample),big);
    return z;
}
inline Bytes group(bool big,int condition=0,int sequence=-1)
{
    Bytes g(144);
    g[0]=static_cast<unsigned char>(-2);g[1]=static_cast<unsigned char>(-5);g[5]=32;g[6]=100;
    g[32]=128;g[35]=128;
    g[75]=24;
    number(g,80,static_cast<std::uint32_t>(sequence),big);g[84]=static_cast<unsigned char>(condition);
    g[85]=1;g[86]=64;g[87]=127;g[88]=50;g[89]=70;g[90]=2;
    return g;
}
inline Bytes sample(bool big,const std::string& filename="Long C4.wav",bool modern=true)
{
    Bytes s(modern?600:336);
    number(s,4,196608,big);number(s,8,48000,big);number(s,12,24,big);number(s,16,2,big);
    name(s,28,4,"WAVE");
    name(s,80,256,"/old/location/Samples");
    if(modern)name(s,336,256,filename);
    return s;
}
inline Bytes make(bool big=false,bool extended=false,bool badLink=false,bool modern=true)
{
    Bytes out;
    block(out,0,0,"EXS Fixture",Bytes(88),big,extended);
    block(out,1,0,"Long",zone(badLink?9:0,3,big),big,extended);
    block(out,1,1,"Reverse",zone(0,3,big,false,true),big,extended);
    block(out,2,3,"Modulation",group(big,3),big,extended);
    block(out,3,0,modern?"Header alias":"Long C4.wav",sample(big,"Long C4.wav",modern),big,extended);
    block(out,4,0,"Parameters",Bytes(64),big,extended);
    return out;
}
inline void write(const std::filesystem::path& path,const Bytes& data)
{
    std::ofstream stream(path,std::ios::binary);
    if(!stream.write(reinterpret_cast<const char*>(data.data()),static_cast<std::streamsize>(data.size())))
        throw std::runtime_error("Cannot create EXS fixture");
}
}
