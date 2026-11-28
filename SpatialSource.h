// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "SpatialBinaural.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace studio::spatial::source
{
// Perceptual distance is signed and dimensionless: -1 close, 0 neutral,
// +1 far. A future physical stage adapter maps metres to this control.
// Width 1 is original stereo; 0 mono; 2 exaggerated side signal.
// Tilt is a signed low/high tonal balance in dB, not a calibrated EQ shelf.
struct Parameters
{
    float distance=0, width=1, tiltDb=0, outputDb=0, amount=1;
    float air=1, distanceWidth=0, mono=0;
    binaural::Parameters direction{};
};
inline float bounded(float v,float lo,float hi,float fallback) noexcept
{
    return std::isfinite(v)?std::clamp(v,lo,hi):fallback;
}
inline Parameters validate(Parameters p) noexcept
{
    p.distance=bounded(p.distance,-1,1,0);
    p.width=bounded(p.width,0,2,1);
    p.tiltDb=bounded(p.tiltDb,-6,6,0);
    p.outputDb=bounded(p.outputDb,-24,12,0);
    p.amount=bounded(p.amount,0,1,1);
    p.air=bounded(p.air,0,1,1);
    p.distanceWidth=bounded(p.distanceWidth,0,1,0);
    p.mono=bounded(p.mono,0,1,0);
    p.direction=binaural::validate(p.direction);
    return p;
}
inline float gain(float db) noexcept {return std::pow(10.0f,db/20);}
inline bool supportedRate(double rate) noexcept
{
    return std::isfinite(rate)&&rate>=8000&&rate<=192000;
}
inline float coefficient(double rate,double hz) noexcept
{
    return float(-std::expm1(-6.283185307179586*std::clamp(hz,10.0,rate*.45)/rate));
}
// Fixed-size, bounded handoff. Construct plans off the audio thread.
// Plan fields are validated again by the receiver. No atomic publication
// is implicit; the future host must serialize delivery at block boundaries.
struct Plan
{
    double rate=48000;
    float width=1, level=1, airPole=.2f, airMix=0, proximity=0;
    float proximityPole=.03f, tiltPole=.12f, tiltLow=1, tiltHigh=1;
    float output=1, amount=1, mono=0;
    binaural::Plan direction{};
};
inline Plan makePlan(double rate,Parameters raw) noexcept
{
    if(!supportedRate(rate))rate=48000;
    const auto p=validate(raw);
    const float far=std::max(0.f,p.distance),near=std::max(0.f,-p.distance);
    Plan plan;
    plan.rate=rate;
    plan.width=p.width*(1-far*p.distanceWidth);
    // Original conservative perceptual model; not a reconstruction of the
    // reference effect. L2 endpoints motivate roughly 30 dB distance loss,
    // substantial progressive high-band absorption, and a mild close boost.
    plan.level=gain(-30*far+4*near);
    plan.airPole=coefficient(rate,18000*std::pow(1.0/6.0,double(far)));
    plan.airMix=p.air*std::min(1.f,2*far);
    plan.proximity=gain(3*near)-1;
    plan.proximityPole=coefficient(rate,220);
    plan.tiltPole=coefficient(rate,1000);
    plan.tiltLow=gain(-p.tiltDb);
    plan.tiltHigh=gain(p.tiltDb);
    plan.output=gain(p.outputDb);
    plan.amount=p.amount;plan.mono=p.mono;
    plan.direction=binaural::makePlan(rate,p.direction);
    return plan;
}
struct Peaks {float left=0,right=0;};
class Processor
{
public:
    // Owner thread only, while not processing. No dynamic memory is needed.
    bool prepare(double rate,Parameters p={}) noexcept
    {
        if(!supportedRate(rate))return false;
        const auto next=makePlan(rate,p);
        if(!direction.prepare(rate,next.direction))return false;
        target=next;
        smooth=coefficient(rate,1.0/(6.283185307179586*.015));
        ready=true;reset();
        return true;
    }
    // Called by the processing owner between blocks using a precomputed plan.
    // Reject wrong-rate or nonfinite data; do not perform expensive math here.
    bool setPlan(Plan p) noexcept
    {
        if(!ready||p.rate!=target.rate)return false;
        if(!in(p.width,0,2)||!in(p.level,0,2)||!in(p.airPole,0,1)||
           !in(p.airMix,0,1)||!in(p.proximity,0,1)||!in(p.proximityPole,0,1)||
           !in(p.tiltPole,0,1)||!in(p.tiltLow,.5f-0.001f,2)||
           !in(p.tiltHigh,.5f-0.001f,2)||!in(p.output,.06f,4)||
           !in(p.amount,0,1)||!in(p.mono,0,1))return false;
        if(!direction.setPlan(p.direction))return false;
        target=p;return true;
    }
    // Explicit reset snaps to the current target and clears filter histories.
    // Never reset on each process block.
    void reset() noexcept {current=target;state={};direction.reset();}
    bool prepared() const noexcept {return ready;}
    static constexpr std::size_t allocatedBytes() noexcept {return 0;}
    static constexpr int latencySamples() noexcept {return 0;} // no buffered lookahead
    // In-place distinct stereo buffers. Invalid pointers/counts are no-op.
    Peaks process(float* left,float* right,int frames) noexcept
    {
        Peaks peaks;
        if(!ready||!left||!right||left==right||frames<=0)return peaks;
        for(int i=0;i<frames;++i)
        {
            float l=clean(left[i]),r=clean(right[i]);
            frame(l,r);
            left[i]=l;right[i]=r;
            peaks.left=std::max(peaks.left,std::abs(l));
            peaks.right=std::max(peaks.right,std::abs(r));
        }
        return peaks;
    }
    // Explicit mono-to-stereo path. Input may alias exactly one output;
    // output arrays must be distinct, and partial overlap is not supported.
    Peaks processMono(const float* input,float* left,float* right,int frames) noexcept
    {
        Peaks peaks;
        if(!ready||!input||!left||!right||left==right||frames<=0)return peaks;
        for(int i=0;i<frames;++i)
        {
            float l=clean(input[i]),r=l;
            frame(l,r);
            left[i]=l;right[i]=r;
            peaks.left=std::max(peaks.left,std::abs(l));
            peaks.right=std::max(peaks.right,std::abs(r));
        }
        return peaks;
    }
private:
    struct Channel {float air1=0,air2=0,near=0,tilt=0;};
    Plan target,current;
    std::array<Channel,2> state{};
    binaural::Engine direction;
    float smooth=.001f;
    bool ready=false;
    static bool in(float v,float lo,float hi) noexcept {return std::isfinite(v)&&v>=lo&&v<=hi;}
    // Bound pathological host input to prevent overflow; ordinary audio
    // including float headroom is not limited or clipped at +/-1.
    static float clean(float x) noexcept
    {return std::isfinite(x)&&std::abs(x)>=1e-25f?std::clamp(x,-1e6f,1e6f):0;}
    static float pole(float& z,float x,float a) noexcept
    {
        z+=a*(x-z);
        if(std::abs(z)<1e-25f)z=0;
        return z;
    }
    void move(float& v,float t) const noexcept
    {
        const float delta=t-v;
        v=std::abs(delta)<1e-6f?t:v+smooth*delta;
    }
    void advance() noexcept
    {
        move(current.width,target.width);move(current.level,target.level);
        move(current.airPole,target.airPole);move(current.airMix,target.airMix);
        move(current.proximity,target.proximity);move(current.proximityPole,target.proximityPole);
        move(current.tiltPole,target.tiltPole);move(current.tiltLow,target.tiltLow);
        move(current.tiltHigh,target.tiltHigh);move(current.output,target.output);
        move(current.amount,target.amount);move(current.mono,target.mono);
    }
    float filter(float x,Channel& s) const noexcept
    {
        const auto a=pole(s.air1,x,current.airPole);
        const auto b=pole(s.air2,a,current.airPole);
        const auto distant=x+current.airMix*(b-x);
        const auto close=pole(s.near,distant,current.proximityPole);
        const auto shaped=distant+current.proximity*close;
        const auto low=pole(s.tilt,shaped,current.tiltPole);
        // Unity tilt is explicitly transparent; the states remain warm.
        return (shaped+(current.tiltLow-1)*low+(current.tiltHigh-1)*(shaped-low))*current.level;
    }
    void frame(float& l,float& r) noexcept
    {
        advance();
        const float dryL=l,dryR=r,cross=.5f*(1-current.width);
        const float inputMid=.5f*(l+r);
        const auto wl=current.width==0?inputMid:l+cross*(r-l);
        const auto wr=current.width==0?inputMid:r+cross*(l-r);
        float wetL=filter(wl,state[0]),wetR=filter(wr,state[1]);
        direction.frame(wetL,wetR);
        const auto blend=[this](float dry,float wet) noexcept
        {
            if(current.amount==0)return dry;
            if(current.amount==1)return wet;
            return dry+current.amount*(wet-dry);
        };
        l=blend(dryL,wetL)*current.output;
        r=blend(dryR,wetR)*current.output;
        const float mid=.5f*(l+r);
        if(current.mono==1){l=r=clean(mid);}
        else {l=clean(l+current.mono*(mid-l));r=clean(r+current.mono*(mid-r));}
    }
};
}
