// SPDX-License-Identifier: GPL-3.0-or-later
// Original offline reference measurement host. No plug-in code or resources bundled.
#include <AudioToolbox/AudioToolbox.h>
#include <CoreFoundation/CoreFoundation.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs=std::filesystem;
using Channels=std::vector<std::vector<float>>;
using Values=std::map<AudioUnitParameterID,float>;
constexpr double pi=3.14159265358979323846;
constexpr UInt32 block=512;
constexpr float amplitude=.25f;
constexpr std::array<double,6> tones{125,250,500,1000,4000,8000};

void check(OSStatus code,const std::string& where)
{
    if(code!=noErr)throw std::runtime_error(where+" (OSStatus "+std::to_string(code)+")");
}
std::string cfText(CFStringRef s)
{
    if(!s)return {};
    auto n=CFStringGetMaximumSizeForEncoding(CFStringGetLength(s),kCFStringEncodingUTF8);
    if(n<0||n>65536)return {};
    std::vector<char> text(size_t(n)+1);
    return CFStringGetCString(s,text.data(),CFIndex(text.size()),kCFStringEncodingUTF8)?std::string(text.data()):std::string();
}
std::string csv(const std::string& s)
{
    std::string out="\"";
    for(char c:s){if(c=='"')out+='"';out+=c;}
    return out+'"';
}
std::ofstream output(const fs::path& path,std::ios::openmode mode=std::ios::out)
{
    std::ofstream f(path,mode);
    if(!f)throw std::runtime_error("Cannot write "+path.string());
    f.exceptions(std::ios::badbit|std::ios::failbit);
    f<<std::setprecision(10);
    return f;
}
void u16(std::ostream& f,uint16_t n)
{
    f.put(char(n&255));f.put(char((n>>8)&255));
}
void u32(std::ostream& f,uint32_t n)
{
    for(unsigned i=0;i<4;++i)f.put(char((n>>(8*i))&255));
}
void wav(const fs::path& path,const Channels& data,int rate)
{
    if(data.empty()||data.size()>2||data.front().empty())throw std::runtime_error("Invalid WAV shape");
    const auto frames=data.front().size();
    for(const auto& c:data)if(c.size()!=frames)throw std::runtime_error("Mismatched WAV channels");
    if(frames>10000000)throw std::runtime_error("WAV exceeds laboratory bound");
    const auto count=uint16_t(data.size());const auto bytes=uint32_t(frames*count*4);
    auto f=output(path,std::ios::out|std::ios::binary);
    f.write("RIFF",4);u32(f,48+bytes);f.write("WAVEfmt ",8);u32(f,16);
    u16(f,3);u16(f,count);u32(f,uint32_t(rate));u32(f,uint32_t(rate)*count*4);
    u16(f,uint16_t(count*4));u16(f,32);
    f.write("fact",4);u32(f,4);u32(f,uint32_t(frames));f.write("data",4);u32(f,bytes);
    for(size_t i=0;i<frames;++i)for(const auto& c:data)
    {
        uint32_t bits=0;static_assert(sizeof(bits)==sizeof(float));
        std::memcpy(&bits,&c[i],4);u32(f,bits);
    }
    f.close();
}
struct Stats
{
    double peak=0,rms=0,energy=0,early=0,late=0;
    long first=-1,peakIndex=-1;
};
Stats measure(const std::vector<float>& v,size_t excitation,int rate)
{
    Stats m;
    for(size_t i=0;i<v.size();++i)
    {
        const double x=v[i];
        if(!std::isfinite(x))throw std::runtime_error("Nonfinite render output");
        const double abs=std::abs(x);
        if(abs>m.peak){m.peak=abs;m.peakIndex=long(i);}
        m.energy+=x*x;
        if(i>=excitation)
        {
            if(i-excitation<size_t(rate*.05))m.early+=x*x;
            else m.late+=x*x;
        }
    }
    m.rms=std::sqrt(m.energy/double(v.size()));
    if(m.peak>1e-8)
    {
        const double threshold=std::max(1e-8,m.peak*.001);
        for(size_t i=0;i<v.size();++i)if(std::abs(v[i])>=threshold){m.first=long(i);break;}
    }
    return m;
}
double correlation(const Channels& v)
{
    double l=0,r=0,lr=0;
    for(size_t i=0;i<v[0].size();++i)
    {double a=v[0][i],b=v[1][i];l+=a*a;r+=b*b;lr+=a*b;}
    return l>1e-20&&r>1e-20?lr/std::sqrt(l*r):std::numeric_limits<double>::quiet_NaN();
}
// Coherent multitone amplitude estimate, 1-second steady-state interval.
double tone(const std::vector<float>& v,int rate,double hz)
{
    const size_t start=size_t(rate*.75),n=size_t(rate);
    if(start+n>v.size())throw std::runtime_error("Tone interval outside render");
    double re=0,im=0;
    for(size_t i=0;i<n;++i)
    {
        double phase=2*pi*hz*double(i)/rate;
        re+=v[start+i]*std::cos(phase);im+=v[start+i]*std::sin(phase);
    }
    return 2*std::hypot(re,im)/double(n);
}
struct Param
{
    AudioUnitParameterID id;
    std::string name;
    float low,high,def;
    UInt32 flags,unit;
};
struct Preset{SInt32 id;std::string name;};

