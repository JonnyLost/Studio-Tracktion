// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

namespace studio::spatial::early
{
inline constexpr double speed=343.0, pi=3.14159265358979323846, maxSeconds=.25;
inline constexpr size_t count=6;
struct Material {float reflection,cutoff;};
// Approximate amplitude reflection coefficient and high-frequency damping.
// Deliberately not a measured venue or frequency-dependent absorption table.
inline constexpr Material wood{.73f,7500},plaster{.84f,9500},stone{.93f,12000},
    carpet{.34f,2400},treated{.28f,3500};
struct Room
{
    const char* name;
    double width,length,height;
    Material wall,floor,ceiling;
};
inline constexpr std::array<Room,4> rooms{{
    {"Treated studio",8,10,3.5,treated,carpet,treated},
    {"Chamber room",12,17,6,wood,wood,plaster},
    {"Scoring stage",22,27,10,wood,wood,treated},
    {"Stone hall",18,32,13,stone,stone,plaster}
}};
struct Settings {float amount=0,room=2;};
inline Settings validate(Settings p) noexcept
{
    p.amount=std::isfinite(p.amount)?std::clamp(p.amount,0.f,1.f):0.f;
    p.room=std::isfinite(p.room)?std::round(std::clamp(p.room,0.f,float(rooms.size()-1))):2.f;
    return p;
}
struct Point {double x,y,z;};
inline double length(Point a,Point b) noexcept
{
    const double x=a.x-b.x,y=a.y-b.y,z=a.z-b.z;
    return std::sqrt(x*x+y*y+z*z);
}
struct Tap
{
    float delay=1,left=0,right=0,pole=.3f;
    bool equals(const Tap& b) const noexcept
    {return delay==b.delay&&left==b.left&&right==b.right&&pole==b.pole;}
};
struct Plan
{
    double rate=48000;
    std::array<Tap,count> taps{};
    bool enabled=false;
    bool equals(const Plan& p) const noexcept
    {
        if(rate!=p.rate||enabled!=p.enabled)return false;
        for(size_t i=0;i<count;++i)if(!taps[i].equals(p.taps[i]))return false;
        return true;
    }
};
inline bool supported(double r) noexcept{return std::isfinite(r)&&r>=8000&&r<=192000;}
inline Plan makePlan(double rate,float angle,float distance,Settings raw) noexcept
{
    if(!supported(rate))rate=48000;
    const auto p=validate(raw);
    Plan plan;plan.rate=rate;
    if(p.amount==0)return plan;
    const auto& room=rooms[size_t(p.room)];
    const double theta=(std::isfinite(angle)?std::clamp(double(angle),-90.,90.):0.)*pi/180;
    const double depth=std::isfinite(distance)?std::clamp(double(distance),-1.,1.):0.;
    const double dx=std::sin(theta),dy=std::cos(theta);
    const Point listener{0,room.length*.23,1.4};
    // P3 distance remains perceptual. Map it to an illustrative radial
    // source position, then constrain the radius to the room interior.
    // The dry path is the timing origin; do not delay it by the flight time.
    double radius=4+(depth<0?2.8:10.)*depth;
    if(std::abs(dx)>1e-8)radius=std::min(radius,(room.width*.5-.5)/std::abs(dx));
    if(dy>1e-8)radius=std::min(radius,(room.length-listener.y-.5)/dy);
    const Point s{radius*dx,listener.y+radius*dy,listener.z};
    const std::array<Point,count> images{{
        {-room.width-s.x,s.y,s.z},{room.width-s.x,s.y,s.z},
        {s.x,-s.y,s.z},{s.x,2*room.length-s.y,s.z},
        {s.x,s.y,-s.z},{s.x,s.y,2*room.height-s.z}
    }};
    const std::array<Material,count> materials{room.wall,room.wall,room.wall,room.wall,room.floor,room.ceiling};
    const double direct=length(s,listener);
    for(size_t i=0;i<count;++i)
    {
        const auto image=images[i];
        const double path=length(image,listener);
        const double excess=std::max(0.,path-direct);
        // Constant-power directional stereo distribution of reflected rays.
        // Not individualized binaural reflection HRTFs.
        const double pan=std::clamp((image.x-listener.x)/path,-1.,1.);
        const double amplitude=.20*p.amount*materials[i].reflection*direct/path;
        auto& t=plan.taps[i];
        t.delay=float(std::clamp(excess/speed*rate,1.,maxSeconds*rate));
        t.left=float(amplitude*std::sqrt(.5*(1-pan)));
        t.right=float(amplitude*std::sqrt(.5*(1+pan)));
        const double hz=std::min(double(materials[i].cutoff)/(1+.018*excess),rate*.45);
        t.pole=float(-std::expm1(-2*pi*hz/rate));
    }
    plan.enabled=true;
    return plan;
}
inline bool valid(const Plan& p,double rate) noexcept
{
    if(!supported(rate)||rate!=p.rate)return false;
    auto bounded=[](float x,double lo,double hi){return std::isfinite(x)&&x>=lo&&x<=hi;};
    for(auto t:p.taps)
        if(!bounded(t.delay,1,maxSeconds*rate+.001)||!bounded(t.left,0,.20)||
           !bounded(t.right,0,.20)||!bounded(t.pole,.001,1)||
           (!p.enabled&&(t.left!=0||t.right!=0)))return false;
    return true;
}
class Engine
{
public:
    // Preparation only; the render path never resizes, allocates or locks.
    bool prepare(double rate,Plan p) noexcept
    {
        if(!valid(p,rate))return false;
        size_t n=1;while(n<size_t(std::ceil(rate*maxSeconds))+2)n*=2;
        try{ring.assign(n,0.f);}catch(...){ready=false;return false;}
        mask=n-1;fadeFrames=std::max(1,int(std::round(rate*.04)));
        target=p;ready=true;reset();return true;
    }
    bool setPlan(Plan p) noexcept
    {
        if(!ready||!valid(p,target.rate))return false;
        // Keep latest pending geometry, finish current bounded crossfade.
        // Never sweep a delay line across a long distance on mouse drag.
        target=p;return true;
    }
    void reset() noexcept
    {
        std::fill(ring.begin(),ring.end(),0.f);cursor=0;
        from=to=target;states={};nextStates={};remaining=0;
    }
    size_t allocatedBytes()const noexcept{return ring.capacity()*sizeof(float);}
    bool prepared()const noexcept{return ready;}
    void frame(float input,float& left,float& right) noexcept
    {
        if(!ready)return;
        ring[cursor]=clean(input);
        if(remaining==0&&!from.equals(target))
        {
            to=target;nextStates={};remaining=fadeFrames;
        }
        // At zero amount retain warm input history but skip all six taps.
        if(remaining>0)
        {
            auto a=render(from,states),b=render(to,nextStates);
            const float blend=float(fadeFrames-remaining+1)/float(fadeFrames);
            left+=a[0]+blend*(b[0]-a[0]);right+=a[1]+blend*(b[1]-a[1]);
            if(--remaining==0){from=to;states=nextStates;}
        }
        else if(from.enabled)
        {
            const auto a=render(from,states);left+=a[0];right+=a[1];
        }
        cursor=(cursor+1)&mask;
    }
private:
    using Filters=std::array<float,count>;
    std::vector<float> ring;
    size_t cursor=0,mask=0;
    Plan from,to,target;
    Filters states{},nextStates{};
    int fadeFrames=1,remaining=0;
    bool ready=false;
    static float clean(float x) noexcept
    {return std::isfinite(x)&&std::abs(x)>=1e-25f?std::clamp(x,-1e6f,1e6f):0.f;}
    std::array<float,2> render(const Plan& p,Filters& filter) noexcept
    {
        std::array<float,2> out{};
        if(!p.enabled)return out;
        for(size_t i=0;i<count;++i)
        {
            const auto& t=p.taps[i];
            const auto whole=size_t(t.delay),index=(cursor-whole)&mask;
            const float a=ring[index],b=ring[(index-1)&mask];
            const float x=a+(t.delay-float(whole))*(b-a);
            auto& z=filter[i];z=clean(z+t.pole*(x-z));
            out[0]+=z*t.left;out[1]+=z*t.right;
        }
        return out;
    }
};
}
