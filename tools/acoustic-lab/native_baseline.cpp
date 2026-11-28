// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../SpatialReverb.h"
#include <bit>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs=std::filesystem;
using namespace studio::spatial;
constexpr int rate=44100, duration=18, block=512;
void u16(std::ostream& f,uint16_t v){for(int i=0;i<2;++i)f.put(char((v>>(8*i))&255));}
void u32(std::ostream& f,uint32_t v){for(int i=0;i<4;++i)f.put(char((v>>(8*i))&255));}
void render(const fs::path& out,Room room,char mode,std::ofstream& report)
{
    RoomReverb reverb;reverb.prepare(rate,room);
    std::ofstream f(out,std::ios::binary);
    f.exceptions(std::ios::badbit|std::ios::failbit);
    constexpr int frames=rate*duration,bytes=frames*8;
    f.write("RIFF",4);u32(f,48+bytes);f.write("WAVEfmt ",8);u32(f,16);
    u16(f,3);u16(f,2);u32(f,rate);u32(f,rate*8);u16(f,8);u16(f,32);
    f.write("fact",4);u32(f,4);u32(f,frames);f.write("data",4);u32(f,bytes);
    std::array<float,block> l{},r{};
    double elapsed=0;
    for(int offset=0;offset<frames;offset+=block)
    {
        const int n=std::min(block,frames-offset);
        l.fill(0);r.fill(0);
        if(rate>=offset&&rate<offset+n)
        {
            if(mode!='R')l[rate-offset]=.25f;
            if(mode!='L')r[rate-offset]=.25f;
        }
        auto start=std::chrono::steady_clock::now();
        reverb.process(l.data(),r.data(),n);
        elapsed+=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
        for(int i=0;i<n;++i)
        {
            if(!std::isfinite(l[i])||!std::isfinite(r[i]))throw std::runtime_error("nonfinite room output");
            u32(f,std::bit_cast<uint32_t>(l[i]));u32(f,std::bit_cast<uint32_t>(r[i]));
        }
    }
    report<<out.filename().string()<<','<<elapsed<<','<<reverb.allocatedBytes()<<'\n';
    std::cout<<"Rendered "<<out.filename().string()<<'\n';
}
int main(int argc,char** argv)
{
    try
    {
        if(argc!=2)throw std::runtime_error("Usage: native_baseline LAB_DIRECTORY");
        fs::path root=argv[1];if(!fs::is_directory(root/"renders"))throw std::runtime_error("Generate lab first");
        std::ofstream report(root/"native_performance.csv");report.exceptions(std::ios::badbit|std::ios::failbit);
        report<<"file,dsp_wall_s,allocated_bytes\n";
        for(const auto& preset:factoryRooms)
        {
            std::string name=preset.name;
            for(char& c:name)if(c==' ')c='_';
            for(char mode:{'M','L','R'})
                render(root/"renders"/("studio_"+name+"_"+mode+"1.wav"),preset.room,mode,report);
        }
    }
    catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
