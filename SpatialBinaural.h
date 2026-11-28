// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace studio::spatial::binaural
{
// Original analytic front-hemisphere approximation, not measured HRTFs.
// Angle is listener perspective: negative left, positive right.
inline constexpr double pi=3.14159265358979323846;
inline constexpr double headRadius=0.0875, speedOfSound=343.0;
inline constexpr std::size_t historySize=512;
inline float bound(float v,float lo,float hi,float fallback) noexcept
{
    return std::isfinite(v)?std::clamp(v,lo,hi):fallback;
}
struct Parameters
{
    float angle=0, timing=1, level=1, shadow=1, focus=0;
};
inline Parameters validate(Parameters p) noexcept
{
    p.angle=bound(p.angle,-90,90,0);
    p.timing=bound(p.timing,0,2,1);
    p.level=bound(p.level,0,2,1);
    p.shadow=bound(p.shadow,0,2,1);
    p.focus=bound(p.focus,0,1,0);
    return p;
}
struct Ear {float delay=0,gain=1,shelf=1;};
struct Plan
{
    double rate=48000;
    std::array<Ear,2> ears{};
    float pole=.15f,focus=0;
};
inline bool supported(double rate) noexcept
{
    return std::isfinite(rate)&&rate>=8000&&rate<=192000;
}
inline Plan makePlan(double rate,Parameters raw) noexcept
{
    if(!supported(rate))rate=48000;
    const auto p=validate(raw);
    const double theta=std::abs(double(p.angle))*pi/180;
    const double lateral=std::sin(theta);
    Plan result;result.rate=rate;
    // Woodworth rigid-sphere front-hemisphere path approximation:
    // delay in seconds = r/c * (theta + sin(theta)).
    // Strength is a user-controlled exaggeration, not head anatomy.
    auto& far=result.ears[p.angle<0?1:0];
    far.delay=float(rate*headRadius/speedOfSound*(theta+lateral)*p.timing);
    far.gain=float(std::pow(10.,-4.*lateral*p.level/20.));
    // Two equal first-order low shelves in cascade (unity at DC).
    // Per-section high-frequency gain is half the total dB target.
    far.shelf=float(std::pow(10.,-14.*lateral*p.shadow/40.));
    const double g=std::tan(pi*std::min(2500.,rate*.35)/rate);
    result.pole=float(g/(1+g)); // bilinear/trapezoidal one-pole
    result.focus=float(.5*lateral*p.focus);
    return result;
}
inline bool validPlan(const Plan& p,double rate) noexcept
{
    auto in=[](float x,float a,float b){return std::isfinite(x)&&x>=a&&x<=b;};
    if(!supported(rate)||p.rate!=rate||!in(p.pole,0,1)||!in(p.focus,0,.5f))return false;
    const auto maxDelay=float(rate*headRadius/speedOfSound*(pi/2+1)*2)+.001f;
    for(auto e:p.ears)
        if(!in(e.delay,0,maxDelay)||!in(e.gain,.39f,1)||!in(e.shelf,.19f,1))return false;
    return maxDelay<float(historySize-2);
}
class Engine
{
public:
    bool prepare(double rate,Plan p) noexcept
    {
        if(!validPlan(p,rate))return false;
        target=p;
        smooth=float(-std::expm1(-1./(.020*rate)));
        ready=true;reset();return true;
    }
    // Serialized owner only. No concurrent publication or allocations.
    bool setPlan(Plan p) noexcept
    {
        if(!ready||!validPlan(p,target.rate))return false;
        target=p;return true;
    }
    void reset() noexcept {current=target;state={};cursor=0;}
    void frame(float& left,float& right) noexcept
    {
        if(!ready)return;
        move(current.focus,target.focus);move(current.pole,target.pole);
        const float l=clean(left),r=clean(right);
        // Optional symmetrical narrowing steers a stereo pair toward a
        // common point source. Off by default to preserve stereo material.
        const float x=l+current.focus*(r-l),y=r+current.focus*(l-r);
        left=ear(0,x);right=ear(1,y);
        cursor=(cursor+1)&(historySize-1);
    }
private:
    static_assert((historySize&(historySize-1))==0,"power-of-two ring");
    struct State {std::array<float,historySize> ring{};float z1=0,z2=0;};
    std::array<State,2> state{};
    Plan current,target;
    std::size_t cursor=0;
    float smooth=.001f;
    bool ready=false;
    static float clean(float x) noexcept
    {return std::isfinite(x)&&std::abs(x)>=1e-25f?std::clamp(x,-1e6f,1e6f):0;}
    void move(float& v,float t) const noexcept
    {
        const float d=t-v;
        v=std::abs(d)<1e-6f?t:v+smooth*d;
    }
    static float low(float x,float& z,float a) noexcept
    {
        const float v=(x-z)*a,y=z+v;
        z=clean(y+v);return y;
    }
    float ear(std::size_t i,float x) noexcept
    {
        auto& e=current.ears[i];auto& s=state[i];
        const auto t=target.ears[i];
        move(e.delay,t.delay);move(e.gain,t.gain);move(e.shelf,t.shelf);
        s.ring[cursor]=x;
        const auto whole=std::size_t(e.delay);
        const float fraction=e.delay-float(whole);
        const auto index=(cursor-whole)&(historySize-1);
        const float a=s.ring[index],b=s.ring[(index-1)&(historySize-1)];
        // Causal linear fractional delay. No fixed lookahead; far ear
        // receives an intentional acoustic delay. Warm history at center.
        const float delayed=a+fraction*(b-a);
        const float lo1=low(delayed,s.z1,current.pole);
        const float h1=delayed+(1-e.shelf)*(lo1-delayed);
        const float lo2=low(h1,s.z2,current.pole);
        const float h2=h1+(1-e.shelf)*(lo2-h1);
        return clean(h2*e.gain);
    }
};
}