struct Input
{
    const Channels& data;
    std::array<std::array<float,block>,2> scratch{};
    long position=0;
    UInt32 count=0;
    bool badTimestamp=false;
};
OSStatus inputCallback(void* ref,AudioUnitRenderActionFlags*,const AudioTimeStamp* time,
                      UInt32 bus,UInt32 frames,AudioBufferList* buffers)
{
    auto& s=*static_cast<Input*>(ref);
    if(!buffers||bus!=0||frames>block||buffers->mNumberBuffers!=s.data.size())
        return kAudioUnitErr_InvalidPropertyValue;
    // Fail rather than silently misalign if a host/plug-in changes the timeline.
    if(!time||!(time->mFlags&kAudioTimeStampSampleTimeValid)||
       !std::isfinite(time->mSampleTime)||time->mSampleTime<double(s.position)||
       time->mSampleTime+frames>double(s.position)+s.count||
       std::abs(time->mSampleTime-std::round(time->mSampleTime))>0.01)
    {s.badTimestamp=true;return kAudioUnitErr_InvalidPropertyValue;}
    const auto offset=long(std::llround(time->mSampleTime));
    for(UInt32 ch=0;ch<buffers->mNumberBuffers;++ch)
    {
        auto& b=buffers->mBuffers[ch];
        if(b.mNumberChannels!=1)return kAudioUnitErr_InvalidPropertyValue;
        if(b.mData&&b.mDataByteSize<frames*sizeof(float))return kAudioUnitErr_InvalidPropertyValue;
        if(!b.mData)b.mData=s.scratch[ch].data();
        auto* dst=static_cast<float*>(b.mData);
        for(UInt32 j=0;j<frames;++j)
        {
            auto k=offset+long(j);
            dst[j]=k>=0&&size_t(k)<s.data[ch].size()?s.data[ch][size_t(k)]:0.0f;
        }
        b.mDataByteSize=frames*sizeof(float);
    }
    return noErr;
}

