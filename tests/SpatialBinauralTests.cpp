// SPDX-License-Identifier: GPL-3.0-or-later
#include "../SpatialSource.h"
#include <array>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <vector>

namespace src=studio::spatial::source;
namespace bin=studio::spatial::binaural;
static void require(bool ok,const char* reason)
{
    if(!ok){std::cerr<<reason<<'\n';std::exit(1);}
}
static bool close(double a,double b,double tol=1e-5){return std::abs(a-b)<tol;}
static src::Parameters params(float angle,float timing=1,float level=1,float shadow=1,float focus=0)
{
    src::Parameters p;p.direction={angle,timing,level,shadow,focus};return p;
}
static double centroid(const std::vector<float>& v)
{
    double sum=0,weighted=0;
    for(size_t i=0;i<v.size();++i){const auto x=std::abs(double(v[i]));sum+=x;weighted+=x*double(i);}
    require(sum>.01,"nonzero impulse");
    return weighted/sum;
}
static std::pair<double,double> response(double rate,double hz,src::Parameters p)
{
    src::Processor dsp;
    require(dsp.prepare(rate,p),"tone preparation");
    const auto n=int(rate);
    std::vector<float> l(n),r(n);
    for(int i=0;i<n;++i)l[i]=r[i]=.1f*float(std::sin(2*bin::pi*hz*i/rate));
    dsp.process(l.data(),r.data(),n);
    double le=0,re=0,in=0;
    for(int i=n/2;i<n;++i)
    {
        le+=double(l[i])*l[i];re+=double(r[i])*r[i];
        const auto x=.1*std::sin(2*bin::pi*hz*i/rate);in+=x*x;
    }
    return {10*std::log10(le/in),10*std::log10(re/in)};
}
static void timing()
{
    std::cout<<"rate,angle,timing_strength,measured_right_minus_left_ms\n";
    for(double rate:{8000.,44100.,48000.,96000.,192000.})
    for(float angle:{-90.f,-45.f,0.f,45.f,90.f})
    for(float strength:{0.f,1.f,2.f})
    {
        src::Processor dsp;
        require(dsp.prepare(rate,params(angle,strength,0,0)),"timing preparation");
        std::vector<float> l(1024),r(1024);l[320]=r[320]=.25f;
        dsp.process(l.data(),r.data(),int(l.size()));
        const auto measured=(centroid(r)-centroid(l))*1000/rate;
        const double theta=std::abs(double(angle))*bin::pi/180;
        const double expected=1000*bin::headRadius/bin::speedOfSound*(theta+std::sin(theta))*strength*(angle>0?-1:1);
        require(close(measured,expected,.0001),"physical-time consistency and sign");
        require((angle>=0?r[320]:l[320])==.25f,"near ear unshifted");
        std::cout<<rate<<','<<angle<<','<<strength<<','<<measured<<'\n';
    }
}
static void cues()
{
    std::cout<<"rate,hz,level_strength,shadow_strength,near_db,far_db\n";
    for(double rate:{44100.,48000.,96000.})
    {
        for(float strength:{0.f,1.f,2.f})
        {
            for(double hz:{125.,1000.,4000.,8000.})
            {
                const auto l=response(rate,hz,params(-90,0,strength,0));
                require(close(l.first,0,.001)&&close(l.second,-4*strength,.001),"isolated frequency-independent level");
                const auto s=response(rate,hz,params(-90,0,0,strength));
                const auto mirror=response(rate,hz,params(90,0,0,strength));
                require(close(s.first,0,.001)&&close(s.first,mirror.second,.001)&&close(s.second,mirror.first,.001),"head shadow mirror symmetry");
                if(hz==125)require(s.second>-.15&&s.second<=.001,"low band preserved");
                if(hz==8000)
                {
                    if(strength==1)require(s.second<-9&&s.second>-14,"normal shadow high-band range");
                    if(strength==2)require(s.second<-16&&s.second>-28,"enhanced shadow high-band range");
                    if(strength==0)require(close(s.second,0,.001),"disabled shadow transparent");
                }
                std::cout<<rate<<','<<hz<<','<<strength<<",0,"<<l.first<<','<<l.second<<'\n';
                std::cout<<rate<<','<<hz<<",0,"<<strength<<','<<s.first<<','<<s.second<<'\n';
            }
            auto lo=response(rate,125,params(-90,0,0,strength));
            auto hi=response(rate,8000,params(-90,0,0,strength));
            require(strength==0||lo.second>hi.second,"shadow frequency dependence");
        }
    }
}
static void stereoAndBlend()
{
    src::Processor dsp;
    std::array<float,4> a{.25f,-.4f,.9f,1.2f},b{-.1f,.2f,-.3f,1.4f};
    auto p=params(-90,0,0,0);
    require(dsp.prepare(48000,p),"disabled preparation");
    auto l=a,r=b;dsp.process(l.data(),r.data(),int(l.size()));
    require(l==a&&r==b,"all three cues disabled, stereo transparent");
    p.direction.focus=1;
    require(dsp.prepare(48000,p),"focus preparation");
    l=a;r=b;dsp.process(l.data(),r.data(),int(l.size()));
    for(size_t i=0;i<l.size();++i)require(l[i]==r[i]&&close(l[i],.5f*(a[i]+b[i])),"full focus point source");
    p=params(0,2,2,2,1);
    dsp.prepare(48000,p);l=a;r=b;dsp.process(l.data(),r.data(),int(l.size()));
    require(l==a&&r==b,"center transparent even at full cue strengths");
    p=params(-90,2,2,2,1);p.distance=1;p.amount=0;
    dsp.prepare(48000,p);l=a;r=b;dsp.process(l.data(),r.data(),int(l.size()));
    require(l==a&&r==b,"P1 dry path independent of binaural");
    p.amount=1;p.mono=1;
    dsp.prepare(48000,p);l=a;r=b;dsp.process(l.data(),r.data(),int(l.size()));
    require(l==r,"mono check after binaural");
    dsp.prepare(48000,params(-90,0,1,0));
    l=a;r={};dsp.processMono(l.data(),l.data(),r.data(),int(l.size()));
    for(size_t i=0;i<l.size();++i)
        require(close(l[i],a[i])&&close(r[i],a[i]*src::gain(-4)),"mono input to both ears");
}
static void automation()
{
    src::Processor a,b;
    require(a.prepare(48000)&&b.prepare(48000),"automation prepare");
    constexpr int n=48000;
    std::vector<float> l(n),r(n),l2,r2;
    for(int i=0;i<n;++i)l[i]=r[i]=.2f*float(std::sin(2*bin::pi*440*i/48000));
    l2=l;r2=r;
    for(int i=0;i<n;i+=4800)
    {
        auto p=params((i/4800)%2==0?-90.f:90.f,2,2,2,.5f);p.distance=.2f;
        auto plan=src::makePlan(48000,p);
        require(a.setPlan(plan)&&b.setPlan(plan),"moving source plan");
        auto bad=plan;bad.direction.ears[0].delay=512;
        require(!a.setPlan(bad),"oversized delay rejected");
        bad=plan;bad.direction.pole=1;
        require(!a.setPlan(bad),"unstable pole rejected");
        bad=plan;bad.direction.ears[0].gain=std::numeric_limits<float>::quiet_NaN();
        require(!a.setPlan(bad),"invalid ear gain rejected");
        bad=plan;bad.direction.rate=44100;
        require(!a.setPlan(bad),"wrong-rate binaural plan rejected");
        a.process(l.data()+i,r.data()+i,4800);
        for(int j=0;j<4800;j+=137)b.process(l2.data()+i+j,r2.data()+i+j,std::min(137,4800-j));
    }
    require(l==l2&&r==r2,"block-size invariant animated panning");
    for(int i=1;i<n;++i)
    {
        require(std::isfinite(l[i])&&std::isfinite(r[i]),"finite animated processing");
        require(std::abs(l[i]-l[i-1])<.06&&std::abs(r[i]-r[i-1])<.06,"bounded moving-source discontinuity");
    }
    a.reset();std::array<float,1024> x{},y{};
    a.process(x.data(),y.data(),int(x.size()));
    require(x==std::array<float,1024>{}&&y==x,"reset clears ear histories");
    require(sizeof(src::Processor)<5120&&src::Processor::allocatedBytes()==0,"fixed memory footprint");
}
static void benchmark()
{
    // Offline throughput only: no Tracktion, disk, AU, or real-time deadline.
    constexpr int frames=256,blocks=375;
    constexpr double rate=48000;
    std::cout<<"sizeof_processor_bytes,"<<sizeof(src::Processor)<<'\n';
    std::cout<<"sources,elapsed_ms,aggregate_render_realtime_ratio,checksum\n";
    for(int count:{16,32,64,128})
    {
        std::vector<src::Processor> sources(static_cast<size_t>(count));
        for(int i=0;i<count;++i)
        {
            auto p=params(float(i)*180/float(count-1)-90,1,1,1,.25f);
            p.distance=float(i%7)/7;
            require(sources[size_t(i)].prepare(rate,p),"benchmark prepare");
        }
        std::array<float,frames> input{},l{},r{};
        for(int i=0;i<frames;++i)input[size_t(i)]=.1f*float(std::sin(2*bin::pi*440*i/rate));
        double sum=0;
        const auto start=std::chrono::steady_clock::now();
        for(int b=0;b<blocks;++b)for(auto& dsp:sources)
        {
            dsp.processMono(input.data(),l.data(),r.data(),frames);
            sum+=l[size_t(b%frames)]+r[size_t((b+17)%frames)];
        }
        const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        require(std::isfinite(sum),"benchmark finite output");
        std::cout<<count<<','<<ms<<','<<ms/(1000.*frames*blocks/rate)<<','<<sum<<'\n';
    }
}
int main()
{
    timing();cues();stereoAndBlend();automation();benchmark();
    std::cout<<"P2 native binaural tests passed\n";
}
