// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "SamplerEngine.h"
#include <map>
#include <mutex>
#include <string>
#include <tuple>

namespace studio::sampler
{
// Control/import threads only. Weak ownership means unloaded instruments do
// not leave recordings resident. The decoder factory is serialized so two
// simultaneous imports cannot allocate the same sample or overrun the cap.
class SharedSamplePool
{
public:
    struct Key
    {
        std::string path;
        std::int64_t size=0,time=0;
        bool operator<(const Key& other) const
        {return std::tie(path,size,time)<std::tie(other.path,other.size,other.time);}
    };
    enum class Outcome { ready, budgetExceeded, failed };
    struct Snapshot {std::size_t bytes=0,count=0;};
    explicit SharedSamplePool(std::size_t maximum):limit(maximum){}
    template<class Factory>
    Outcome acquire(const Key& key,std::size_t bankBudget,std::size_t estimate,
                    std::shared_ptr<const Sample>& out,Factory&& factory)
    {
        std::lock_guard lock(mutex);
        auto totals=prune();
        if(auto i=entries.find(key);i!=entries.end())
        {
            if(auto sample=i->second.lock())
            {
                if(sample->bytes()>bankBudget)return Outcome::budgetExceeded;
                out=std::move(sample);return Outcome::ready;
            }
        }
        if(estimate>bankBudget||estimate>limit-totals.bytes)return Outcome::budgetExceeded;
        auto sample=factory();
        if(!sample)return Outcome::failed;
        if(sample->bytes()>bankBudget||sample->bytes()>limit-totals.bytes)return Outcome::budgetExceeded;
        entries[key]=sample;out=std::move(sample);return Outcome::ready;
    }
    Snapshot snapshot(){std::lock_guard lock(mutex);return prune();}
private:
    Snapshot prune()
    {
        Snapshot totals;
        for(auto i=entries.begin();i!=entries.end();)
        {
            if(auto sample=i->second.lock())
            {totals.bytes+=sample->bytes();++totals.count;++i;}
            else i=entries.erase(i);
        }
        return totals;
    }
    const std::size_t limit;
    std::mutex mutex;
    std::map<Key,std::weak_ptr<const Sample>> entries;
};
inline constexpr std::size_t maxSharedSampleBytes=768ull*1024*1024;
inline SharedSamplePool& sharedSamples()
{
    static SharedSamplePool pool(maxSharedSampleBytes);
    return pool;
}
}