struct Unit
{
    AudioUnit au=nullptr;
    bool initialized=false;
    explicit Unit(AudioComponent c)
    {
        check(AudioComponentInstanceNew(c,&au),"Instantiate Audio Unit");
    }
    ~Unit(){if(au){if(initialized)AudioUnitUninitialize(au);AudioComponentInstanceDispose(au);}}
    Unit(const Unit&)=delete;Unit& operator=(const Unit&)=delete;
    template<class T> void set(AudioUnitPropertyID p,AudioUnitScope scope,AudioUnitElement e,const T& value)
    {check(AudioUnitSetProperty(au,p,scope,e,&value,UInt32(sizeof(value))),"Set property "+std::to_string(p));}
    template<class T> bool get(AudioUnitPropertyID p,AudioUnitScope scope,AudioUnitElement e,T& value)
    {UInt32 bytes=sizeof(value);return AudioUnitGetProperty(au,p,scope,e,&value,&bytes)==noErr&&bytes==sizeof(value);}
    static AudioStreamBasicDescription format(int rate,UInt32 ch)
    {
        AudioStreamBasicDescription f{};
        f.mSampleRate=rate;f.mFormatID=kAudioFormatLinearPCM;
        f.mFormatFlags=kAudioFormatFlagsNativeFloatPacked|kAudioFormatFlagIsNonInterleaved;
        f.mBytesPerPacket=4;f.mFramesPerPacket=1;f.mBytesPerFrame=4;
        f.mChannelsPerFrame=ch;f.mBitsPerChannel=32;return f;
    }
    void prepare(int rate,UInt32 channels,Input& source)
    {
        auto in=format(rate,channels),out=format(rate,2);
        set(kAudioUnitProperty_StreamFormat,kAudioUnitScope_Input,0,in);
        set(kAudioUnitProperty_StreamFormat,kAudioUnitScope_Output,0,out);
        set(kAudioUnitProperty_MaximumFramesPerSlice,kAudioUnitScope_Global,0,block);
        AURenderCallbackStruct cb{inputCallback,&source};
        set(kAudioUnitProperty_SetRenderCallback,kAudioUnitScope_Input,0,cb);
        check(AudioUnitInitialize(au),"Initialize Audio Unit");initialized=true;
        AudioStreamBasicDescription actualIn{},actualOut{};
        if(!get(kAudioUnitProperty_StreamFormat,kAudioUnitScope_Input,0,actualIn)||
           !get(kAudioUnitProperty_StreamFormat,kAudioUnitScope_Output,0,actualOut)||
           actualIn.mChannelsPerFrame!=channels||actualOut.mChannelsPerFrame!=2||
           actualIn.mSampleRate!=rate||actualOut.mSampleRate!=rate||
           actualIn.mFormatID!=kAudioFormatLinearPCM||actualOut.mFormatID!=kAudioFormatLinearPCM||
           actualIn.mFormatFlags!=in.mFormatFlags||actualOut.mFormatFlags!=out.mFormatFlags||
           actualIn.mBytesPerFrame!=4||actualOut.mBytesPerFrame!=4)
            throw std::runtime_error("Negotiated AU format differs from requested Float32 planar format");
    }
    std::vector<Param> parameters()
    {
        UInt32 bytes=0;Boolean writable=false;
        check(AudioUnitGetPropertyInfo(au,kAudioUnitProperty_ParameterList,kAudioUnitScope_Global,0,&bytes,&writable),"Parameter list size");
        if(!bytes||bytes%sizeof(AudioUnitParameterID)||bytes>4096*sizeof(AudioUnitParameterID))
            throw std::runtime_error("Unexpected parameter list size");
        std::vector<AudioUnitParameterID> ids(bytes/sizeof(AudioUnitParameterID));
        check(AudioUnitGetProperty(au,kAudioUnitProperty_ParameterList,kAudioUnitScope_Global,0,ids.data(),&bytes),"Parameter list");
        std::vector<Param> result;
        for(auto id:ids)
        {
            AudioUnitParameterInfo info{};
            if(!get(kAudioUnitProperty_ParameterInfo,kAudioUnitScope_Global,id,info))
                throw std::runtime_error("Missing parameter info "+std::to_string(id));
            std::string name(info.name,strnlen(info.name,sizeof(info.name)));
            if(info.flags&kAudioUnitParameterFlag_HasCFNameString)name=cfText(info.cfNameString);
            if(info.flags&kAudioUnitParameterFlag_CFNameRelease)
            {
                if((info.flags&kAudioUnitParameterFlag_HasCFNameString)&&info.cfNameString)CFRelease(info.cfNameString);
                if(info.unit==kAudioUnitParameterUnit_CustomUnit&&info.unitName)CFRelease(info.unitName);
            }
            result.push_back({id,name,info.minValue,info.maxValue,info.defaultValue,UInt32(info.flags),UInt32(info.unit)});
        }
        std::sort(result.begin(),result.end(),[](auto a,auto b){return a.id<b.id;});
        return result;
    }
    float read(AudioUnitParameterID id)
    {
        AudioUnitParameterValue value=0;
        check(AudioUnitGetParameter(au,id,kAudioUnitScope_Global,0,&value),"Read parameter "+std::to_string(id));
        if(!std::isfinite(value))throw std::runtime_error("Nonfinite parameter");
        return value;
    }
    void apply(const Values& v)
    {
        for(auto [id,value]:v)check(AudioUnitSetParameter(au,id,kAudioUnitScope_Global,0,value,0),"Set parameter "+std::to_string(id));
    }
    std::string formatted(AudioUnitParameterID id,float value)
    {
        AudioUnitParameterStringFromValue f{id,&value,nullptr};
        UInt32 size=sizeof(f);
        auto status=AudioUnitGetProperty(au,kAudioUnitProperty_ParameterStringFromValue,kAudioUnitScope_Global,0,&f,&size);
        auto text=status==noErr?cfText(f.outString):std::string();
        if(status==noErr&&f.outString)CFRelease(f.outString);
        return text;
    }
    std::vector<Preset> presets()
    {
        CFArrayRef list=nullptr;
        if(!get(kAudioUnitProperty_FactoryPresets,kAudioUnitScope_Global,0,list)||!list)
            throw std::runtime_error("Factory preset list unavailable");
        std::vector<Preset> result;
        for(CFIndex i=0;i<CFArrayGetCount(list);++i)
        {
            auto* p=static_cast<const AUPreset*>(CFArrayGetValueAtIndex(list,i));
            if(p)result.push_back({p->presetNumber,cfText(p->presetName)});
        }
        CFRelease(list);return result;
    }
    void select(const Preset& p)
    {
        auto name=CFStringCreateWithCString(nullptr,p.name.c_str(),kCFStringEncodingUTF8);
        AUPreset preset{p.id,name};
        auto status=AudioUnitSetProperty(au,kAudioUnitProperty_PresentPreset,kAudioUnitScope_Global,0,&preset,sizeof(preset));
        if(name)CFRelease(name);
        check(status,"Select preset "+std::to_string(p.id));
    }
    Channels render(Input& source)
    {
        const auto frames=source.data.front().size();
        Channels result(2,std::vector<float>(frames));
        struct TwoBuffers{UInt32 count;AudioBuffer buffers[2];};
        static_assert(offsetof(TwoBuffers,buffers)==offsetof(AudioBufferList,mBuffers));
        for(size_t pos=0;pos<frames;pos+=block)
        {
            UInt32 n=UInt32(std::min<size_t>(block,frames-pos));
            AudioTimeStamp time{};time.mSampleTime=double(pos);time.mFlags=kAudioTimeStampSampleTimeValid;
            source.position=long(pos);source.count=n;
            TwoBuffers storage{2,{{1,n*UInt32(sizeof(float)),result[0].data()+pos},
                                 {1,n*UInt32(sizeof(float)),result[1].data()+pos}}};
            auto* buffers=reinterpret_cast<AudioBufferList*>(&storage);
            AudioUnitRenderActionFlags flags=0;
            check(AudioUnitRender(au,&flags,&time,0,n,buffers),"Render at "+std::to_string(pos));
            if(source.badTimestamp)throw std::runtime_error("Unexpected input timestamp");
            if(buffers->mNumberBuffers!=2)throw std::runtime_error("Unexpected output channel count");
            for(size_t ch=0;ch<2;++ch)
            {
                const auto& b=storage.buffers[ch];
                if(!b.mData||b.mDataByteSize<n*sizeof(float)||b.mNumberChannels!=1)
                    throw std::runtime_error("Invalid output buffer");
                if(b.mData!=result[ch].data()+pos)
                    std::memmove(result[ch].data()+pos,b.mData,n*sizeof(float));
            }
        }
        return result;
    }
};

