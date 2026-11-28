// SPDX-License-Identifier: GPL-3.0-or-later
#include "../SpatialSource.h"
#include <cstdlib>
#include <iostream>
#include <limits>
#include <vector>
namespace early=studio::spatial::early;
namespace source=studio::spatial::source;
static void require(bool ok,const char* message)
{
    if(!ok){std::cerr<<message<<'\n';std::exit(1);}
}
static bool close(double a,double b,double tolerance=1e-5){return std::abs(a-b)<tolerance;}
static double energy(const std::vector<float>& v)
{
    double e=0;for(float x:v){require(std::isfinite(x),"finite reflection output");e+=double(x)*x;}return e;
}
static void geometry()
{
    std::cout<<"room,rate,angle,distance,surface,delay_ms,left_gain,right_gain,pole\n";
    for(double rate:{8000.,44100.,48000.,96000.,192000.})
    for(int room=0;room<4;++room)
    for(float depth:{-1.f,0.f,1.f})
    {
        const early::Settings settings{1,float(room)};
        const auto left=early::makePlan(rate,-45,depth,settings),right=early::makePlan(rate,45,depth,settings);
        require(early::valid(left,rate)&&early::valid(right,rate),"bounded geometry");
        const std::array<size_t,6> mirrored{1,0,2,3,4,5};
        for(size_t i=0;i<early::count;++i)
        {
            const auto& l=left.taps[i];const auto& r=right.taps[mirrored[i]];
            require(close(l.delay,r.delay,.001)&&close(l.left,r.right)&&close(l.right,r.left),"reflection mirror");
            const auto reference=early::makePlan(48000,-45,depth,settings).taps[i];
            require(close(l.delay/rate,reference.delay/48000,.00013),"sample rate physical time");
            std::cout<<room<<','<<rate<<",-45,"<<depth<<','<<i<<','<<1000*l.delay/rate<<','<<l.left<<','<<l.right<<','<<l.pole<<'\n';
        }
    }
    const auto center=early::makePlan(48000,0,0,{1,2});
    const double floor=(std::sqrt(4*4+2.8*2.8)-4)/343*48000;
    require(close(center.taps[4].delay,floor,.001),"floor image excess path");
    require(close(center.taps[0].delay,center.taps[1].delay)&&close(center.taps[0].left,center.taps[1].right),"center symmetry");
    const auto closePlan=early::makePlan(48000,0,-1,{1,2});
    const auto farPlan=early::makePlan(48000,0,1,{1,2});
    require(farPlan.taps[4].delay<closePlan.taps[4].delay&&farPlan.taps[4].left>closePlan.taps[4].left,"distance changes relative reflection path and gain");
    const auto treated=early::makePlan(48000,0,0,{1,0});
    const auto stone=early::makePlan(48000,0,0,{1,3});
    require(treated.taps[4].pole<stone.taps[4].pole&&treated.taps[4].left<stone.taps[4].left,"floor material difference");
    require(!early::makePlan(48000,0,0,{}).enabled,"default reflection off");
}
static void render()
{
    constexpr double rate=48000;
    const auto plan=early::makePlan(rate,-35,.2f,{1,2});
    early::Engine engine;
    require(engine.prepare(rate,plan),"prepare reflection history");
    require(engine.allocatedBytes()<=65536,"48k ring capacity");
    std::vector<float> l(16000),r(l.size());
    for(size_t i=0;i<l.size();++i)engine.frame(i==0?1.f:0.f,l[i],r[i]);
    float minDelay=1e9f;for(auto t:plan.taps)minDelay=std::min(minDelay,t.delay);
    for(size_t i=0;i<size_t(minDelay);++i)require(l[i]==0&&r[i]==0,"no pre-arrival or direct duplicate");
    require(energy(l)>1e-5&&energy(r)>1e-5,"nonzero stereo reflections");
    require(energy(l)<1&&energy(r)<1,"bounded impulse energy");
    engine.reset();float a=0,b=0;engine.frame(0,a,b);require(a==0&&b==0,"reset clears history");
    auto bad=plan;bad.taps[0].delay=1e9f;require(!engine.setPlan(bad),"reject invalid delay");
    bad=plan;bad.taps[0].pole=std::numeric_limits<float>::quiet_NaN();require(!engine.setPlan(bad),"reject nonfinite pole");
    require(!engine.setPlan(early::makePlan(44100,0,0,{1,2})),"reject different rate");
    for(double sr:{8000.,192000.})require(engine.prepare(sr,early::makePlan(sr,90,1,{1,3}))&&engine.allocatedBytes()<=262144,"extreme rate bound");
}
static void integration()
{
    constexpr int n=16000;constexpr double rate=48000;
    source::Parameters p;
    source::Processor a,b;early::Engine er;
    require(a.prepare(rate,p)&&b.prepare(rate,p)&&er.prepare(rate,early::makePlan(rate,0,0,{})),"neutral prepare");
    std::vector<float> l(n),r(n),x(n),y(n);l[64]=r[64]=x[64]=y[64]=.25f;
    a.process(l.data(),r.data(),n);b.process(x.data(),y.data(),n,&er);
    require(l==x&&r==y,"disabled reflections preserve P3 output");
    require(er.prepare(rate,early::makePlan(rate,0,0,{1,2})),"wet preparation");
    x=l;y=r;b.prepare(rate,p);b.process(x.data(),y.data(),n,&er);
    require(x[64]==.25f&&y[64]==.25f&&energy(x)>energy(l)&&energy(y)>energy(r),"direct plus later reflections");
    p.amount=0;b.prepare(rate,p);er.reset();x=l;y=r;b.process(x.data(),y.data(),n,&er);
    require(l==x&&r==y,"amount zero includes reflections in bypass");
    p.amount=1;p.mono=1;b.prepare(rate,p);er.reset();x=l;y=r;b.process(x.data(),y.data(),n,&er);
    require(x==y,"mono monitor includes reflection output");
}
static void transitions()
{
    constexpr double rate=48000;constexpr int n=48000;
    early::Engine a,b;
    auto start=early::makePlan(rate,-30,0,{1,1});
    require(a.prepare(rate,start)&&b.prepare(rate,start),"transition prep");
    std::vector<float> l(n),r(n),x(n),y(n);
    for(int i=0;i<n;++i)
    {
        if(i%997==0)
        {
            auto p=early::makePlan(rate,float((i/997)%3-1)*70,float((i/997)%3-1),{float(i%2),float((i/997)%4)});
            require(a.setPlan(p)&&b.setPlan(p),"rapid geometry update");
        }
        float in=.1f*float(std::sin(2*early::pi*440*i/rate));
        a.frame(in,l[i],r[i]);b.frame(in,x[i],y[i]);
        require(std::isfinite(l[i])&&std::isfinite(r[i]),"finite transition");
        if(i)require(std::abs(l[i]-l[i-1])<.07f&&std::abs(r[i]-r[i-1])<.07f,"bounded transition discontinuity");
    }
    require(l==x&&r==y,"deterministic geometry automation");
    const auto off=early::makePlan(rate,0,0,{});
    require(a.setPlan(off),"disable reflections");
    float left=0,right=0;
    for(int i=0;i<6000;++i){left=right=0;a.frame(.1f,left,right);}
    require(left==0&&right==0,"settled reflection off");
}
int main()
{
    geometry();render();integration();transitions();
    std::cout<<"P4 early reflection tests passed\n";
}
