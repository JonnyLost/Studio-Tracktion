// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "SoundBankFixtures.h"
namespace studio::soundbank::legacyfixtures
{
inline void put(Bytes& b,std::size_t p,U32 v,int n)
{for(int i=0;i<n;++i)b.at(p+i)=static_cast<unsigned char>(v>>(8*i));}
inline void akaiName(Bytes& b,std::size_t p,std::string n)
{
    const std::string alphabet="0123456789 ABCDEFGHIJKLMNOPQRSTUVWXYZ#+-.";
    n.resize(12,' ');
    for(std::size_t i=0;i<12;++i)b.at(p+i)=static_cast<unsigned char>(alphabet.find(n[i]));
}
inline Bytes sample(bool s3,bool loop=true)
{
    Bytes b(s3?192:150);
    b[0]=3;b[2]=60;akaiName(b,3,"STRINGS");b[15]=128;b[19]=loop?1:2;
    put(b,20,64,2);put(b,26,196608,4);put(b,30,0,4);put(b,34,196608,4);put(b,138,48000,2);
    if(loop){put(b,38,100000,4);put(b,44,30000,4);put(b,48,9999,2);}
    for(int n=0;n<196608;++n)fixtures::word(b,(n%256-128)*128);
    return b;
}
inline Bytes program(bool p3)
{
    const int stride=p3?192:150;
    Bytes b(2*stride);b[0]=1;akaiName(b,3,"ORCHESTRA");b[15]=40;b[19]=36;b[20]=84;b[42]=1;
    b[stride]=2;b[stride+3]=48;b[stride+4]=72;
    const int z=stride+34;akaiName(b,z,"STRINGS");b[z+12]=20;b[z+13]=110;put(b,z+14,192,2);b[z+18]=10;
    return b;
}
inline Bytes gig(bool extended=false,bool multidim=false)
{
    using namespace fixtures;
    Bytes fmt,pcm,ws,rh,wl,ih,pt,dim(extended?1092:172);
    word(fmt,1);word(fmt,1);dword(fmt,48000);dword(fmt,96000);word(fmt,2);word(fmt,16);
    for(int i=0;i<196608;++i)word(pcm,(i%256-128)*128);
    dword(ws,20);word(ws,62);word(ws,25);dword(ws,0);dword(ws,0);dword(ws,1);
    dword(ws,16);dword(ws,0);dword(ws,1024);dword(ws,8192);
    word(rh,48);word(rh,72);word(rh,10);word(rh,110);word(rh,0);word(rh,0);
    word(wl,0);word(wl,0);dword(wl,1);dword(wl,999); // intentionally wrong DLS fallback
    dword(ih,1);dword(ih,0);dword(ih,40);dword(pt,8);dword(pt,1);dword(pt,0);
    put(dim,0,multidim?2:1,4);put(dim,extended?68:44,0,4);
    Bytes title;name(title,"GIG fixture",12);
    return list("DLS ",{chunk("ptbl",pt),list("lins",{list("ins ",{chunk("insh",ih),
        list("lrgn",{list("rgn ",{chunk("rgnh",rh),chunk("wlnk",wl),chunk("3lnk",dim),
        list("3prg",{list("3ewl",{chunk("wsmp",ws)})})})}),list("INFO",{chunk("INAM",title)})})}),
        list("wvpl",{list("wave",{chunk("fmt ",fmt),chunk("data",pcm)})})},"RIFF");
}
}