struct Case
{
    std::string name,signal="impulse",routing="mono";
    int rate=48000;
    double seconds=1.25;
    Values settings;
};
Channels signal(const Case& c)
{
    size_t n=size_t(std::ceil(c.seconds*c.rate)),start=size_t(c.rate*.25);
    Channels data(c.routing=="mono"?1:2,std::vector<float>(n));
    for(size_t i=start;i<n;++i)
    {
        double t=double(i-start)/c.rate,x=0;
        if(c.signal=="impulse")x=i==start?amplitude:0;
        else if(c.signal=="tones"&&t<2.0)
        {
            for(double hz:tones)x+=amplitude/double(tones.size())*std::sin(2*pi*hz*t);
            x*=std::min({1.0,t/.02,(2.0-t)/.02});
        }
        else if(c.signal=="sweep"&&t<2.0)
        {
            double logRatio=std::log(18000.0/30.0);
            x=amplitude*std::sin(2*pi*30*2/logRatio*std::expm1(logRatio*t/2));
            x*=std::min({1.0,t/.02,(2.0-t)/.02});
        }
        for(size_t ch=0;ch<data.size();++ch)
            if(c.routing=="mono"||c.routing=="dual"||(c.routing=="left"&&ch==0)||(c.routing=="right"&&ch==1))
                data[ch][i]=float(x);
    }
    return data;
}
std::vector<Case> plan()
{
    // These are controlled normalized values, not assumed physical units or
    // purported internal defaults/neutral positions.
    const Values dry{{0,0},{1,.5f},{2,.5f},{3,.5f},{4,.5f},{5,.5f},
                     {10,.5f},{11,.5f},{12,.5f},{13,.5f},{15,1},{16,0},{17,.5f},
                     {18,0},{30,0},{38,0},{39,0},{40,0},{41,0},{42,0},{43,0}};
    std::vector<Case> out;
    auto add=[&](std::string name,Values changes={},std::string sig="impulse",
                 std::string routing="mono",double seconds=1.25,int rate=48000)
    {
        auto v=dry;for(auto [id,value]:changes)v[id]=value;
        out.push_back({std::move(name),std::move(sig),std::move(routing),rate,seconds,std::move(v)});
    };
    out.push_back({"default_impulse","impulse","mono",48000,8,{}});
    out.push_back({"default_sweep","sweep","mono",48000,5,{}});
    add("amount_zero",{{15,0}});
    for(int i=0;i<=8;++i)add("pan_"+std::to_string(i),{{3,float(i)/8}});
    for(int i=0;i<=4;++i)
    {
        float v=float(i)/4;
        add("far_"+std::to_string(i),{{0,v}});
        add("far_tones_"+std::to_string(i),{{0,v}},"tones","mono",2.5);
        add("width_left_"+std::to_string(i),{{1,v}},"impulse","left");
        add("width_right_"+std::to_string(i),{{1,v}},"impulse","right");
        add("tilt_"+std::to_string(i),{{2,v}},"tones","mono",2.5);
    }
    for(auto id:{4u,5u,17u})for(int endpoint=0;endpoint<=1;++endpoint)
        for(int side=0;side<=1;++side)
            add("coupling_"+std::to_string(id)+"_"+std::to_string(endpoint)+"_"+std::to_string(side),
                {{id,float(endpoint)},{3,float(side)}});
    for(int side=0;side<=1;++side)
        add("pan_tones_"+std::to_string(side),{{3,float(side)}},"tones","mono",2.5);
    add("mono_check",{{16,1},{3,0}},"impulse","left");
    add("dry_sweep",{},"sweep","mono",2.5);
    add("dry_sweep_left",{{3,0}},"sweep","mono",2.5);
    add("dry_sweep_right",{{3,1}},"sweep","mono",2.5);
    for(int i=0;i<=4;++i)
        add("reverb_distance_"+std::to_string(i),{{18,1},{0,float(i)/4}},"impulse","mono",8);
    for(int i=0;i<=2;++i)
        add("reverb_decay_"+std::to_string(i),{{18,1},{20,float(i)/2},{19,1}},"impulse","mono",12);
    for(auto id:{24u,25u,26u})for(int endpoint=0;endpoint<=1;++endpoint)
        add("distance_coupling_"+std::to_string(id)+"_"+std::to_string(endpoint),
            {{18,1},{0,1},{id,float(endpoint)}},"impulse",id==24?"left":"mono",8);
    // Tempo-sync and modulation are deliberately deferred; no host transport
    // callbacks are supplied. Test only delay-enabled, explicitly labelled.
    add("delay_enabled_default_sync",{{30,1}},"impulse","mono",8);
    for(int rate:{44100,96000})
    {
        add("center_"+std::to_string(rate),{},"impulse","mono",1.25,rate);
        add("left_"+std::to_string(rate),{{3,0}},"impulse","mono",1.25,rate);
        add("right_"+std::to_string(rate),{{3,1}},"impulse","mono",1.25,rate);
    }
    return out;
}
void require(bool test,const char* why){if(!test)throw std::runtime_error(why);}
void selfTest()
{
    auto cases=plan();std::map<std::string,int> names;
    for(const auto& c:cases)
    {
        require(++names[c.name]==1,"duplicate case");
        auto input=signal(c);
        require(input.size()==(c.routing=="mono"?1u:2u),"signal channels");
        require(input[0].size()==size_t(std::ceil(c.seconds*c.rate)),"signal size");
        for(auto [id,value]:c.settings)require(id<44&&value>=0&&value<=1,"invalid parameter case");
    }
    Case c{"test","impulse","mono",48000,1.25,{}};
    auto data=signal(c);auto s=measure(data[0],12000,48000);
    require(s.first==12000&&s.peakIndex==12000&&s.peak==.25&&s.energy==.0625&&s.late==0,"impulse stats");
    require(std::abs(correlation({data[0],data[0]})-1)<1e-9,"correlation");
    c.signal="tones";c.seconds=2.5;data=signal(c);
    require(std::abs(tone(data[0],48000,1000)-amplitude/6)<1e-6,"multitone estimate");
    auto dir=fs::temp_directory_path()/("studio-lab-self-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directory(dir);
    try
    {
        wav(dir/"test.wav",{{.25f,0},{-.25f,0}},48000);
        require(fs::file_size(dir/"test.wav")==72,"WAV size");
        std::ifstream f(dir/"test.wav",std::ios::binary);
        std::string header(12,'\0');f.read(header.data(),12);
        require(header.substr(0,4)=="RIFF"&&header.substr(8,4)=="WAVE","WAV header");
    }catch(...){fs::remove_all(dir);throw;}
    fs::remove_all(dir);
    std::cout<<"Spatial Lab self-test passed ("<<cases.size()<<" measurement cases)\n";
}

AudioComponent locate()
{
    // FourCC values from the supplied successful AU validation report.
    AudioComponentDescription d{};
    d.componentType=kAudioUnitType_Effect;
    d.componentSubType=(UInt32('@')<<24)|(UInt32('s')<<16)|(UInt32('2')<<8)|UInt32('2');
    d.componentManufacturer=(UInt32('A')<<24)|(UInt32('u')<<16)|(UInt32('b')<<8)|UInt32('n');
    auto c=AudioComponentFindNext(nullptr,&d);
    if(!c)throw std::runtime_error("Panagement 2 AU not found. Verify with auval -v aufx '@s22' Aubn");
    UInt32 version=0;check(AudioComponentGetVersion(c,&version),"Read AU version");
    if(version!=0x20800)throw std::runtime_error("This measurement plan is pinned to Panagement 2.8.0 (reported version "+std::to_string(version)+")");
    return c;
}
void verify(const std::vector<Param>& params)
{
    const std::array<const char*,44> expected{
        "Far","Stereo Width","Tilt Filter","Pan","Pan > Delay","Pan > Spectral",
        "LFO Wave","LFO Tempo","LFO Phase","LFO Freq","LFO > Pan","LFO > Far",
        "LFO > Gain","LFO > Tilt","Output Level","Global Amount","Check Mono","Pan > Level",
        "Reverb Enable","Reverb Late Mix","Reverb Decay","Room Tail","Room Size","Room Algorithm",
        "Far > Width","Far > Early","Far > Late Mix","Predelay Time","Predelay Mode","Room Bass",
        "Delay Enable","Delay Mix","Delay Sync","Delay Time","Feedback","Delay Low Cut","Delay High Cut","Delay Diffusion",
        "Circuit Bend RmulDry","Circuit Bend swapLR","Circuit Bend IDx80","Circuit Bend R/8","Circuit Bend noPgPg","Circuit Bend D/8"};
    require(params.size()==expected.size(),"Unexpected parameter count; aborting reference test");
    for(size_t i=0;i<expected.size();++i)
    {
        auto& p=params[i];
        if(p.id!=i||p.name!=expected[i]||p.low!=0||p.high!=1||
           !(p.flags&kAudioUnitParameterFlag_IsReadable)||!(p.flags&kAudioUnitParameterFlag_IsWritable))
            throw std::runtime_error("Parameter schema differs at ID "+std::to_string(i)+": "+p.name);
    }
}
int run(const fs::path& dir)
{
    if(fs::exists(dir))throw std::runtime_error("Output directory already exists; choose a new one");
    auto component=locate();
    fs::create_directories(dir/"audio");fs::create_directories(dir/"input");
    auto manifest=output(dir/"manifest.txt");
    manifest<<"Studio Spatial Laboratory L1\nReference: aufx @s22 Aubn, Panagement 2.8.0\n"
            <<"Offline planar Float32 AUv2, 512 maximum frames, no device I/O or host tempo callbacks.\n"
            <<"All source signals start at 0.25 seconds. Impulses peak at 0.25 (-12.04 dBFS).\n"
            <<"Each case uses a fresh instance; initial 0.25 s silence permits smoothing, not guaranteed steady state.\n"
            <<"Normalized control values are requests, not physical units. No source code or preset state blobs exported.\n"
            <<"Onset is first sample >= max(1e-8, channel peak * 0.001), absolute in file; not a verified ITD estimate.\n"
            <<"Early/late energy is split at 50 ms after excitation (impulses only); finite render window.\n"
            <<"Tone amplitudes use coherent 1-second window 0.75–1.75 s; no assumption of linearity.\n"
            <<"Preset snapshots report actual readback, not internal state. Formatted display values may be empty.\n"
            <<"Delay case uses its default sync value; without tempo callbacks no sync claim.\n"
            <<"Reported render wall time is offline and includes host overhead, not DAW CPU load.\n";
    auto paramsFile=output(dir/"parameters.csv");
    paramsFile<<"id,name,minimum,default,maximum,flags,unit,actual,display\n";
    auto displays=output(dir/"display_values.csv");displays<<"id,requested,display\n";
    auto presetsFile=output(dir/"presets.csv");presetsFile<<"preset_id,preset_name,parameter_id,parameter_name,actual,display,status\n";
    Case infoCase{"metadata","impulse","mono",48000,1.25,{}};
    auto infoSignal=signal(infoCase);Input infoInput{infoSignal};
    Unit metadata(component);metadata.prepare(48000,1,infoInput);
    auto params=metadata.parameters();verify(params);
    for(auto& p:params)
    {
        auto actual=metadata.read(p.id);
        paramsFile<<p.id<<','<<csv(p.name)<<','<<p.low<<','<<p.def<<','<<p.high<<','<<p.flags<<','<<p.unit<<','<<actual<<','<<csv(metadata.formatted(p.id,actual))<<'\n';
        for(int i=0;i<=10;++i)
        {
            float value=float(i)/10;
            displays<<p.id<<','<<value<<','<<csv(metadata.formatted(p.id,value))<<'\n';
        }
    }
    int failures=0;
    auto presets=metadata.presets();
    manifest<<"Factory presets enumerated: "<<presets.size()<<" (validation report: 14)\n";
    if(presets.size()!=14)++failures;
    for(auto& p:presets)
    {
        try
        {
            Input source{infoSignal};Unit u(component);u.prepare(48000,1,source);u.select(p);
            for(auto& param:params)
            {
                auto v=u.read(param.id);
                presetsFile<<p.id<<','<<csv(p.name)<<','<<param.id<<','<<csv(param.name)<<','<<v<<','<<csv(u.formatted(param.id,v))<<",ok\n";
            }
        }
        catch(const std::exception& e)
        {++failures;presetsFile<<p.id<<','<<csv(p.name)<<",,,,,"<<csv(e.what())<<'\n';}
    }
    auto measurements=output(dir/"measurements.csv");
    measurements<<"case,signal,routing,rate,frames,input_file,output_file,peak_l,peak_r,rms_l,rms_r,first_l,first_r,peak_index_l,peak_index_r,energy_l,energy_r,early_l,early_r,late_l,late_r,correlation,reported_latency_s,reported_tail_s,render_wall_ms\n";
    auto values=output(dir/"case_parameters.csv");values<<"case,id,requested,actual_before,actual_after,display_after\n";
    auto spectra=output(dir/"tones.csv");spectra<<"case,frequency_hz,input_amplitude,output_l,output_r\n";
    auto errors=output(dir/"errors.csv");errors<<"case,error\n";
    auto cases=plan();size_t completed=0;
    for(auto& c:cases)
    {
        std::cout<<c.name<<std::endl;
        try
        {
            auto input=signal(c);
            auto key=std::to_string(c.rate)+"_"+c.signal+"_"+c.routing+"_"+std::to_string(input.front().size())+".wav";
            if(!fs::exists(dir/"input"/key))wav(dir/"input"/key,input,c.rate);
            Input source{input};Unit u(component);u.prepare(c.rate,UInt32(input.size()),source);
            u.apply(c.settings);
            Values before;
            for(auto& p:params)before[p.id]=u.read(p.id);
            // Setting requests can be quantized by the AU; record the readbacks.
            double latency=std::numeric_limits<double>::quiet_NaN(),tail=latency;
            u.get(kAudioUnitProperty_Latency,kAudioUnitScope_Global,0,latency);
            u.get(kAudioUnitProperty_TailTime,kAudioUnitScope_Global,0,tail);
            auto start=std::chrono::steady_clock::now();
            auto rendered=u.render(source);
            double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
            auto l=measure(rendered[0],size_t(c.rate*.25),c.rate),r=measure(rendered[1],size_t(c.rate*.25),c.rate);
            auto file=c.name+".wav";wav(dir/"audio"/file,rendered,c.rate);
            for(auto& p:params)
            {
                auto v=u.read(p.id);values<<csv(c.name)<<','<<p.id<<',';
                if(auto it=c.settings.find(p.id);it!=c.settings.end())values<<it->second;
                values<<','<<before.at(p.id)<<','<<v<<','<<csv(u.formatted(p.id,v))<<'\n';
            }
            if(c.signal=="tones")for(auto hz:tones)
                spectra<<csv(c.name)<<','<<hz<<','<<tone(input[0],c.rate,hz)<<','<<tone(rendered[0],c.rate,hz)<<','<<tone(rendered[1],c.rate,hz)<<'\n';
            measurements<<csv(c.name)<<','<<c.signal<<','<<c.routing<<','<<c.rate<<','<<input.front().size()
                <<','<<csv("input/"+key)<<','<<csv("audio/"+file)<<','<<l.peak<<','<<r.peak<<','<<l.rms<<','<<r.rms
                <<','<<l.first<<','<<r.first<<','<<l.peakIndex<<','<<r.peakIndex<<','<<l.energy<<','<<r.energy<<',';
            if(c.signal=="impulse")measurements<<l.early<<','<<r.early<<','<<l.late<<','<<r.late;
            else measurements<<",,,";
            measurements<<','<<correlation(rendered)<<','<<latency<<','<<tail<<','<<ms<<'\n';
            ++completed;
        }
        catch(const std::exception& e)
        {++failures;errors<<csv(c.name)<<','<<csv(e.what())<<'\n';std::cerr<<c.name<<": "<<e.what()<<'\n';}
    }
    manifest<<"Completed: "<<completed<<" / "<<cases.size()<<"\nFailure count (including presets): "<<failures<<"\n";
    std::cout<<"Completed "<<completed<<"/"<<cases.size()<<" cases; failures "<<failures<<".\n";
    return failures?2:0;
}
int hostSmoke()
{
    AudioComponentDescription d{};
    d.componentType=kAudioUnitType_Effect;
    d.componentSubType=kAudioUnitSubType_LowPassFilter;
    d.componentManufacturer=kAudioUnitManufacturer_Apple;
    auto component=AudioComponentFindNext(nullptr,&d);
    if(!component){std::cout<<"System low-pass AU unavailable; host smoke skipped\n";return 77;}
    Case c{"smoke","impulse","dual",48000,1.25,{}};
    auto data=signal(c);Input source{data};
    Unit u(component);u.prepare(48000,2,source);
    auto v=u.render(source);
    auto l=measure(v[0],12000,48000),r=measure(v[1],12000,48000);
    require(l.peak>1e-8&&r.peak>1e-8,"System AU produced no signal");
    require(l.first>=12000&&r.first>=12000,"Unexpected output before excitation");
    std::cout<<"System Audio Unit offline host smoke passed\n";
    return 0;
}
int main(int argc,char** argv)
{
    try
    {
        if(argc==2&&std::string(argv[1])=="--self-test"){selfTest();return 0;}
        if(argc==2&&std::string(argv[1])=="--host-smoke")return hostSmoke();
        if(argc!=2){std::cerr<<"Usage: spatial-lab OUTPUT_DIRECTORY | --self-test\n";return 64;}
        return run(fs::path(argv[1]));
    }
    catch(const std::exception& e){std::cerr<<"Spatial Lab: "<<e.what()<<'\n';return 1;}
}
