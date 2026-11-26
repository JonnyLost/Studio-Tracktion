// SPDX-License-Identifier: GPL-3.0-or-later
#include "../SamplerSharedPool.h"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cmath>
#include <thread>
using namespace studio::sampler;
int main()
{
    SharedSamplePool pool(8192);
    std::atomic<int> decoded{0};
    auto factory=[&]()->std::shared_ptr<const Sample>
    {
        ++decoded;auto s=std::make_shared<Sample>();s->rate=48000;s->left.resize(1024,0.1f);return s;
    };
    const SharedSamplePool::Key key{"/recording.wav",123,1};
    std::shared_ptr<const Sample> a,b,c;
    std::thread x([&]{assert(pool.acquire(key,8192,4096,a,factory)==SharedSamplePool::Outcome::ready);});
    std::thread y([&]{assert(pool.acquire(key,8192,4096,b,factory)==SharedSamplePool::Outcome::ready);});
    x.join();y.join();
    assert(a==b&&decoded==1&&pool.snapshot().bytes==4096&&pool.snapshot().count==1);
    assert(pool.acquire(key,1024,4096,c,factory)==SharedSamplePool::Outcome::budgetExceeded);
    assert(pool.acquire({key.path,123,2},8192,4096,c,factory)==SharedSamplePool::Outcome::ready&&c!=a);
    assert(pool.acquire({"/other.wav",123,1},8192,4096,c,factory)==SharedSamplePool::Outcome::budgetExceeded);
    a.reset();b.reset();c.reset();assert(pool.snapshot().bytes==0);
    assert(StreamCache::capacityBytes(true,streamPreloadFrames+17)==2*sizeof(float)*streamPageFrames);
    assert(StreamCache::capacityBytes(true,20*streamPageFrames)==2*sizeof(float)*streamPageFrames*streamCachePages);

    auto sample=std::make_shared<Sample>();sample->rate=48000;sample->left.resize(8192,0.01f);
    Bank bank;bank.sampleRevision=1;bank.settings.polyphony=maxVoices;bank.settings.attack=0;bank.settings.gainDb=0;
    for(int note=0;note<128;++note)for(int layer=0;layer<8;++layer)
    {
        Zone z;z.sample=sample;z.root=z.low=z.high=note;z.soft=layer*16+1;z.hard=std::min(127,(layer+1)*16);bank.zones.push_back(z);
    }
    assert(bank.zones.size()==maxZones);
    auto slow=bank;bank.buildIndex();assert(bank.indexed&&bank.noteZones[60].size()==8);
    Engine indexed,reference;indexed.prepare(48000);reference.prepare(48000);
    constexpr int block=128;
    std::array<float,block> il{},rl{};
    for(int t=0;t<16;++t)
    {
        indexed.use(bank);reference.use(slow);
        if(t==0)for(int n=0;n<128;++n){indexed.midi(0x90,n,100);reference.midi(0x90,n,100);}
        if(t==8)for(int n=0;n<128;++n){indexed.midi(0x80,n,0);reference.midi(0x80,n,0);}
        il.fill(0);rl.fill(0);indexed.render(il.data(),nullptr,block);reference.render(rl.data(),nullptr,block);
        indexed.publishMeters();reference.publishMeters();
        assert(indexed.activeVoices()==reference.activeVoices());
        if(t==0)assert(indexed.activeVoices()==128);
        for(int i=0;i<block;++i)assert(std::abs(il[i]-rl[i])<1e-7f);
    }
    // 32 sampler instances, same immutable mapped recording and bank.
    // This is an engine workload, not an actual DAW/SSD benchmark.
    std::vector<std::unique_ptr<Engine>> tracks;
    for(int i=0;i<32;++i){auto e=std::make_unique<Engine>();e->prepare(48000);tracks.push_back(std::move(e));}
    const auto start=std::chrono::steady_clock::now();
    for(int b=0;b<128;++b)
        for(auto& e:tracks)
        {
            e->use(bank);
            if(b==0)for(int n=48;n<64;++n)e->midi(0x90,n,100);
            il.fill(0);e->render(il.data(),nullptr,block);e->publishMeters();
            for(auto v:il)assert(std::isfinite(v));
        }
    const auto ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    std::printf("Sampler M26: shared allocation, budget, adaptive page capacity, 1024-zone indexed parity, 128 voices, 32-engine stress passed (%.1f ms synthetic workload).\n",ms);
}
