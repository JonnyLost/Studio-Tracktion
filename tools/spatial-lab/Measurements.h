// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>
namespace lab
{
inline double nan(){return std::numeric_limits<double>::quiet_NaN();}
struct Timing {double lag=nan(),score=nan();};
 // Signed right-minus-left relative delay, estimated using normalized cross
 // correlation. Filtering and ambiguous peaks can bias this diagnostic.
inline Timing timing(const std::vector<float>& l,const std::vector<float>& r,size_t onset,int rate)
{
    if(l.size()!=r.size()||rate<=0||onset>=l.size())return {};
    const int maxLag=int(std::ceil(rate*.003));
    const auto begin=onset>size_t(maxLag*2)?onset-size_t(maxLag*2):0;
    const auto end=std::min(l.size(),onset+size_t(rate*.025));
    if(end<=begin+size_t(maxLag*2))return {};
    std::vector<double> scores(size_t(maxLag*2+1),-2);
    for(int lag=-maxLag;lag<=maxLag;++lag)
    {
        double a=0,b=0,ab=0;
        for(size_t i=begin+size_t(maxLag);i<end-size_t(maxLag);++i)
        {
            const double x=l[i],y=r[size_t(static_cast<long>(i)+lag)];
            a+=x*x;b+=y*y;ab+=x*y;
        }
        if(a>1e-18&&b>1e-18)scores[size_t(lag+maxLag)]=ab/std::sqrt(a*b);
    }
    const auto it=std::max_element(scores.begin(),scores.end());
    const size_t k=size_t(it-scores.begin());
    if(*it<.2||k==0||k+1==scores.size())return {};
    double offset=0;
    const double den=scores[k-1]-2*scores[k]+scores[k+1];
    if(scores[k-1]>=-1&&scores[k+1]>=-1&&den<-1e-12)offset=std::clamp(.5*(scores[k-1]-scores[k+1])/den,-.5,.5);
    return {(double(k)-maxLag+offset)*1000/rate,*it};
}
struct Decay {double t20=nan(),r2=nan(),endDb=nan();};
 // Reverse-integrated stereo energy, 10 ms sampling, fit -5 to -25 dB.
 // Finite-window estimate, no noise-floor compensation. Flag a high residual
 // tail using endDb rather than claiming a fully captured RT60.
inline Decay decay(const std::vector<float>& l,const std::vector<float>& r,size_t onset,int rate)
{
    if(l.size()!=r.size()||rate<=0||onset>=l.size())return {};
    const size_t step=std::max<size_t>(1,size_t(rate/100));
    const size_t count=(l.size()-onset+step-1)/step;
    std::vector<double> e(count,0);
    for(size_t i=onset;i<l.size();++i)
    {double a=l[i],b=r[i];e[(i-onset)/step]+=a*a+b*b;}
    for(size_t i=count-1;i>0;--i)e[i-1]+=e[i];
    if(e[0]<1e-18)return {};
    Decay out;
    const auto tailIndex=count>100?count-100:count-1;
    out.endDb=10*std::log10(std::max(1e-30,e[tailIndex]/e[0]));
    double n=0,x=0,y=0,xx=0,yy=0,xy=0;
    for(size_t i=0;i<count;++i)
    {
        const double db=10*std::log10(std::max(1e-30,e[i]/e[0]));
        if(db>-5||db<-25)continue;
        const double t=double(i*step)/rate;
        ++n;x+=t;y+=db;xx+=t*t;yy+=db*db;xy+=t*db;
    }
    const double dx=n*xx-x*x,dy=n*yy-y*y,dxy=n*xy-x*y;
    if(n<10||dx<=0||dy<=0||dxy>=0)return out;
    out.t20=-60/(dxy/dx);out.r2=dxy*dxy/(dx*dy);
    return out;
}
inline double rmsWindow(const std::vector<float>& v,size_t start,size_t n)
{
    if(!n||start>v.size()||n>v.size()-start)return nan();
    double e=0;for(size_t i=start;i<start+n;++i){double x=v[i];e+=x*x;}
    return std::sqrt(e/double(n));
}
}
