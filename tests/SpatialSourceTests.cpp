// SPDX-License-Identifier: GPL-3.0-or-later
#include "../SpatialSource.h"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <vector>
using namespace studio::spatial::source;
static void require(bool test,const char* why)
{
    if(!test){std::cerr<<why<<'\n';std::exit(1);}
}
static bool close(float a,float b,float tol=1e-5f){return std::abs(a-b)<tol;}
static double response(Parameters p,double rate,double hz)
{
    Processor dsp;
    require(dsp.prepare(rate,p),"prepare tone");
    const int n=int(rate*2);
    std::vector<float> l(n),r(n);
    for(int i=0;i<n;++i)l[i]=r[i]=.1f*float(std::sin(6.283185307179586*hz*i/rate));
    dsp.process(l.data(),r.data(),n);
    double e=0,input=0;
    for(int i=n/2;i<n;++i)
    {
        double x=.1*std::sin(6.283185307179586*hz*i/rate);
        e+=double(l[i])*l[i];input+=x*x;
        require(l[i]==r[i],"center mono symmetry");
    }
    return 10*std::log10(e/input);
}
static void widthAndBlend()
{
    Processor dsp;
    std::array<float,4> inputL{.25f,-.4f,.9f,1.2f},inputR{-.1f,.2f,-.3f,1.4f};
    for(float width:{0.f,1.f,2.f})
    {
        Parameters p;p.width=width;
        require(dsp.prepare(48000,p),"prepare width");
        auto l=inputL,r=inputR;
        dsp.process(l.data(),r.data(),int(l.size()));
        for(size_t i=0;i<l.size();++i)
        {
            float mid=.5f*(inputL[i]+inputR[i]),side=.5f*(inputL[i]-inputR[i]);
            require(close(l[i],mid+width*side)&&close(r[i],mid-width*side),"mid/side law");
            require(close(l[i]+r[i],inputL[i]+inputR[i]),"mono sum preserved");
        }
        if(width==1)require(l==inputL&&r==inputR,"default bit-exact normal audio");
    }
    Parameters p;p.distance=1;p.width=2;p.tiltDb=-6;p.amount=0;
    dsp.prepare(48000,p);auto l=inputL,r=inputR;
    dsp.process(l.data(),r.data(),int(l.size()));
    require(l==inputL&&r==inputR,"dry amount zero");
    p.outputDb=6;p.mono=1;dsp.prepare(48000,p);l=inputL;r=inputR;
    dsp.process(l.data(),r.data(),int(l.size()));
    for(size_t i=0;i<l.size();++i)
        require(close(l[i],.5f*(inputL[i]+inputR[i])*gain(6))&&l[i]==r[i],"independent output trim and mono monitor");
    dsp.prepare(48000,{});l=inputL;r={};
    dsp.processMono(l.data(),l.data(),r.data(),int(l.size()));
    require(l==inputL&&r==inputL,"mono in-place alias");
}
static void curves()
{
    for(double rate:{44100.,48000.,96000.})
    {
        double previous=1;
        for(float distance:{0.f,.25f,.5f,.75f,1.f})
        {
            Parameters p;p.distance=distance;
            const double low=response(p,rate,125),high=response(p,rate,8000);
            require(low<previous,"distance monotone low band");previous=low;
            require(high<=low+.01,"distance high frequency damping");
            if(distance==0)require(std::abs(low)<.001&&std::abs(high)<.001,"neutral frequency response");
            if(distance==1)
            {
                require(low>-30.2&&low<-29.9,"far gain target");
                require(high-low<-15&&high-low>-22,"far spectral target");
                p.air=0;require(std::abs(response(p,rate,8000)+30)<.01,"air damping can be disabled");
            }
        }
        Parameters near;near.distance=-1;
        require(response(near,rate,125)>response(near,rate,8000)+1.5,"close bass boost");
        require(std::abs(response(near,rate,8000)-4)<.2,"close level");
        Parameters tilt;tilt.tiltDb=6;
        require(response(tilt,rate,8000)-response(tilt,rate,125)>8,"positive tilt");
        tilt.tiltDb=-6;
        require(response(tilt,rate,125)-response(tilt,rate,8000)>8,"negative tilt");
        Parameters p;p.distance=1;p.distanceWidth=1;
        require(makePlan(rate,p).width==0,"distance width coupling");
    }
}
static void stateAndSafety()
{
    Processor a,b;
    require(!a.prepare(0)&&!a.prepared(),"invalid rate rejected");
    require(a.prepare(48000)&&b.prepare(48000),"prepare automation");
    Parameters p;p.distance=1;p.width=2;p.tiltDb=-5;p.outputDb=6;
    auto plan=makePlan(48000,p);
    require(a.setPlan(plan)&&b.setPlan(plan),"valid plan");
    require(!a.setPlan(makePlan(96000,p)),"wrong-rate plan");
    auto broken=plan;broken.level=std::numeric_limits<float>::quiet_NaN();
    require(!a.setPlan(broken),"invalid plan");
    constexpr int n=48000;
    std::vector<float> l(n,.25f),r(n,-.125f),l2=l,r2=r;
    a.process(l.data(),r.data(),n);
    for(int i=0;i<n;i+=137)b.process(l2.data()+i,r2.data()+i,std::min(137,n-i));
    require(l==l2&&r==r2,"block partition invariance");
    require(std::abs(l.front()-.25f)<.005,"smooth first sample after automation");
    require(std::abs(l.back())<.06,"settled attenuation");
    for(int i=1;i<n;++i)require(std::isfinite(l[i])&&std::abs(l[i]-l[i-1])<.005,"continuous automation");
    a.reset();std::array<float,4> x{},y{};
    auto peaks=a.process(x.data(),y.data(),4);
    require(peaks.left==0&&peaks.right==0,"reset silence");
    a.prepare(192000);
    x={std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN(),1e30f,1e-38f};
    y=x;peaks=a.process(x.data(),y.data(),4);
    require(x[0]==0&&x[1]==0&&x[2]==1e6f&&x[3]==0&&peaks.left==1e6f,"pathological input bounded");
    auto invalid=validate({std::numeric_limits<float>::quiet_NaN(),100,-100,100,-1,9,9,9});
    require(invalid.distance==0&&invalid.width==2&&invalid.tiltDb==-6&&invalid.outputDb==12&&invalid.amount==0&&invalid.air==1&&invalid.distanceWidth==1&&invalid.mono==1,"parameter bounds");
    require(Processor::allocatedBytes()==0&&Processor::latencySamples()==0&&sizeof(Processor)<5120,"fixed footprint");
}
int main()
{
    widthAndBlend();curves();stateAndSafety();
    std::cout<<"P1 native spatial tests passed\n";
    std::cout<<"rate,position,125Hz_dB,1kHz_dB,8kHz_dB\n";
    for(double rate:{44100.,48000.,96000.})for(float distance:{-1.f,0.f,.25f,.5f,.75f,1.f})
    {
        Parameters p;p.distance=distance;
        std::cout<<rate<<','<<distance<<','<<response(p,rate,125)<<','<<response(p,rate,1000)<<','<<response(p,rate,8000)<<'\n';
    }
}
